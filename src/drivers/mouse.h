#ifndef MOUSE_H
#define MOUSE_H

#include <stdbool.h>
#include <stdint.h>

/* Initialize PS/2 mouse */
int mouse_init(void);

/* Poll mouse state */
void mouse_poll(void);

/* Get mouse state */
int mouse_get_x(void);
int mouse_get_y(void);
bool mouse_get_left(void);
bool mouse_get_right(void);

/* Set mouse bounds (e.g. for different screen modes) */
void mouse_set_bounds(int width, int height);

#endif
