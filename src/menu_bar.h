#ifndef MENU_BAR_H
#define MENU_BAR_H

#include <stdint.h>
#include <stdbool.h>

/* ── Menu item ──────────────────────────────────────────────────── */
typedef struct {
    const char *label;
    void      (*action)(void);  /* NULL = no action (separator)   */
    bool        separator;      /* true = draw as divider line     */
    bool        enabled;        /* false = grayed out              */
} MenuItem;

/* ── Menu ───────────────────────────────────────────────────────── */
typedef struct {
    const char *title;
    MenuItem   *items;
    int         item_count;
    int         bar_x;          /* pixel x of title on menu bar   */
    int         bar_w;          /* pixel width of title area       */
    int         drop_x;         /* pixel x of dropdown panel      */
    int         drop_w;         /* pixel width of dropdown panel  */
    int         drop_h;         /* pixel height of dropdown panel */
} Menu;

/* ── Lifecycle ──────────────────────────────────────────────────── */
void menubar_init(void);
void menubar_clear(void);
void menubar_add_menu(const char *title, MenuItem *items, int item_count);

/* ── Per-frame ──────────────────────────────────────────────────── */
void menubar_draw(void);

/* ── Input ──────────────────────────────────────────────────────── *
 *  Call this BEFORE wm_handle_input every frame.
 *  Pass raw mouse coordinates and button state.
 *  Returns true if the event was consumed (don't pass to windows).
 */
bool menubar_handle_mouse(int mx, int my, bool lmb, bool prev_lmb);

/* ── State query ────────────────────────────────────────────────── */
bool menubar_is_open(void);
void menubar_close(void);

#endif /* MENU_BAR_H */