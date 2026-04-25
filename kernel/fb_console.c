// 基于线性帧缓冲的图形控制台底层（引导阶段已切换 VBE 模式 0x118）
#include "types.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "x86.h"
#include "fb_console.h"

extern const uchar g_font8x16[128][16];

#define CHAR_W 8
#define CHAR_H 16

static int fb_ok;
/* 用户态全屏 gui() 活动期间暂停帧缓冲文本与光标，避免与桌面像素叠加产生花屏 */
static int fb_gui_mode;
static uchar *fb_kva;
static uint fb_pitch;
static uint fb_width;
static uint fb_height;
static uint fb_bpp;
static uint fb_cols;
static uint fb_rows;

/*
 * 与帧缓冲同步的字符格影子（字形 + VGA 属性），供光标重绘时恢复底层字符。
 * 旧实现 fb_paint_cursor 先整格画空格再画下划线，会擦掉光标下的字母，插入态左移时像「覆盖」字符。
 */
#define FB_CELL_MAX 65536
static uchar fb_cell_ch[FB_CELL_MAX];
static uchar fb_cell_attr[FB_CELL_MAX];
static void fb_copy_scanline(uchar *dst, const uchar *src, uint n);
static void fb_copy_dwords(uchar *dst, const uchar *src, uint n);
static void fb_glyph_render_pixels_to(uchar *base, uint pitch, uint bpp,
                                      uint col, uint row, uchar ch, uchar cga);

/*
 * 文本区软件后备缓冲（double-buffer）：
 * - 仅覆盖可滚动正文区（不含最后一行 panic 行）；
 * - 滚屏时在 RAM 中 memmove，再整块写回显存，避免逐字符重绘整屏。
 * X220 上超过一屏后主要卡在滚屏重绘路径，这里用 Linux 常见的“后备缓冲+批量 blit”降低开销。
 */
#define FB_BACKBUF_MAX (6 * 1024 * 1024)
static uchar fb_backbuf[FB_BACKBUF_MAX];
static int fb_backbuf_ok;
static uint fb_text_rows;      /* 参与滚屏的字符行数（通常 fb_rows-1） */
static uint fb_text_px_rows;   /* 对应像素行数 */
static uint fb_backbuf_bytes;  /* fb_pitch * fb_text_px_rows */
#define FB_ROW_MAX 1024
static uchar fb_row_dirty[FB_ROW_MAX];
/*
 * 脏行同步（类似 Linux fbcon 的 damage flush 思路）：
 * 平时字符输出只更新显存并打脏标；滚屏前再把脏行一次性重绘到后备缓冲，
 * 避免每个字符都对后备缓冲再写一遍导致写放大。
 */
static void
fb_sync_dirty_rows_to_backbuf(void)
{
  /*
   * X220 优化：改为“逐字符增量同步 backbuf”（见 fb_glyph_render_pixels），
   * 滚屏时不再做全屏脏行回放，避免超过一屏后每滚一行都重绘整屏字形。
   */
  return;
}

static void
fb_fill_rect_to(uchar *base, uint pitch, uint bpp,
                uint x0, uint y0, uint w, uint h,
                uchar r, uchar g, uchar b)
{
  uint yy, xx;

  if(base == 0 || w == 0 || h == 0)
    return;
  if(bpp == 32){
    uint pack = (uint)b | ((uint)g << 8) | ((uint)r << 16) | (0xFFu << 24);
    for(yy = 0; yy < h; yy++){
      uint *p32 = (uint*)(void*)(base + (y0 + yy) * pitch + x0 * 4);
      /*
       * 利用 x86 rep stosl 做批量填充（Sandy Bridge 真机优于逐像素循环）。
       * 这一段命中全屏清屏、滚屏清底行等热点路径。
       */
      stosl(p32, (int)pack, (int)w);
    }
  } else if(bpp == 24){
    for(yy = 0; yy < h; yy++){
      uchar *row = base + (y0 + yy) * pitch + x0 * 3;
      for(xx = 0; xx < w; xx++){
        row[xx * 3 + 0] = b;
        row[xx * 3 + 1] = g;
        row[xx * 3 + 2] = r;
      }
    }
  } else {
    ushort v = (ushort)(((ushort)(r >> 3) << 11) | ((ushort)(g >> 2) << 5) | (ushort)(b >> 3));
    for(yy = 0; yy < h; yy++){
      ushort *p16 = (ushort*)(void*)(base + (y0 + yy) * pitch + x0 * 2);
      for(xx = 0; xx < w; xx++)
        p16[xx] = v;
    }
  }
}

