/*
 * Scrollback Buffer for AurionOS
 * Allows scrolling through command history with PgUp/PgDn
*/

#include <stdint.h>
#include <stdbool.h>

#define SCROLLBACK_LINES 500
#define SCREEN_WIDTH 80
#define SCREEN_HEIGHT 25
#define VGA_MEMORY ((volatile uint16_t*)0xB8000)

// Scrollback buffer (circular buffer)
static uint16_t scrollback_buffer[SCROLLBACK_LINES][SCREEN_WIDTH];
static uint32_t scrollback_write_pos = 0;
static uint32_t scrollback_count = 0;
static int32_t scroll_offset = 0;

// Saved screen when scrolling
static uint16_t saved_screen[SCREEN_HEIGHT][SCREEN_WIDTH];
static bool screen_saved = false;

// Save current screen before scrolling
static void save_current_screen(void) {
    if (screen_saved) return;
    for (int y = 0; y < SCREEN_HEIGHT; y++) {
        for (int x = 0; x < SCREEN_WIDTH; x++) {
            saved_screen[y][x] = VGA_MEMORY[y * SCREEN_WIDTH + x];
        }
    }
    screen_saved = true;
}

// Restore saved screen
static void restore_saved_screen(void) {
    if (!screen_saved) return;
    for (int y = 0; y < SCREEN_HEIGHT; y++) {
        for (int x = 0; x < SCREEN_WIDTH; x++) {
            VGA_MEMORY[y * SCREEN_WIDTH + x] = saved_screen[y][x];
        }
    }
    screen_saved = false;
}

// Capture a line from VGA to scrollback (called when screen scrolls)
void scrollback_capture_line(void) {
    // Capture top line of screen before it scrolls away
    for (int x = 0; x < SCREEN_WIDTH; x++) {
        scrollback_buffer[scrollback_write_pos][x] = VGA_MEMORY[x];
    }
    
    scrollback_write_pos = (scrollback_write_pos + 1) % SCROLLBACK_LINES;
    if (scrollback_count < SCROLLBACK_LINES) {
        scrollback_count++;
    }
}

// Save current line to scrollback (legacy API)
void scrollback_save_line(const char *line, const uint8_t *attrs, uint16_t line_num) {
    (void)line_num;
    for (int x = 0; x < SCREEN_WIDTH; x++) {
        uint8_t ch = line ? line[x] : ' ';
        uint8_t at = attrs ? attrs[x] : 0x07;
        scrollback_buffer[scrollback_write_pos][x] = (at << 8) | ch;
    }
    scrollback_write_pos = (scrollback_write_pos + 1) % SCROLLBACK_LINES;
    if (scrollback_count < SCROLLBACK_LINES) {
        scrollback_count++;
    }
}

// Redraw screen from scrollback
static void scrollback_redraw(void) {
    if (scrollback_count == 0) return;
    
    // Calculate which lines to show
    int32_t total_lines = scrollback_count + SCREEN_HEIGHT;
    int32_t view_start = total_lines - SCREEN_HEIGHT - scroll_offset;
    
    for (int y = 0; y < SCREEN_HEIGHT; y++) {
        int32_t line_idx = view_start + y;
        
        if (line_idx < 0) {
            // Before scrollback - show empty
            for (int x = 0; x < SCREEN_WIDTH; x++) {
                VGA_MEMORY[y * SCREEN_WIDTH + x] = 0x0720; // gray space
            }
        } else if (line_idx < (int32_t)scrollback_count) {
            // From scrollback buffer
            uint32_t buf_idx = (scrollback_write_pos + SCROLLBACK_LINES - scrollback_count + line_idx) % SCROLLBACK_LINES;
            for (int x = 0; x < SCREEN_WIDTH; x++) {
                VGA_MEMORY[y * SCREEN_WIDTH + x] = scrollback_buffer[buf_idx][x];
            }
        } else {
            // From saved current screen
            int screen_y = line_idx - scrollback_count;
            if (screen_y < SCREEN_HEIGHT && screen_saved) {
                for (int x = 0; x < SCREEN_WIDTH; x++) {
                    VGA_MEMORY[y * SCREEN_WIDTH + x] = saved_screen[screen_y][x];
                }
            }
        }
    }
    
    // Show scroll indicator on right edge
    if (scroll_offset > 0) {
        VGA_MEMORY[0 * SCREEN_WIDTH + 79] = 0x4E00 | '^'; // Red ^ at top
        VGA_MEMORY[24 * SCREEN_WIDTH + 79] = 0x4E00 | 'v'; // Red v at bottom
    }
}

// Scroll up (show older content) - called from interrupt
void scrollback_scroll_up(void) {
    if (scrollback_count == 0) return;
    
    // Save screen on first scroll
    if (scroll_offset == 0) {
        save_current_screen();
    }
    
    if (scroll_offset < (int32_t)scrollback_count) {
        scroll_offset += 5; // Scroll 5 lines at a time
        if (scroll_offset > (int32_t)scrollback_count) {
            scroll_offset = scrollback_count;
        }
        scrollback_redraw();
    }
}

// Scroll down (show newer content) - called from interrupt
void scrollback_scroll_down(void) {
    if (scroll_offset > 0) {
        scroll_offset -= 5;
        if (scroll_offset <= 0) {
            scroll_offset = 0;
            restore_saved_screen();
        } else {
            scrollback_redraw();
        }
    }
}

// Get scroll offset
int32_t scrollback_get_offset(void) {
    return scroll_offset;
}

// Reset scroll (return to live view)
void scrollback_reset(void) {
    if (scroll_offset > 0) {
        scroll_offset = 0;
        restore_saved_screen();
    }
}

// Check if in scrollback mode
bool scrollback_is_active(void) {
    return scroll_offset > 0;
}