// Console input and output.
// Input is from the keyboard or serial port.
// Output is written to the screen and serial port.

#include "types.h"
#include "defs.h"
#include "param.h"
#include "traps.h"
#include "spinlock.h"
#include "fs.h"
#include "file.h"
#include "memlayout.h"
#include "mmu.h"
#include "proc.h"
#include "x86.h"
#include "fb_console.h"

extern uint ticks;

static void consputc(int);

/* 帧缓冲就绪前仅串口可见；将 uart 字节流记入缓冲，fb_init 后重放到屏上 */
#define EARLY_CONS_LOG_SIZE 65536
static char early_cons_log[EARLY_CONS_LOG_SIZE];
static uint early_cons_len;

void
early_uart_mirror(int c)
{
  if(fb_is_active())
    return;
  if(early_cons_len >= EARLY_CONS_LOG_SIZE)
    return;
  early_cons_log[early_cons_len++] = (char)c;
}

/* 约每 HZ/2 拍切换亮/灭（100Hz 下约 0.5s 半周期） */
static int
fb_cursor_lit(void)
{
  uint per;

  per = HZ / 2;
  if(per == 0)
    per = 1;
  return (int)((ticks / per) & 1u);
}

static volatile int panicked = 0;
static int input_echo = 1;
static int input_raw = 0;
/*
 * 原始输入（902h）下：退格字节仍送入用户态，但屏幕擦除须与「尚有可删字符」一致。
 * 每回显一可打印字符 +1；成功回显退格 −1；换行/^U/控制台复位时清零。
 * emit_tab_spaces 通过 CSI 907;n h 为补全空格同步增加预算。
 */
static int input_echoq;

static struct {
  struct spinlock lock;
  int locking;
} cons;

static void
printint(int xx, int base, int sign)
{
  static char digits[] = "0123456789abcdef";
  char buf[16];
  int i;
  uint x;

  if(sign && (sign = xx < 0))
    x = -xx;
  else
    x = xx;

  i = 0;
  do{
    buf[i++] = digits[x % base];
  }while((x /= base) != 0);

  if(sign)
    buf[i++] = '-';

  while(--i >= 0)
    consputc(buf[i]);
}

static void
uartprintint(int xx, int base, int sign)
{
  static char digits[] = "0123456789abcdef";
  char buf[16];
  int i;
  uint x;

  if(sign && (sign = xx < 0))
    x = -xx;
  else
    x = xx;

  i = 0;
  do{
    buf[i++] = digits[x % base];
  }while((x /= base) != 0);

  if(sign)
    buf[i++] = '-';

  while(--i >= 0)
    uartputc(buf[i]);
}
//PAGEBREAK: 50

// Print to the console. only understands %c, %d, %x, %p, %s.
void
cprintf(char *fmt, ...)
{
  int i, c, locking;
  uint *argp;
  char *s;

  locking = cons.locking;
  if(locking)
    acquire(&cons.lock);

  if (fmt == 0)
    panic("null fmt");

  argp = (uint*)(void*)(&fmt + 1);
  for(i = 0; (c = fmt[i] & 0xff) != 0; i++){
    if(c != '%'){
      consputc(c);
      continue;
    }
    c = fmt[++i] & 0xff;
    if(c == 0)
      break;
    switch(c){
    case 'c':
      consputc((char)(*argp++ & 0xff));
      break;
    case 'd':
      printint(*argp++, 10, 1);
      break;
    case 'x':
    case 'p':
      printint(*argp++, 16, 0);
      break;
    case 's':
      if((s = (char*)*argp++) == 0)
        s = "(null)";
      for(; *s; s++)
        consputc(*s);
      break;
    case '%':
      consputc('%');
      break;
    default:
      // Print unknown % sequence to draw attention.
      consputc('%');
      consputc(c);
      break;
    }
  }

  if(locking)
    release(&cons.lock);
}