static void
fb_backbuf_flush(void)
{
  uint y;

  if(!fb_ok || !fb_backbuf_ok || fb_text_px_rows == 0)
    return;
  /*
   * 正文区逐扫描线 dword 拷贝到显存；在部分环境（含 QEMU）下大块单次 memmove
   * 反而不如按行 rep movsl 稳定，故保留按行路径。
   */
  for(y = 0; y < fb_text_px_rows; y++)
    fb_copy_scanline(fb_kva + y * fb_pitch, fb_backbuf + y * fb_pitch, fb_pitch);
}

static void
fb_fill_rect(uint x0, uint y0, uint w, uint h, uchar r, uchar g, uchar b)
{
  if(!fb_ok || w == 0 || h == 0)
    return;
  /* 按行批量写入（全屏清屏若逐像素则可达数十秒） */
  fb_fill_rect_to(fb_kva, fb_pitch, fb_bpp, x0, y0, w, h, r, g, b);

  if(fb_backbuf_ok){
    uint y1, y2;
    y1 = y0;
    y2 = y0 + h;
    if(y1 < fb_text_px_rows){
      if(y2 > fb_text_px_rows)
        y2 = fb_text_px_rows;
      fb_fill_rect_to(fb_backbuf, fb_pitch, fb_bpp, x0, y1, w, y2 - y1, r, g, b);
    }
  }
}

static void
fb_copy_scanline(uchar *dst, const uchar *src, uint n)
{
  fb_copy_dwords(dst, src, n);
}

/*
 * 仅用于非重叠、前向的像素拷贝路径（backbuf -> VRAM）。
 * 采用 rep movsl（4 字节步进）+ 尾字节，贴近 Linux 对老代 x86 的做法。
 */
static void
fb_copy_dwords(uchar *dst, const uchar *src, uint n)
{
  uint dwords, tail, i;

  if(n == 0)
    return;
  dwords = n >> 2;
  tail = n & 3u;
  if(dwords){
    asm volatile("cld; rep movsl"
                 : "+D"(dst), "+S"(src), "+c"(dwords)
                 :
                 : "memory", "cc");
  }
  for(i = 0; i < tail; i++)
    dst[i] = src[i];
}

static void
fb_scroll_cells(void)
{
  uint r;
  uint cols, rows;
  uint idx;

  if(!fb_ok || fb_rows < 3)
    return;
  cols = fb_cols;
  rows = fb_rows;
  for(r = 0; r < rows - 2; r++){
    idx = r * cols;
    if(idx + 2 * cols <= FB_CELL_MAX){
      memmove(&fb_cell_ch[idx], &fb_cell_ch[idx + cols], cols);
      memmove(&fb_cell_attr[idx], &fb_cell_attr[idx + cols], cols);
    } else {
      uint c;
      for(c = 0; c < cols; c++){
        uint i0, i1;
        i0 = idx + c;
        i1 = idx + cols + c;
        if(i0 < FB_CELL_MAX && i1 < FB_CELL_MAX){
          fb_cell_ch[i0] = fb_cell_ch[i1];
          fb_cell_attr[i0] = fb_cell_attr[i1];
        }
      }
    }
  }
  idx = (rows - 2) * cols;
  if(idx < FB_CELL_MAX){
    if(idx + cols <= FB_CELL_MAX){
      memset(&fb_cell_ch[idx], ' ', cols);
      memset(&fb_cell_attr[idx], 0x07, cols);
    } else {
      uint c;
      for(c = 0; c < cols; c++){
        uint i0 = idx + c;
        if(i0 < FB_CELL_MAX){
          fb_cell_ch[i0] = ' ';
          fb_cell_attr[i0] = 0x07;
        }
      }
    }
  }
}

static void
vga16_to_rgb(uchar idx, uchar *r, uchar *g, uchar *b)
{
  static const uchar pal[] = {
    0, 0, 0,       0, 0, 175,     0, 175, 0,     0, 175, 175,
    175, 0, 0,     175, 0, 175,   175, 175, 0,   175, 175, 175,
    85, 85, 85,    85, 85, 255,   85, 255, 85,   85, 255, 255,
    255, 85, 85,   255, 85, 255,  255, 255, 85,  255, 255, 255
  };

  idx &= 0x0F;
  *r = pal[idx * 3 + 0];
  *g = pal[idx * 3 + 1];
  *b = pal[idx * 3 + 2];
}

