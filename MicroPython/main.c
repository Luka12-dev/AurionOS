/* MicroPython main entry point for AurionOS */

#include <stdint.h>
#include <string.h>

#include "py/builtin.h"
#include "py/compile.h"
#include "py/runtime.h"
#include "py/repl.h"
#include "py/gc.h"
#include "py/mperrno.h"
#include "py/objexcept.h"
#include "shared/runtime/pyexec.h"

/* AurionOS system calls */
extern void c_puts(const char *s);
extern void c_putc(char c);
extern void set_attr(uint8_t a);
extern int boot_mode_flag;

/* Filesystem interface */
extern int load_file_content(const char *filename, char *buffer, int max_len);

/* Heap for garbage collector */
static char heap[MICROPY_HEAP_SIZE];
static char *stack_top;

/* Help text */
const char aurionos_help_text[] =
"Welcome to MicroPython on AurionOS!\n"
"\n"
"Control commands:\n"
"  CTRL-C -- interrupt running program\n"
"  CTRL-D -- exit REPL\n"
"\n"
"For online help type help().\n"
"For more information see http://micropython.org/\n";

/* Execute a Python file from AurionOS filesystem */
int micropython_exec_file(const char *filename) {
    static char file_buffer[8192];
    
    int size = load_file_content(filename, file_buffer, sizeof(file_buffer));
    if (size < 0) {
        set_attr(0x0C);
        c_puts("File does not exist: ");
        c_puts(filename);
        c_puts("\n");
        set_attr(0x07);
        return -2; /* not found */
    }
    if (size == 0) {
        /* Empty file: treat as a no-op success */
        return 0;
    }
    
    /* Ensure null termination */
    if (size < (int)sizeof(file_buffer)) {
        file_buffer[size] = '\0';
    } else {
        file_buffer[sizeof(file_buffer) - 1] = '\0';
    }
    
    /* Execute the script */
    nlr_buf_t nlr;
    if (nlr_push(&nlr) == 0) {
        mp_lexer_t *lex = mp_lexer_new_from_str_len(
            qstr_from_str(filename), 
            file_buffer, 
            strlen(file_buffer), 
            0
        );
        qstr source_name = lex->source_name;
        mp_parse_tree_t parse_tree = mp_parse(lex, MP_PARSE_FILE_INPUT);
        mp_obj_t module_fun = mp_compile(&parse_tree, source_name, false);
        mp_call_function_0(module_fun);
        nlr_pop();
        return 0;
    } else {
        /* Exception occurred */
        mp_obj_t exc = (mp_obj_t)nlr.ret_val;
        mp_obj_print_exception(&mp_plat_print, exc);

        /* Extra detail for SyntaxError: show line with a caret. */
        if (mp_obj_exception_match(exc, MP_OBJ_FROM_PTR(&mp_type_SyntaxError))) {
            size_t n_tb = 0;
            size_t *tb = NULL;
            mp_obj_exception_get_traceback(exc, &n_tb, &tb);
            if (n_tb >= 2 && tb) {
                /* tb layout: file, line, block ... take the line */
                size_t line = tb[1];
                if (line >= 1) {
                    const char *p = file_buffer;
                    size_t cur = 1;
                    while (*p && cur < line) {
                        if (*p == '\n') cur++;
                        p++;
                    }
                    const char *ls = p;
                    while (*p && *p != '\n' && *p != '\r') p++;
                    const char *le = p;
                    c_puts("  Line ");
                    char num[16];
                    extern void int_to_str(uint32_t, char *);
                    int_to_str((uint32_t)line, num);
                    c_puts(num);
                    c_puts(": ");
                    for (const char *q = ls; q < le; q++) {
                        char ch = *q;
                        if (ch == '\t') ch = ' ';
                        c_putc(ch);
                    }
                    c_putc('\n');
                    c_puts("        ^\n");
                }
            }
        }
        return -3; /* runtime/compile error */
    }
}

/* Start MicroPython REPL */
void micropython_repl(void) {
    /* Check if we're in GUI mode */
    if (boot_mode_flag == 0) {
        set_attr(0x0C);
        c_puts("ERROR: MicroPython REPL is not available in GUI mode.\n");
        c_puts("Use: PYTHON filename.py to run a script.\n");
        set_attr(0x07);
        return;
    }
    
    set_attr(0x0B);
    c_puts("MicroPython v1.28.0 on AurionOS\n");
    c_puts("Type \"help()\" for more information.\n");
    set_attr(0x07);
    
    pyexec_friendly_repl();
}

/* Initialize MicroPython */
void micropython_init(void) {
    int stack_dummy;
    stack_top = (char *)&stack_dummy;
    
    gc_init(heap, heap + sizeof(heap));
    mp_init();
}

/* Deinitialize MicroPython */
void micropython_deinit(void) {
    mp_deinit();
}

/* Garbage collection */
void gc_collect(void) {
    void *dummy;
    gc_collect_start();
    gc_collect_root(&dummy, ((mp_uint_t)stack_top - (mp_uint_t)&dummy) / sizeof(mp_uint_t));
    gc_collect_end();
}

/* File system stubs - AurionOS has its own FS */
mp_lexer_t *mp_lexer_new_from_file(qstr filename) {
    mp_raise_OSError(MP_ENOENT);
}

mp_import_stat_t mp_import_stat(const char *path) {
    return MP_IMPORT_STAT_NO_EXIST;
}

/* Error handlers */
void nlr_jump_fail(void *val) {
    set_attr(0x0C);
    c_puts("\nFATAL: uncaught NLR jump\n");
    set_attr(0x07);
    while (1) {
        __asm__ volatile("hlt");
    }
}

void MP_NORETURN __fatal_error(const char *msg) {
    set_attr(0x0C);
    c_puts("\nFATAL ERROR: ");
    c_puts(msg);
    c_puts("\n");
    set_attr(0x07);
    while (1) {
        __asm__ volatile("hlt");
    }
}

#ifndef NDEBUG
void MP_WEAK __assert_func(const char *file, int line, const char *func, const char *expr) {
    set_attr(0x0C);
    c_puts("Assertion failed: ");
    c_puts(expr);
    c_puts(" at ");
    c_puts(file);
    c_puts("\n");
    set_attr(0x07);
    __fatal_error("Assertion failed");
}
#endif