// 仅输出到串口，不写屏幕（用于保留调试串口日志、避免污染显示界面）。
void
uartcprintf(char *fmt, ...)
{
  int i, c, locking;
  uint *argp;
  char *s;

  locking = cons.locking;
  if(locking)
    acquire(&cons.lock);

  if(fmt == 0)
    panic("null fmt");

  argp = (uint*)(void*)(&fmt + 1);
  for(i = 0; (c = fmt[i] & 0xff) != 0; i++){
    if(c != '%'){
      uartputc(c);
      continue;
    }
    c = fmt[++i] & 0xff;
    if(c == 0)
      break;
    switch(c){
    case 'c':
      uartputc((char)(*argp++ & 0xff));
      break;
    case 'd':
      uartprintint(*argp++, 10, 1);
      break;
    case 'x':
    case 'p':
      uartprintint(*argp++, 16, 0);
      break;
    case 's':
      if((s = (char*)*argp++) == 0)
        s = "(null)";
      for(; *s; s++)
        uartputc(*s);
      break;
    case '%':
      uartputc('%');
      break;
    default:
      uartputc('%');
      uartputc(c);
      break;
    }
  }

  if(locking)
    release(&cons.lock);
}

/* 启动阶段行尾状态：绿色成功 / 红色失败（直至 shell 欢迎框前） */
void
boot_ok(void)
{
  cprintf(" \033[32m[OK]\033[0m\n");
}

void
boot_fail(void)
{
  cprintf(" \033[31m[FAILED]\033[0m\n");
}

void
panic(char *s)
{
  cli();

  if(fb_is_active())
    fb_panic_draw(cpu->id, s);

  // Also print via cprintf - safe because we removed panicked check
  // from consputc, so this won't freeze other CPUs.
  cprintf("PANIC cpu%d: %s\n", cpu->id, s);

  // DO NOT set panicked=1 - that would freeze ALL CPUs via consputc.
  // Just halt THIS CPU. Other CPUs continue normally.
  for(;;)
    ;
}

//PAGEBREAK: 50
#define BACKSPACE 0x100

// Current CGA 风格属性（前景 | 背景<<4），用于帧缓冲调色
// 默认：浅灰(7) 黑底(0) = 0x07
static uchar cga_color = 0x07;

// ANSI escape sequence parser state
// 0 = normal, 1 = saw ESC, 2 = saw ESC[
static int esc_state = 0;
#define CSI_MAX 4
static int esc_params[CSI_MAX];
static int esc_pc;
static int esc_bold;

// Map ANSI color (0-7) to VGA color (0-7)
static uchar ansi_to_vga[] = { 0, 4, 2, 6, 1, 5, 3, 7 };

/* 软件光标（字符格索引），仅帧缓冲模式使用 */
static int fb_pos;

static void
fb_clear_visible(void)
{
  if(fb_is_active()){
    fb_clear_content();
    fb_pos = 0;
    fb_paint_cursor(0, 0, fb_cursor_lit(), cga_color);
  }
}

