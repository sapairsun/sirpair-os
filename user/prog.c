#include "types.h"
#include "user.h"

#define W 1024
#define H 768
#define CELLS 30
#define TICKS_PER_SEC 100

static uchar frame[W * H];

static uchar
rgb332(int r, int g, int b)
{
  return (uchar)((r & 0xE0) | ((g & 0xE0) >> 3) | ((b & 0xC0) >> 6));
}

static void
fill_rect(int x0, int y0, int w, int h, uchar c)
{
  int x, y;
  for(y = y0; y < y0 + h; y++){
    if(y < 0 || y >= H)
      continue;
    for(x = x0; x < x0 + w; x++){
      if(x < 0 || x >= W)
        continue;
      frame[y * W + x] = c;
    }
  }
}

static void
fill_triangle(int x0, int y0, int x1, int y1, int x2, int y2, uchar c)
{
  int minx = x0, maxx = x0, miny = y0, maxy = y0;
  int x, y;
  if(x1 < minx) minx = x1;
  if(x2 < minx) minx = x2;
  if(x1 > maxx) maxx = x1;
  if(x2 > maxx) maxx = x2;
  if(y1 < miny) miny = y1;
  if(y2 < miny) miny = y2;
  if(y1 > maxy) maxy = y1;
  if(y2 > maxy) maxy = y2;

  if(minx < 0) minx = 0;
  if(miny < 0) miny = 0;
  if(maxx >= W) maxx = W - 1;
  if(maxy >= H) maxy = H - 1;

  for(y = miny; y <= maxy; y++){
    for(x = minx; x <= maxx; x++){
      int e0 = (x - x0) * (y1 - y0) - (y - y0) * (x1 - x0);
      int e1 = (x - x1) * (y2 - y1) - (y - y1) * (x2 - x1);
      int e2 = (x - x2) * (y0 - y2) - (y - y2) * (x0 - x2);
      int has_neg = (e0 < 0) || (e1 < 0) || (e2 < 0);
      int has_pos = (e0 > 0) || (e1 > 0) || (e2 > 0);
      if(!(has_neg && has_pos))
        frame[y * W + x] = c;
    }
  }
}

static int
isqrt_i(int v)
{
  int x = v;
  int y;
  if(v <= 0)
    return 0;
  y = (x + 1) / 2;
  while(y < x){
    x = y;
    y = (x + v / x) / 2;
  }
  return x;
}

static void
draw_star(int cx, int cy, int outer_r, int inner_r, int cos1024, int sin1024, uchar c)
{
  static const int cos1000[10] = {0, 588, 951, 951, 588, 0, -588, -951, -951, -588};
  static const int sin1000[10] = {-1000, -809, -309, 309, 809, 1000, 809, 309, -309, -809};
  int px[10], py[10];
  int i;

  for(i = 0; i < 10; i++){
    int r = (i % 2 == 0) ? outer_r : inner_r;
    int bx = (cos1000[i] * r) / 1000;
    int by = (sin1000[i] * r) / 1000;
    int rx = (bx * cos1024 - by * sin1024) / 1024;
    int ry = (bx * sin1024 + by * cos1024) / 1024;
    px[i] = cx + rx;
    py[i] = cy + ry;
  }

  for(i = 0; i < 10; i++){
    int j = (i + 1) % 10;
    fill_triangle(cx, cy, px[i], py[i], px[j], py[j], c);
  }
}

static void
draw_flag(void)
{
  int fw = 240;
  int fh = 160;
  int fx = (W - fw) / 2;
  int fy = H - 280;
  int unit = fh / 20;
  int bx = fx + 5 * unit;
  int by = fy + 5 * unit;
  int sx[4] = {10, 12, 12, 10};
  int sy[4] = {2, 4, 7, 9};
  int i;
  uchar red = rgb332(222, 41, 16);
  uchar yellow = rgb332(255, 222, 0);

  fill_rect(fx, fy, fw, fh, red);
  draw_star(bx, by, 3 * unit, (3 * unit * 38) / 100, 1024, 0, yellow);

  for(i = 0; i < 4; i++){
    int cx = fx + sx[i] * unit;
    int cy = fy + sy[i] * unit;
    int dx = bx - cx;
    int dy = by - cy;
    int len = isqrt_i(dx * dx + dy * dy);
    int cos1024, sin1024;
    if(len == 0){
      cos1024 = 1024;
      sin1024 = 0;
    } else {
      cos1024 = (-dy * 1024) / len;
      sin1024 = (dx * 1024) / len;
    }
    draw_star(cx, cy, unit, (unit * 38) / 100, cos1024, sin1024, yellow);
  }
}

static void
draw_background(void)
{
  int i;
  uchar deep_blue = rgb332(10, 20, 120);
  for(i = 0; i < W * H; i++)
    frame[i] = deep_blue;
}

static void
draw_progress(int filled)
{
  int barw = (W * 3) / 4;
  int barh = 28;
  int x0 = (W - barw) / 2;
  int y0 = H - 70;
  int cellw = barw / CELLS;
  int i, x, y;
  uchar border = rgb332(255, 255, 255);
  uchar offc = rgb332(230, 30, 30);
  uchar onc = rgb332(0, 230, 80);

  for(y = y0 - 3; y < y0 + barh + 3; y++){
    if(y < 0 || y >= H)
      continue;
    for(x = x0 - 3; x < x0 + barw + 3; x++){
      if(x < 0 || x >= W)
        continue;
      if(y == y0 - 3 || y == y0 + barh + 2 || x == x0 - 3 || x == x0 + barw + 2)
        frame[y * W + x] = border;
    }
  }

  for(i = 0; i < CELLS; i++){
    int cx0 = x0 + i * cellw;
    int cx1 = (i == CELLS - 1) ? (x0 + barw) : (cx0 + cellw);
    uchar c = (i < filled) ? onc : offc;
    for(y = y0; y < y0 + barh; y++){
      for(x = cx0 + 1; x < cx1 - 1; x++)
        frame[y * W + x] = c;
    }
  }
}

int
main(void)
{
  int step = 0;

  for(;;){
    draw_background();
    draw_flag();
    draw_progress(step);
    if(gui(frame, sizeof(frame)) < 0){
      printf(2, "prog: gui syscall failed\n");
      exit(0);
    }
    step++;
    if(step > CELLS)
      step = 0;
    sleep(TICKS_PER_SEC);
  }
}
