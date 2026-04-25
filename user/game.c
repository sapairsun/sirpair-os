#include "types.h"
#include "user.h"

#define W 1024
#define H 768

#define BW 20
#define BH 40

#define CELL 12
#define BOARD_X 300
#define BOARD_Y 60

#define EVT_TICK 'T'

#define KEY_DN 0xE3
#define KEY_LF 0xE4
#define KEY_RT 0xE5
#define KEY_UP 0xE2

static uchar frame[W * H];
static uchar board[BH][BW];

static const char shapes[7][4][4] = {
  { {0,0,0,0}, {1,1,1,1}, {0,0,0,0}, {0,0,0,0} }, // I
  { {1,1,0,0}, {1,1,0,0}, {0,0,0,0}, {0,0,0,0} }, // O
  { {0,1,0,0}, {1,1,1,0}, {0,0,0,0}, {0,0,0,0} }, // T
  { {0,1,1,0}, {1,1,0,0}, {0,0,0,0}, {0,0,0,0} }, // S
  { {1,1,0,0}, {0,1,1,0}, {0,0,0,0}, {0,0,0,0} }, // Z
  { {1,0,0,0}, {1,1,1,0}, {0,0,0,0}, {0,0,0,0} }, // J
  { {0,0,1,0}, {1,1,1,0}, {0,0,0,0}, {0,0,0,0} }  // L
};

static const uchar colors[8] = {
  0x00, 0x1f, 0xe0, 0xfc, 0x92, 0x36, 0xc3, 0xff
};

static int cur_x, cur_y, cur_shape, cur_rot, game_over;

static void
set_input_raw(int on)
{
  if(on){
    printf(1, "\033[901l"); // hide echo while gaming
    printf(1, "\033[902h"); // raw key mode
  } else {
    printf(1, "\033[902l");
    printf(1, "\033[901h");
  }
}

static void
fill_rect(int x0, int y0, int ww, int hh, uchar c)
{
  int x, y;
  for(y = y0; y < y0 + hh; y++){
    if(y < 0 || y >= H)
      continue;
    for(x = x0; x < x0 + ww; x++){
      if(x < 0 || x >= W)
        continue;
      frame[y * W + x] = c;
    }
  }
}

static void
draw_cell(int bx, int by, uchar c)
{
  int x = BOARD_X + bx * CELL;
  int y = BOARD_Y + by * CELL;
  fill_rect(x, y, CELL - 1, CELL - 1, c);
}

static int
shape_cell(int shape, int rot, int r, int c)
{
  switch(rot & 3){
  case 0:  return shapes[shape][r][c];
  case 1:  return shapes[shape][3 - c][r];
  case 2:  return shapes[shape][3 - r][3 - c];
  default: return shapes[shape][c][3 - r];
  }
}

static int
collide_rot(int nx, int ny, int nrot)
{
  int r, c;
  for(r = 0; r < 4; r++){
    for(c = 0; c < 4; c++){
      if(!shape_cell(cur_shape, nrot, r, c))
        continue;
      int x = nx + c;
      int y = ny + r;
      if(x < 0 || x >= BW || y >= BH)
        return 1;
      if(y >= 0 && board[y][x])
        return 1;
    }
  }
  return 0;
}

static int
collide(int nx, int ny)
{
  return collide_rot(nx, ny, cur_rot);
}

static void
draw_scene(void)
{
  int x, y, r, c;
  for(y = 0; y < H; y++)
    for(x = 0; x < W; x++)
      frame[y * W + x] = 0x01; // dark blue

  fill_rect(BOARD_X - 3, BOARD_Y - 3, BW * CELL + 6, BH * CELL + 6, 0xff);
  fill_rect(BOARD_X, BOARD_Y, BW * CELL, BH * CELL, 0x00);

  for(y = 0; y < BH; y++){
    for(x = 0; x < BW; x++){
      if(board[y][x])
        draw_cell(x, y, colors[board[y][x] & 7]);
    }
  }

  for(r = 0; r < 4; r++){
    for(c = 0; c < 4; c++){
      if(!shape_cell(cur_shape, cur_rot, r, c))
        continue;
      x = cur_x + c;
      y = cur_y + r;
      if(x >= 0 && x < BW && y >= 0 && y < BH)
        draw_cell(x, y, colors[(cur_shape % 7) + 1]);
    }
  }

  if(game_over){
    fill_rect(BOARD_X + 20, BOARD_Y + BH * CELL / 2 - 24, BW * CELL - 40, 48, 0x00);
    fill_rect(BOARD_X + 24, BOARD_Y + BH * CELL / 2 - 20, BW * CELL - 48, 40, 0xff);
  }

  gui(frame, sizeof(frame));
}

