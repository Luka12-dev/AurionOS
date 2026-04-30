#ifndef ICON_LOADER_H
#define ICON_LOADER_H

/* Call once at boot to decode all embedded PNG icons */
void icons_load_all(void);

/* Per-icon draw functions - pass to wm_dock_add() */
void icon_draw_png_terminal  (int x, int y);
void icon_draw_png_browser   (int x, int y);
void icon_draw_png_notepad   (int x, int y);
void icon_draw_png_calculator(int x, int y);
void icon_draw_png_files     (int x, int y);
void icon_draw_png_clock     (int x, int y);
void icon_draw_png_paint     (int x, int y);
void icon_draw_png_sysinfo   (int x, int y);
void icon_draw_png_settings  (int x, int y);
void icon_draw_png_folder(int x, int y);
void icon_draw_png_file(int x, int y);
void icon_draw_png_snake(int x, int y);
void icon_draw_png_3d_demo(int x, int y);
void icon_draw_png_cube(int x, int y);
void icon_draw_png_taskmgr(int x, int y);
void icon_draw_png_audiomgr(int x, int y);


#endif