/*
 * 仅向显存写入字形像素（不修改影子格），供滚动整屏重绘与光标路径复用。
 * 真机上线性帧缓冲常为 WC/UC，读显存极慢；滚动时改为按影子重绘、仅写像素，可明显减轻卡顿。
 */
static void
fb_glyph_render_pixels_to(uchar *base, uint pitch, uint bpp,
                          uint col, uint row, uchar ch, uchar cga)
{
  uchar fgr, fgg, fgb, bgr, bgg, bgb;
  uint x0, y0;
  uint dx, dy;
  uchar mask;
  const uchar *gl;
  uint fg32, bg32;
  ushort fg16, bg16;

  if(base == 0 || col >= fb_cols || row >= fb_rows)
    return;
  if(ch > 127)
    ch = '?';
  gl = g_font8x16[ch];
  vga16_to_rgb(cga & 0x0F, &fgr, &fgg, &fgb);
  vga16_to_rgb((cga >> 4) & 0x0F, &bgr, &bgg, &bgb);
  x0 = col * CHAR_W;
  y0 = row * CHAR_H;

  if(bpp == 32){
    fg32 = (uint)fgb | ((uint)fgg << 8) | ((uint)fgr << 16) | (0xFFu << 24);
    bg32 = (uint)bgb | ((uint)bgg << 8) | ((uint)bgr << 16) | (0xFFu << 24);
    for(dy = 0; dy < CHAR_H; dy++){
      mask = gl[dy];
      uint *rowp = (uint*)(void*)(base + (y0 + dy) * pitch + x0 * 4);
      for(dx = 0; dx < CHAR_W; dx++){
        int bit = (mask >> (7 - dx)) & 1;
        rowp[dx] = bit ? fg32 : bg32;
      }
    }
  } else if(bpp == 16){
    fg16 = (ushort)(((ushort)(fgr >> 3) << 11) | ((ushort)(fgg >> 2) << 5) | (ushort)(fgb >> 3));
    bg16 = (ushort)(((ushort)(bgr >> 3) << 11) | ((ushort)(bgg >> 2) << 5) | (ushort)(bgb >> 3));
    for(dy = 0; dy < CHAR_H; dy++){
      mask = gl[dy];
      ushort *rowp = (ushort*)(void*)(base + (y0 + dy) * pitch + x0 * 2);
      for(dx = 0; dx < CHAR_W; dx++){
        int bit = (mask >> (7 - dx)) & 1;
        rowp[dx] = bit ? fg16 : bg16;
      }
    }
  } else {
    for(dy = 0; dy < CHAR_H; dy++){
      mask = gl[dy];
      uchar *rowp = base + (y0 + dy) * pitch + x0 * 3;
      for(dx = 0; dx < CHAR_W; dx++){
        int bit = (mask >> (7 - dx)) & 1;
        if(bit){
          rowp[dx * 3 + 0] = fgb;
          rowp[dx * 3 + 1] = fgg;
          rowp[dx * 3 + 2] = fgr;
        } else {
          rowp[dx * 3 + 0] = bgb;
          rowp[dx * 3 + 1] = bgg;
          rowp[dx * 3 + 2] = bgr;
        }
      }
    }
  }
}

/*
 * 仅将一格（CHAR_W×CHAR_H）从后备缓冲拷到显存；配合「先栅格化到后备缓冲」
 * 避免对每个字符做两次完整字形光栅化（滚屏与大量输出时的主要热点）。
 */