static void
cgaputc(int c)
{
  int pos;
  int cols;
  int maxrow;

  /*
   * 全屏图形桌面（gui）时禁止向帧缓冲写文本/解析会改屏的转义，
   * 否则定时器里的光标闪烁仍会画进显存，破坏桌面。
   * 串口仍由 consputc 中的 uartputc 先行输出，故回归魔数等仍可捕获。
   */
  if(fb_is_active() && fb_gui_mode_get()){
    esc_state = 0;
    return;
  }

  if(esc_state == 1){
    if(c == '['){
      int k;
      esc_state = 2;
      esc_pc = 0;
      esc_bold = 0;
      for(k = 0; k < CSI_MAX; k++)
        esc_params[k] = 0;
      return;
    }
    esc_state = 0;
  } else if(esc_state == 2){
    if(c >= '0' && c <= '9'){
      esc_params[esc_pc] = esc_params[esc_pc] * 10 + (c - '0');
      return;
    }
    if(c == ';'){
      if(esc_pc < CSI_MAX - 1)
        esc_pc++;
      return;
    }
    if(c == 'm'){
      int i;
      for(i = 0; i <= esc_pc; i++){
        int p = esc_params[i];
        if(p == 0){
          cga_color = 0x07;
          esc_bold = 0;
        } else if(p == 1){
          esc_bold = 1;
          cga_color = (cga_color & 0xF0) | ((cga_color & 0x0F) | 0x08);
        } else if(p >= 30 && p <= 37){
          uchar vga_fg = ansi_to_vga[p - 30];
          if(esc_bold)
            vga_fg |= 0x08;
          cga_color = (cga_color & 0xF0) | vga_fg;
        } else if(p >= 40 && p <= 47){
          uchar vga_bg = ansi_to_vga[p - 40];
          cga_color = (cga_color & 0x0F) | (vga_bg << 4);
        }
      }
      esc_state = 0;
      return;
    }
    if(c == 'J'){
      if(esc_params[0] == 2)
        fb_clear_visible();
      esc_state = 0;
      return;
    }
    if(c == 'H'){
      int row, col, npos;
      if(!fb_is_active()){
        esc_state = 0;
        return;
      }
      cols = fb_cols_get();
      maxrow = fb_rows_get() - 1;
      if(maxrow < 1)
        maxrow = 1;
      if(esc_pc == 0 && esc_params[0] == 0){
        npos = 0;
      } else {
        row = esc_params[0] ? esc_params[0] : 1;
        col = esc_params[1] ? esc_params[1] : 1;
        if(row < 1)
          row = 1;
        if(row > maxrow)
          row = maxrow;
        if(col < 1)
          col = 1;
        if(col > (int)cols)
          col = cols;
        npos = (row - 1) * cols + (col - 1);
      }
      fb_pos = npos;
      fb_paint_cursor(fb_pos % cols, fb_pos / cols, fb_cursor_lit(), cga_color);
      esc_state = 0;
      return;
    }
    if(c == 'h' || c == 'l'){
      if(esc_params[0] == 901){
        input_echo = (c == 'h') ? 1 : 0;
      } else if(esc_params[0] == 902){
        input_raw = (c == 'h') ? 1 : 0;
      } else if(esc_params[0] == 907 && c == 'h'){
        int n = esc_params[1];
        if(n > 0 && n < 512){
          input_echoq += n;
          if(input_echoq > 1024)
            input_echoq = 1024;
        }
      } else if(esc_params[0] == 908 && c == 'h'){
        int n = esc_params[1];
        if(n >= 0 && n <= 1024)
          input_echoq = n;
      }
      esc_state = 0;
      return;
    }
    esc_state = 0;
    return;
  }

  if(c == 0x1B){
    esc_state = 1;
    return;
  }

  if(!fb_is_active())
    return;

  cols = fb_cols_get();
  maxrow = fb_rows_get() - 1;
  pos = fb_pos;

  if(c == '\n'){
    int old;
    /*
     * 换行前光标停在行尾空位（下划线闪烁）；若不清除，下一行重画光标后，
     * 上一行尾格仍留亮条，在表头等输出末尾会像多出「下划线/下划字符」。
     */
    old = pos;
    pos += cols - pos % cols;
    fb_draw_glyph_vga(old % cols, old / cols, ' ', cga_color);
  } else if(c == BACKSPACE){
    if(pos > 0){
      int prev;
      /*
       * 退格前光标停在一格「空位」上（下划线闪烁）；pos-- 后只擦了被删字符格，
       * 原光标格未重画，会留下多条下划线残影。须同时清除原光标格。
       */
      prev = pos;
      pos--;
      fb_draw_glyph_vga(pos % cols, pos / cols, ' ', cga_color);
      fb_draw_glyph_vga(prev % cols, prev / cols, ' ', cga_color);
    }
  } else {
    fb_draw_glyph_vga(pos % cols, pos / cols, (uchar)(c & 0xff), cga_color);
    pos++;
  }

  if((pos / cols) >= maxrow){
    fb_scroll_content();
    pos -= cols;
  }

  fb_pos = pos;
  fb_paint_cursor(pos % cols, pos / cols, fb_cursor_lit(), cga_color);
}

void
console_cursor_tick(void)
{
  int cols;
  int pos;
  uint per;

  if(!fb_is_active())
    return;
  per = HZ / 2;
  if(per == 0)
    per = 1;
  if(ticks == 0 || (ticks % per) != 0)
    return;
  cols = fb_cols_get();
  pos = fb_pos;
  fb_paint_cursor(pos % cols, pos / cols, fb_cursor_lit(), cga_color);
}

void
consputc(int c)
{
  // CRITICAL FIX for ThinkPad X220:
  // The panicked check was REMOVED entirely. On real hardware, the
  // panicked variable can become non-zero through:
  //   1. An AP calling panic() from unexpected hardware events
  //   2. Memory corruption from EHCI DMA or wild pointer writes
  //   3. BIOS-leftover thermal/NMI interrupts on APs
  // When panicked != 0, ALL CPUs would freeze in consputc's infinite
  // loop (cli; for(;;)), making the system completely unresponsive.
  // Removing this check allows the BSP and other healthy CPUs to
  // continue operating even if one AP encounters a fatal error.
  // The panicking CPU is halted in panic() via cli(); for(;;).

  if(c == BACKSPACE){
    uartputc('\b'); uartputc(' '); uartputc('\b');
  } else if(c == '\t'){
    /*
     * 按 8 列制表位展开为空格；帧缓冲与串口一致。
     */
    if(fb_is_active()){
      int col = fb_pos % fb_cols_get();
      int nspaces = (col % 8) ? (8 - (col % 8)) : 8;
      int j;
      for(j = 0; j < nspaces; j++){
        uartputc(' ');
        cgaputc(' ');
      }
    } else {
      int j;
      for(j = 0; j < 8; j++)
        uartputc(' ');
    }
    return;
  } else
    uartputc(c);
  cgaputc(c);
}

