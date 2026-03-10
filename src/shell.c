#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

extern void c_puts(const char *s);
extern void c_putc(char c);
extern uint16_t c_getkey(void);
extern void set_attr(uint8_t a);
extern int cmd_dispatch(const char *cmd);
extern void cmd_init(void);
extern void cmd_init_silent(void);  /* Silent version for GUI restore */
extern int netif_init(void);  /* Network interface initialization */
extern char current_dir[256];  /* Get current directory from commands.c */
extern void wifi_autostart(void);  /* WiFi auto-initialization */

/* Keyboard layout remapping (commands.c) */
extern char keyboard_remap(char c);

/* Cursor and scrollback */
extern void cursor_init(void);
extern void cursor_set_style(uint8_t style);

// Scrollback functions
extern void scrollback_scroll_up(void);
extern void scrollback_scroll_down(void);
extern void scrollback_reset(void);
extern bool scrollback_is_active(void);

/* GUI screen restore after reboot */
extern int gui_check_and_restore_screen(void);

#define MAX_INPUT 256
#define HISTORY_SIZE 20

static char history[HISTORY_SIZE][MAX_INPUT];
static int history_count = 0;
static int history_pos = 0;

static size_t str_len(const char *s) {
    size_t l = 0;
    while(s[l]) l++;
    return l;
}

static void str_trim(char *s) {
    size_t len = str_len(s);
    while (len > 0 && (s[len-1] == ' ' || s[len-1] == '\t' || s[len-1] == '\n' || s[len-1] == '\r')) {
        s[--len] = '\0';
    }
}

static void strupr(char *s) {
    while (*s) {
        if (*s >= 'a' && *s <= 'z')
            *s -= 32;
        s++;
    }
}

/* Compare strings (case insensitive) */
static int str_cmp_nocase(const char *a, const char *b) {
    while (*a && *b) {
        char ca = (*a >= 'a' && *a <= 'z') ? *a - 32 : *a;
        char cb = (*b >= 'a' && *b <= 'z') ? *b - 32 : *b;
        if (ca != cb) return ca - cb;
        a++; b++;
    }
    return *a - *b;
}

void shell_main(void) {
    char line[MAX_INPUT];
    int pos = 0;

    /* CRITICAL: Initialize command system and filesystem FIRST! 
       This must happen before gui_check_and_restore_screen() or wm_load_settings() */
    cmd_init();

    set_attr(0x07);

    /* Check if we're returning from GUI via reboot - restore screen if so */
    if (gui_check_and_restore_screen()) {
        /* Screen was restored from GUI exit, skip boot messages */
        /* Skip netif_init too as it may print messages */
        /* Print newline after the restored "guitest" line, then go to prompt */
        c_putc('\n');
        /* Go straight to command prompt */
        goto prompt_loop;
    }

    /* Initialize custom cursor - DISABLED */
    // cursor_init();
    // cursor_set_style(0); // Block cursor

    /* Initialize network interface subsystem */
    netif_init();    
    /* Auto-detect and initialize WiFi hardware */
    wifi_autostart();

prompt_loop:
    while (1) {
        set_attr(0x0E); // Yellow prompt
        c_puts(current_dir);
        c_puts("> ");
        set_attr(0x07); // Gray/white input
        
        pos = 0;
        for (int i = 0; i < MAX_INPUT; i++) line[i] = 0;

        while (1) {
            uint16_t k = c_getkey();
            uint8_t key = (uint8_t)(k & 0xFF);
            uint8_t scan = (uint8_t)((k >> 8) & 0xFF);
            
            // Handle arrow keys for command history
            if (key == 0) { // Special key (scan code only)
                if (scan == 0x48) { // Up arrow
                    if (history_count > 0) {
                        // Clear current line
                        while (pos > 0) {
                            c_putc(8); c_putc(' '); c_putc(8);
                            pos--;
                        }
                        
                        // Move back in history
                        if (history_pos > 0) {
                            history_pos--;
                        }
                        
                        // Copy from history
                        int i = 0;
                        while (history[history_pos][i] && i < MAX_INPUT - 1) {
                            line[i] = history[history_pos][i];
                            c_putc(line[i]);
                            i++;
                        }
                        line[i] = '\0';
                        pos = i;
                    }
                    continue;
                }
                
                if (scan == 0x50) { // Down arrow
                    if (history_count > 0 && history_pos < history_count - 1) {
                        // Clear current line
                        while (pos > 0) {
                            c_putc(8); c_putc(' '); c_putc(8);
                            pos--;
                        }
                        
                        // Move forward in history
                        history_pos++;
                        
                        // Copy from history
                        int i = 0;
                        while (history[history_pos][i] && i < MAX_INPUT - 1) {
                            line[i] = history[history_pos][i];
                            c_putc(line[i]);
                            i++;
                        }
                        line[i] = '\0';
                        pos = i;
                    }
                    continue;
                }
                
                // Handle Page Up/Down for scrollback
                if (scan == 0x49) { // Page Up
                    scrollback_scroll_up();
                    continue;
                }
                if (scan == 0x51) { // Page Down
                    scrollback_scroll_down();
                    continue;
                }
            }

            if (key == 13 || key == 10) { 
                line[pos] = '\0';
                c_putc('\n');
                break;
            }
            else if (key == 3) {  /* Ctrl+C */
                c_puts("^C\n");
                pos = 0;
                break;
            }
            else if (key == 8) {
                if (pos > 0) {
                    pos--;
                    c_putc(8); c_putc(' '); c_putc(8);
                }
            } 
            else if (pos < MAX_INPUT - 1 && key >= 32 && key <= 126) {
                char mapped = keyboard_remap((char)key);
                line[pos++] = mapped;
                c_putc(mapped);
            }
        }

        str_trim(line);

        if (str_len(line) > 0) {
            // Add to history
            if (history_count < HISTORY_SIZE) {
                // Add new entry
                int i = 0;
                while (line[i] && i < MAX_INPUT - 1) {
                    history[history_count][i] = line[i];
                    i++;
                }
                history[history_count][i] = '\0';
                history_count++;
            } else {
                // Shift history up and add at end
                for (int i = 0; i < HISTORY_SIZE - 1; i++) {
                    for (int j = 0; j < MAX_INPUT; j++) {
                        history[i][j] = history[i + 1][j];
                    }
                }
                int i = 0;
                while (line[i] && i < MAX_INPUT - 1) {
                    history[HISTORY_SIZE - 1][i] = line[i];
                    i++;
                }
                history[HISTORY_SIZE - 1][i] = '\0';
            }
            history_pos = history_count;
            
            // set_attr(0x07); // Default text
            strupr(line);

            /* Check for GUIMODE - return from shell to kernel for mode switch */
            if (str_cmp_nocase(line, "GUIMODE") == 0) {
                c_puts("Switching to GUI mode...\n");
                return; /* Returns to kernel, which will start desktop */
            }

            /* Check for EXIT */
            if (str_cmp_nocase(line, "EXIT") == 0) {
                c_puts("Returning to desktop...\n");
                return;
            }

            int result = cmd_dispatch(line);
            
            if (result == -255) {
                c_puts("Bad command or file name\n");
            } else if (result == -2) {
                /* Special code: command printed its own prompt (e.g., cls) */
                /* Skip printing the prompt on next iteration */
                pos = 0;
                for (int i = 0; i < MAX_INPUT; i++) line[i] = 0;
                continue; /* Go directly to input without printing prompt */
            }
        }
    }
}