static void
fb_blit_cell_vram_from_backbuf(uint col, uint row)
{
  uint x0, y0, dy;
  uint bppb, rowbytes;
  uchar *src, *dst;

  if(!fb_ok || !fb_backbuf_ok || row >= fb_text_rows || col >= fb_cols)
    return;
  bppb = fb_bpp / 8;
  if(bppb == 0)
    return;
  x0 = col * CHAR_W;
  y0 = row * CHAR_H;

  if(fb_bpp == 32){
    for(dy = 0; dy < CHAR_H; dy++){
      uint *s, *d;
      s = (uint*)(void*)(fb_backbuf + (y0 + dy) * fb_pitch + x0 * 4);
      d = (uint*)(void*)(fb_kva + (y0 + dy) * fb_pitch + x0 * 4);
      d[0] = s[0];
      d[1] = s[1];
      d[2] = s[2];
      d[3] = s[3];
      d[4] = s[4];
      d[5] = s[5];
      d[6] = s[6];
      d[7] = s[7];
    }
    return;
  }
  if(fb_bpp == 16){
    for(dy = 0; dy < CHAR_H; dy++){
      ushort *s, *d;
      s = (ushort*)(void*)(fb_backbuf + (y0 + dy) * fb_pitch + x0 * 2);
      d = (ushort*)(void*)(fb_kva + (y0 + dy) * fb_pitch + x0 * 2);
      d[0] = s[0];
      d[1] = s[1];
      d[2] = s[2];
      d[3] = s[3];
      d[4] = s[4];
      d[5] = s[5];
      d[6] = s[6];
      d[7] = s[7];
    }
    return;
  }
  rowbytes = CHAR_W * bppb;
  for(dy = 0; dy < CHAR_H; dy++){
    src = fb_backbuf + (y0 + dy) * fb_pitch + x0 * bppb;
    dst = fb_kva + (y0 + dy) * fb_pitch + x0 * bppb;
    memmove(dst, src, rowbytes);
  }
}

static void
fb_glyph_render_pixels(uint col, uint row, uchar ch, uchar cga)
{
  if(!fb_ok)
    return;
  /*
   * 有后备缓冲时：只在 WB 的后备缓冲里栅格化，再小块拷贝到显存；
   * 旧路径对 fb_kva 与 backbuf 各栅格化一次，在 X220 上成本接近翻倍。
   */
  if(fb_backbuf_ok && row < fb_text_rows){
    fb_glyph_render_pixels_to(fb_backbuf, fb_pitch, fb_bpp, col, row, ch, cga);
    fb_blit_cell_vram_from_backbuf(col, row);
    return;
  }
  fb_glyph_render_pixels_to(fb_kva, fb_pitch, fb_bpp, col, row, ch, cga);
}

void
fb_scroll_content(void)
{
  uint r, c, idx;

  if(!fb_ok || fb_rows < 2)
    return;

  /*
   * 主路径：后备缓冲滚屏
   * - 影子字符格上移一行；
   * - RAM 后备缓冲 memmove 一行像素并清底行；
   * - 一次性 blit 回显存。
   * 该路径保持可见行顺序正确，不会出现提示符跑到屏幕中间的问题。
   */
  if(fb_backbuf_ok && fb_text_px_rows >= CHAR_H){
    uint moved = fb_backbuf_bytes - CHAR_H * fb_pitch;
    fb_sync_dirty_rows_to_backbuf();
    fb_scroll_cells();
    if(moved > 0)
      memmove(fb_backbuf, fb_backbuf + CHAR_H * fb_pitch, moved);
    fb_fill_rect_to(fb_backbuf, fb_pitch, fb_bpp, 0,
                    fb_text_px_rows - CHAR_H, fb_width, CHAR_H, 0, 0, 0);
    fb_backbuf_flush();
    return;
  }

  /*
   * 兜底：无后备缓冲时按影子逐字符重绘正文区（含新空行）。
   */
  if(fb_rows >= 3){
    fb_scroll_cells();
    for(r = 0; r + 1 < fb_rows; r++)
      for(c = 0; c < fb_cols; c++){
        idx = r * fb_cols + c;
        if(idx >= FB_CELL_MAX)
          continue;
        fb_glyph_render_pixels(c, r, fb_cell_ch[idx], fb_cell_attr[idx]);
      }
    return;
  }

  /* 仅两行字符高（极少见）：仍用整块上移 */
  {
    uint y;
    uint limy;
    uchar *base;

    limy = (fb_rows - 1) * CHAR_H;
    base = fb_kva;
    for(y = 0; y + CHAR_H < limy; y++)
      fb_copy_scanline(base + y * fb_pitch, base + (y + CHAR_H) * fb_pitch, fb_pitch);
    fb_fill_rect(0, limy - CHAR_H, fb_width, CHAR_H, 0, 0, 0);
    fb_scroll_cells();
  }
}