#define INPUT_BUF 128
struct {
  struct spinlock lock;
  char buf[INPUT_BUF];
  uint r;  // Read index
  uint w;  // Write index
  uint e;  // Edit index
} input;

#define C(x)  ((x)-'@')  // Control-x

/*
 * 前台进程被 ^C 杀掉（如 more/vi 关回显、开原始输入）时，用户态来不及执行恢复序列；
 * 须把控制台全局状态拉回行编辑默认，避免 shell 提示符下看不见输入。
 */
static void
console_reset_tty(void)
{
  input_echo = 1;
  input_raw = 0;
  input_echoq = 0;
  esc_state = 0;
  esc_pc = 0;
  esc_bold = 0;
  cga_color = 0x07;
}

void
consoleintr(int (*getc)(void))
{
  int c;

  acquire(&input.lock);
  while((c = getc()) >= 0){
    switch(c){
    case C('P'):  // Process listing.
      procdump();
      break;
    case C('U'):  // Kill line.
      while(input.e != input.w &&
            input.buf[(input.e-1) % INPUT_BUF] != '\n'){
        input.e--;
        consputc(BACKSPACE);
      }
      input_echoq = 0;
      break;
    case C('H'): case '\x7f':  // Backspace
      /*
       * 行模式：从编辑缓冲删一字节并回显退格。
       * 原始模式（902h）：必须把退格交给读者（如 vi），否则 read 永远收不到。
       * 原始模式下若一律回显退格，空行时仍会左移帧缓冲光标，擦掉提示符；故仅在有
       * 可删字符（echoq 或尚有未读入用户态的字节）时做屏幕擦除。
       */
      if(input_raw){
        if(input.e - input.r < INPUT_BUF){
          int ch = (c == '\x7f') ? '\x7f' : '\b';
          if(input_echo){
            if(input_echoq > 0){
              input_echoq--;
              consputc(BACKSPACE);
            } else if(input.e > input.r){
              consputc(BACKSPACE);
            }
          }
          input.buf[input.e++ % INPUT_BUF] = ch;
          input.w = input.e;
          wakeup(&input.r);
        }
      } else if(input.e != input.w){
        input.e--;
        consputc(BACKSPACE);
      }
      break;
    case C('C'):  // Kill current foreground process and return to prompt.
      killfgproc();
      console_reset_tty();
      // Discard current input line and wake readers with a newline.
      while(input.e != input.w &&
            input.buf[(input.e-1) % INPUT_BUF] != '\n'){
        input.e--;
      }
      consputc('\n');
      if(input.e - input.r < INPUT_BUF){
        input.buf[input.e++ % INPUT_BUF] = '\n';
        input.w = input.e;
        wakeup(&input.r);
      }
      break;
    default:
      if(c != 0 && input.e-input.r < INPUT_BUF){
        c = (c == '\r') ? '\n' : c;
        input.buf[input.e++ % INPUT_BUF] = c;
        if(input_echo){
          /* 902h 下行编辑：Tab 不在内核展开为空格，由 shell 统一画补全或制表位，避免 ki+Tab+ll 出现间隙 */
          if(input_raw && c == '\t')
            ;
          else
            consputc(c);
        }
        if(input_raw && input_echo && c >= 0x20 && c <= 0x7e){
          input_echoq++;
          if(input_echoq > 1024)
            input_echoq = 1024;
        }
        if(input_raw && c == '\n')
          input_echoq = 0;
        if(input_raw || c == '\n' || c == C('D') || input.e == input.r+INPUT_BUF){
          input.w = input.e;
          wakeup(&input.r);
        }
      }
      break;
    }
  }
  release(&input.lock);
}

/*
 * 控制台是否已有可读字节（input.r != input.w）。
 * 供 filefdready 使用：若恒返回「可读」，则 telnet 等会在无输入时仍进入 read(0) 阻塞，
 * 无法在同一进程内轮询套接字，导致对端回显仅在下次键入后才出现。
 */
