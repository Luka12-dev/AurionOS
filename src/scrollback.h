#ifndef SCROLLBACK_H
#define SCROLLBACK_H

#include <stdint.h>
#include <stdbool.h>

// Save current line to scrollback
void scrollback_save_line(const char *line, const uint8_t *attrs, uint16_t line_num);

// Scroll functions
void scrollback_scroll_up(void);
void scrollback_scroll_down(void);

// Scroll state
int32_t scrollback_get_offset(void);
void scrollback_reset(void);
bool scrollback_is_active(void);

#endif // SCROLLBACK_H