void
fb_clear_content(void)
{
  uint rows;
  uint r, c, idx;

  if(!fb_ok)
    return;
  rows = fb_rows >= 2 ? fb_rows - 1 : fb_rows;
  fb_fill_rect_to(fb_kva, fb_pitch, fb_bpp, 0, 0, fb_width, rows * CHAR_H, 0, 0, 0);
  if(fb_backbuf_ok)
    fb_fill_rect_to(fb_backbuf, fb_pitch, fb_bpp, 0, 0, fb_width, rows * CHAR_H, 0, 0, 0);
  memset(fb_row_dirty, 0, sizeof(fb_row_dirty));
  for(r = 0; r + 1 < fb_rows; r++)
    for(c = 0; c < fb_cols; c++){
      idx = r * fb_cols + c;
      if(idx < FB_CELL_MAX){
        fb_cell_ch[idx] = ' ';
        fb_cell_attr[idx] = 0x07;
      }
    }
}

void
fb_gui_mode_set(int on)
{
  fb_gui_mode = on ? 1 : 0;
}

int
fb_gui_mode_get(void)
{
  return fb_gui_mode;
}

void
fb_draw_glyph_vga(uint col, uint row, uchar ch, uchar cga)
{
  uint idx;

  if(!fb_ok)
    return;
  if(fb_gui_mode)
    return;
  if(col < fb_cols && row < fb_rows){
    idx = row * fb_cols + col;
    if(idx < FB_CELL_MAX){
      fb_cell_ch[idx] = ch;
      fb_cell_attr[idx] = cga;
    }
  }
  fb_glyph_render_pixels(col, row, ch, cga);
}

/*
 * lit=0：仅重画影子中的字符（不画下划线，相当于光标“灭”）
 * lit=1：先按影子恢复字形，再在格底画三条前景色扫描线（下划线闪烁）
 */
void
fb_paint_cursor(uint col, uint row, int lit, uchar cga)
{
  uchar fgr, fgg, fgb;
  uchar ch, at;
  uint x0, y0;
  uint dx, dy;
  uint idx;
  uint fg32;
  ushort fg16;

  (void)cga;

  if(!fb_ok || fb_gui_mode)
    return;
  if(col >= fb_cols || row >= fb_rows)
    return;
  idx = row * fb_cols + col;
  if(idx >= FB_CELL_MAX)
    return;
  ch = fb_cell_ch[idx];
  at = fb_cell_attr[idx];
  fb_glyph_render_pixels(col, row, ch, at);
  if(!lit)
    return;
  vga16_to_rgb(at & 0x0F, &fgr, &fgg, &fgb);
  x0 = col * CHAR_W;
  y0 = row * CHAR_H;
  if(fb_bpp == 32){
    fg32 = (uint)fgb | ((uint)fgg << 8) | ((uint)fgr << 16) | (0xFFu << 24);
    for(dy = CHAR_H - 3; dy < CHAR_H; dy++){
      uint *rowp = (uint*)(void*)(fb_kva + (y0 + dy) * fb_pitch + x0 * 4);
      for(dx = 0; dx < CHAR_W; dx++)
        rowp[dx] = fg32;
    }
  } else if(fb_bpp == 16){
    fg16 = (ushort)(((ushort)(fgr >> 3) << 11) | ((ushort)(fgg >> 2) << 5) | (ushort)(fgb >> 3));
    for(dy = CHAR_H - 3; dy < CHAR_H; dy++){
      ushort *rowp = (ushort*)(void*)(fb_kva + (y0 + dy) * fb_pitch + x0 * 2);
      for(dx = 0; dx < CHAR_W; dx++)
        rowp[dx] = fg16;
    }
  } else {
    for(dy = CHAR_H - 3; dy < CHAR_H; dy++){
      uchar *rowp = fb_kva + (y0 + dy) * fb_pitch + x0 * 3;
      for(dx = 0; dx < CHAR_W; dx++){
        rowp[dx * 3 + 0] = fgb;
        rowp[dx * 3 + 1] = fgg;
        rowp[dx * 3 + 2] = fgr;
      }
    }
  }
}

uint
fb_cols_get(void)
{
  return fb_cols;
}

uint
fb_rows_get(void)
{
  return fb_rows;
}

uint
fb_panic_row(void)
{
  if(!fb_ok || fb_rows == 0)
    return 0;
  return fb_rows - 1;
}

