#ifndef ICONS_H
#define ICONS_H

/* Icon draw function type */
typedef void (*icon_draw_fn)(int x, int y);

/* Individual icon draw functions */
void icon_draw_terminal(int x, int y);
void icon_draw_browser(int x, int y);
void icon_draw_notepad(int x, int y);
void icon_draw_paint(int x, int y);
void icon_draw_calc(int x, int y);
void icon_draw_files(int x, int y);
void icon_draw_clock(int x, int y);
void icon_draw_sysinfo(int x, int y);
void icon_draw_file(int x, int y);
void icon_draw_folder(int x, int y);

/* Look up icon draw function by app name */
icon_draw_fn icon_get_draw_fn(const char *name);

#endif