static void
clear_lines(void)
{
  int y, x, yy, full;
  for(y = BH - 1; y >= 0; y--){
    full = 1;
    for(x = 0; x < BW; x++){
      if(!board[y][x]){
        full = 0;
        break;
      }
    }
    if(!full)
      continue;
    for(yy = y; yy > 0; yy--){
      for(x = 0; x < BW; x++)
        board[yy][x] = board[yy - 1][x];
    }
    for(x = 0; x < BW; x++)
      board[0][x] = 0;
    y++;
  }
}

static void
spawn_piece(void)
{
  cur_shape = uptime() % 7;
  cur_rot = 0;
  cur_x = BW / 2 - 2;
  cur_y = -1;
  if(collide(cur_x, cur_y))
    game_over = 1;
}

static void
lock_piece(void)
{
  int r, c;
  for(r = 0; r < 4; r++){
    for(c = 0; c < 4; c++){
      if(!shape_cell(cur_shape, cur_rot, r, c))
        continue;
      int x = cur_x + c;
      int y = cur_y + r;
      if(y >= 0 && y < BH && x >= 0 && x < BW)
        board[y][x] = (cur_shape % 7) + 1;
    }
  }
  clear_lines();
  spawn_piece();
}

static void
tick_down(void)
{
  if(game_over)
    return;
  if(collide(cur_x, cur_y + 1))
    lock_piece();
  else
    cur_y++;
}

static void
try_rotate(void)
{
  int nr = (cur_rot + 1) & 3;
  if(collide_rot(cur_x, cur_y, nr))
    return;
  cur_rot = nr;
}

int
main(void)
{
  int p[2];
  int tpid, kpid;
  uchar ev;
  uchar ch;

  memset(board, 0, sizeof(board));
  game_over = 0;
  spawn_piece();
  draw_scene();
  set_input_raw(1);

  if(pipe(p) < 0){
    printf(2, "game: pipe failed\n");
    exit(0);
  }

  tpid = fork();
  if(tpid == 0){
    close(p[0]);
    for(;;){
      sleep(40); // 0.4s @100Hz, half falling speed
      ev = EVT_TICK;
      if(write(p[1], &ev, 1) != 1)
        break;
    }
    exit(0);
  }

  kpid = fork();
  if(kpid == 0){
    int esc_state = 0;
    close(p[0]);
    for(;;){
      if(read(0, &ch, 1) != 1)
        break;
      // Path A: native Sirpair keycodes from keyboard driver.
      if(ch == KEY_LF || ch == KEY_RT || ch == KEY_DN || ch == KEY_UP){
        ev = ch;
        if(write(p[1], &ev, 1) != 1)
          break;
        esc_state = 0;
        continue;
      }

      // Path B: ANSI escape sequences from some terminals:
      // ESC [ D/C/B/A  or  ESC O D/C/B/A
      if(ch == 0x1b){
        esc_state = 1;
        continue;
      }
      if(esc_state == 1){
        if(ch == '[' || ch == 'O'){
          esc_state = 2;
          continue;
        }
        esc_state = 0;
      } else if(esc_state == 2){
        if(ch == 'D')
          ev = KEY_LF;
        else if(ch == 'C')
          ev = KEY_RT;
        else if(ch == 'B')
          ev = KEY_DN;
        else if(ch == 'A')
          ev = KEY_UP;
        else
          ev = 0;
        esc_state = 0;
        if(ev && write(p[1], &ev, 1) != 1)
          break;
        continue;
      }

      // Optional fallback controls to ease validation on serial terminals.
      if(ch == 'a')
        ev = KEY_LF;
      else if(ch == 'd')
        ev = KEY_RT;
      else if(ch == 's')
        ev = KEY_DN;
      else if(ch == 'w')
        ev = KEY_UP;
      else if(ch == 'q')
        ev = 'q';
      else
        ev = 0;

      if(ev){
        if(write(p[1], &ev, 1) != 1)
          break;
      }
    }
    exit(0);
  }

  close(p[1]);
  while(read(p[0], &ev, 1) == 1){
    if(ev == 'q')
      break;
    if(ev == EVT_TICK){
      tick_down();
    } else if(ev == KEY_LF){
      if(!game_over && !collide(cur_x - 1, cur_y))
        cur_x--;
    } else if(ev == KEY_RT){
      if(!game_over && !collide(cur_x + 1, cur_y))
        cur_x++;
    } else if(ev == KEY_DN){
      tick_down();
    } else if(ev == KEY_UP){
      if(!game_over)
        try_rotate();
    }
    draw_scene();
  }

  kill(tpid);
  kill(kpid);
  wait();
  wait();
  set_input_raw(0);
  exit(0);
}