void
fb_panic_draw(int cpu, const char *s)
{
  uint pr;
  uint i;
  uchar dig;
  /* 亮白前景（15）红底（4） */
  uchar panic_attr;

  if(!fb_ok)
    return;
  pr = fb_panic_row();
  panic_attr = (4 << 4) | 0x0F;
  fb_fill_rect(0, pr * CHAR_H, fb_width, CHAR_H, 175, 0, 0);
  fb_draw_glyph_vga(0, pr, 'P', panic_attr);
  fb_draw_glyph_vga(1, pr, 'A', panic_attr);
  fb_draw_glyph_vga(2, pr, 'N', panic_attr);
  fb_draw_glyph_vga(3, pr, 'I', panic_attr);
  fb_draw_glyph_vga(4, pr, 'C', panic_attr);
  fb_draw_glyph_vga(5, pr, ' ', panic_attr);
  fb_draw_glyph_vga(6, pr, 'c', panic_attr);
  fb_draw_glyph_vga(7, pr, 'p', panic_attr);
  fb_draw_glyph_vga(8, pr, 'u', panic_attr);
  dig = (uchar)('0' + (cpu % 10));
  fb_draw_glyph_vga(9, pr, dig, panic_attr);
  fb_draw_glyph_vga(10, pr, ':', panic_attr);
  for(i = 0; s[i] && i < 60; i++)
    fb_draw_glyph_vga(12 + i, pr, (uchar)s[i], panic_attr);
}

void
fb_init(void)
{
  volatile uchar *mode;
  volatile uchar *st;
  ushort attrs;
  uint phys;
  ushort pitch;
  ushort w, h;
  uchar bpp;
  uint nbytes;
  uint i;

  fb_ok = 0;
  fb_backbuf_ok = 0;
  fb_backbuf_bytes = 0;
  fb_text_rows = 0;
  fb_text_px_rows = 0;
  mode = (volatile uchar*)P2V(0x9000);
  st = (volatile uchar*)P2V(0x8FF0);
  if(*st != 1)
    return;

  attrs = *(volatile ushort*)(mode + 0x00);
  phys = *(volatile uint*)(mode + 0x28);
  pitch = *(volatile ushort*)(mode + 0x10);
  w = *(volatile ushort*)(mode + 0x12);
  h = *(volatile ushort*)(mode + 0x14);
  bpp = *(volatile uchar*)(mode + 0x19);

  if(!(attrs & 0x80))
    return;
  if(phys == 0 || pitch == 0 || w == 0 || h == 0)
    return;
  if(bpp != 16 && bpp != 24 && bpp != 32)
    return;

  nbytes = pitch * h;
  for(i = 0; i < nbytes; i += PGSIZE){
    if(acpi_map_phys(phys + i) == 0)
      return;
  }

  fb_kva = (uchar*)acpi_map_phys(phys);
  if(fb_kva == 0)
    return;

  fb_pitch = pitch;
  fb_width = w;
  fb_height = h;
  fb_bpp = bpp;
  fb_cols = w / CHAR_W;
  fb_rows = h / CHAR_H;
  fb_text_rows = fb_rows >= 2 ? fb_rows - 1 : fb_rows;
  fb_text_px_rows = fb_text_rows * CHAR_H;
  fb_backbuf_bytes = fb_pitch * fb_text_px_rows;
  fb_backbuf_ok = (fb_backbuf_bytes > 0 && fb_backbuf_bytes <= FB_BACKBUF_MAX);
  if(fb_cols == 0 || fb_rows == 0)
    return;

  fb_ok = 1;
  fb_fill_rect(0, 0, w, h, 0, 0, 0);
  if(fb_backbuf_ok)
    fb_fill_rect_to(fb_backbuf, fb_pitch, fb_bpp, 0, 0, w, fb_text_px_rows, 0, 0, 0);
  memset(fb_row_dirty, 0, sizeof(fb_row_dirty));
  for(i = 0; i < fb_cols * fb_rows && i < FB_CELL_MAX; i++){
    fb_cell_ch[i] = ' ';
    fb_cell_attr[i] = 0x07;
  }
}

int
fb_is_active(void)
{
  return fb_ok;
}

void
fb_idle_poll(void)
{
  /* 闪烁由定时器中断里的 console_cursor_tick 驱动 */
}