int
consolecanread(void)
{
  int r;

  acquire(&input.lock);
  r = (input.r != input.w);
  release(&input.lock);
  return r;
}

int
consoleread(struct inode *ip, char *dst, int n)
{
  uint target;
  int c;

  iunlock(ip);
  target = n;
  acquire(&input.lock);
  while(n > 0){
    while(input.r == input.w){
      if(proc->killed){
        release(&input.lock);
        ilock(ip);
        return -1;
      }
      fb_idle_poll();
      sleep(&input.r, &input.lock);
    }
    c = input.buf[input.r++ % INPUT_BUF];
    if(c == C('D')){  // EOF
      if(n < target){
        // Save ^D for next time, to make sure
        // caller gets a 0-byte result.
        input.r--;
      }
      break;
    }
    *dst++ = c;
    --n;
    if(c == '\n')
      break;
  }
  release(&input.lock);
  ilock(ip);

  return target - n;
}

int
consolewrite(struct inode *ip, char *buf, int n)
{
  int i;
  int sh_dbg_only_uart;

  iunlock(ip);
  acquire(&cons.lock);
  sh_dbg_only_uart = 0;
  if(n >= 8 && memcmp(buf, "[sh dbg]", 8) == 0)
    sh_dbg_only_uart = 1;
  if(sh_dbg_only_uart){
    for(i = 0; i < n; i++)
      uartputc(buf[i] & 0xff);
  } else {
    for(i = 0; i < n; i++)
      consputc(buf[i] & 0xff);
  }
  release(&cons.lock);
  ilock(ip);

  return n;
}

void
consoleinit(void)
{
  volatile int w;
  uchar config;

  initlock(&cons.lock, "console");
  initlock(&input.lock, "input");

  devsw[CONSOLE].write = consolewrite;
  devsw[CONSOLE].read = consoleread;
  cons.locking = 1;

  // 帧缓冲 fb_init 在 main 中 kinit2 之后调用（需扩展 kmem 池后再映射显存）

  // ---------------------------------------------------------------
  // 8042 controller initialization (matching reference keyboard_init)
  //
  // CRITICAL for X220: disable the mouse port (0xA7) so that the
  // Synaptics touchpad and IBM TrackPoint stop flooding the 8042
  // output buffer with PS/2 data. Without this, the Lenovo EC may
  // refuse to process the 0xFE reset command during reboot.
  // ---------------------------------------------------------------

  // Wait for input buffer empty, then disable keyboard port
  for(w = 0; w < 65536; w++){ if(!(inb(0x64) & 0x02)) break; }
  outb(0x64, 0xAD);

  // Disable mouse port — stops touchpad/trackpoint data
  for(w = 0; w < 65536; w++){ if(!(inb(0x64) & 0x02)) break; }
  outb(0x64, 0xA7);

  // Drain output buffer
  while(inb(0x64) & 0x01)
    (void)inb(0x60);

  // Read controller config byte
  for(w = 0; w < 65536; w++){ if(!(inb(0x64) & 0x02)) break; }
  outb(0x64, 0x20);
  for(w = 0; w < 65536; w++){ if(inb(0x64) & 0x01) break; }
  config = inb(0x60);

  // Enable keyboard IRQ (bit 0), disable mouse IRQ (bit 1 clear),
  // enable scan code translation (bit 6)
  config |= 0x01;
  config &= ~0x02;
  config |= 0x40;

  for(w = 0; w < 65536; w++){ if(!(inb(0x64) & 0x02)) break; }
  outb(0x64, 0x60);
  for(w = 0; w < 65536; w++){ if(!(inb(0x64) & 0x02)) break; }
  outb(0x60, config);

  // Re-enable keyboard port (mouse port stays DISABLED)
  for(w = 0; w < 65536; w++){ if(!(inb(0x64) & 0x02)) break; }
  outb(0x64, 0xAE);

  picenable(IRQ_KBD);
  ioapicenable(IRQ_KBD, 0);
}

void
console_fbinit(void)
{
  uint i;

  fb_init();
  fb_pos = 0;
  for(i = 0; i < early_cons_len; i++)
    cgaputc((uchar)early_cons_log[i]);
  if(fb_is_active())
    fb_paint_cursor(0, 0, fb_cursor_lit(), cga_color);
}

