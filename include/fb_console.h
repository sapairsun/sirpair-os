#ifndef SIRPAIR_FB_CONSOLE_H
#define SIRPAIR_FB_CONSOLE_H

#include "types.h"

void fb_init(void);
int fb_is_active(void);
void fb_idle_poll(void);
void fb_draw_glyph_vga(uint col, uint row, uchar ch, uchar cga_attr);
void fb_paint_cursor(uint col, uint row, int lit, uchar cga_attr);
void fb_scroll_content(void);
void fb_clear_content(void);
uint fb_cols_get(void);
uint fb_rows_get(void);
uint fb_panic_row(void);
void fb_panic_draw(int cpu, const char *msg);
void fb_gui_mode_set(int on);
int fb_gui_mode_get(void);

#endif
