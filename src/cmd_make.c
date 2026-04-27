/*
 * MAKE command for Aurion OS
 * A minimal build system inspired by GNU Make.
 * Reads a "Makefile" from the filesystem and executes build rules.
 *
 * Supports:
 *   - Target: dependency rules
 *   - Tab-indented commands (executed via cmd_dispatch)
 *   - Variable assignment (VAR = value) and expansion $(VAR)
 *   - Default target (first target) or specified target
 *   - .PHONY declarations
 *   - Comment lines starting with #
*/

#include <stdint.h>
#include <stdbool.h>

/* External OS functions */
extern void c_puts(const char *s);
extern void c_putc(char c);
extern uint16_t c_getkey(void);
extern int cmd_dispatch(const char *line);
extern int str_cmp(const char *a, const char *b);
extern int str_len(const char *s);
extern void str_copy(char *dst, const char *src, int max);

/* Filesystem access */
#include "../include/fs.h"

extern int fs_count;
extern FSEntry fs_table[];
extern FileContent file_contents[];
extern int file_content_count;

extern char current_dir[256];

/* Forward declaration for attribute setting */
extern void set_attr(uint8_t attr);

/* String helpers */
static int make_str_len(const char *s) {
    int l = 0;
    while (s[l]) l++;
    return l;
}

static int make_str_cmp(const char *a, const char *b) {
    if (!a || !b) return -1;
    while (*a && *b) {
        char ca = (*a >= 'a' && *a <= 'z') ? *a - 32 : *a;
        char cb = (*b >= 'a' && *b <= 'z') ? *b - 32 : *b;
        if (ca != cb) return (int)(ca - cb);
        a++; b++;
    }
    char ca = (*a >= 'a' && *a <= 'z') ? *a - 32 : *a;
    char cb = (*b >= 'a' && *b <= 'z') ? *b - 32 : *b;
    return (int)(ca - cb);
}

static void make_str_copy(char *dst, const char *src, int max) {
    int i = 0;
    while (i < max - 1 && src[i]) { dst[i] = src[i]; i++; }
    dst[i] = 0;
}

static int make_str_startswith(const char *s, const char *prefix) {
    while (*prefix) {
        char cs = (*s >= 'a' && *s <= 'z') ? *s - 32 : *s;
        char cp = (*prefix >= 'a' && *prefix <= 'z') ? *prefix - 32 : *prefix;
        if (cs != cp) return 0;
        s++; prefix++;
    }
    return 1;
}

static void make_str_trim(char *s) {
    /* Trim trailing whitespace */
    int len = make_str_len(s);
    while (len > 0 && (s[len - 1] == ' ' || s[len - 1] == '\t' ||
           s[len - 1] == '\r' || s[len - 1] == '\n')) {
        s[--len] = 0;
    }
}

/* Variables */
#define MAX_VARS 32
#define MAX_VAR_NAME 32
#define MAX_VAR_VAL 128

static char var_names[MAX_VARS][MAX_VAR_NAME];
static char var_values[MAX_VARS][MAX_VAR_VAL];
static int var_count = 0;

static void var_set(const char *name, const char *value) {
    for (int i = 0; i < var_count; i++) {
        if (make_str_cmp(var_names[i], name) == 0) {
            make_str_copy(var_values[i], value, MAX_VAR_VAL);
            return;
        }
    }
    if (var_count < MAX_VARS) {
        make_str_copy(var_names[var_count], name, MAX_VAR_NAME);
        make_str_copy(var_values[var_count], value, MAX_VAR_VAL);
        var_count++;
    }
}

static const char *var_get(const char *name) {
    for (int i = 0; i < var_count; i++) {
        if (make_str_cmp(var_names[i], name) == 0) {
            return var_values[i];
        }
    }
    return "";
}

/* Expand variables in a string: $(VAR) -> value */
static void expand_vars(const char *src, char *dst, int max) {
    int si = 0, di = 0;
    while (src[si] && di < max - 1) {
        if (src[si] == '$' && src[si + 1] == '(') {
            si += 2;
            char vname[MAX_VAR_NAME];
            int vi = 0;
            while (src[si] && src[si] != ')' && vi < MAX_VAR_NAME - 1) {
                vname[vi++] = src[si++];
            }
            vname[vi] = 0;
            if (src[si] == ')') si++;
            const char *val = var_get(vname);
            while (*val && di < max - 1) {
                dst[di++] = *val++;
            }
        } else {
            dst[di++] = src[si++];
        }
    }
    dst[di] = 0;
}

/* Targets */
#define MAX_TARGETS 32
#define MAX_TARGET_NAME 64
#define MAX_TARGET_CMDS 8
#define MAX_CMD_LEN 256
#define MAX_DEPS 8

typedef struct {
    char name[MAX_TARGET_NAME];
    char deps[MAX_DEPS][MAX_TARGET_NAME];
    int dep_count;
    char commands[MAX_TARGET_CMDS][MAX_CMD_LEN];
    int cmd_count;
    bool phony;
    bool built;
} MakeTarget;

static MakeTarget targets[MAX_TARGETS];
static int target_count = 0;
static char default_target[MAX_TARGET_NAME];

static MakeTarget *find_target(const char *name) {
    for (int i = 0; i < target_count; i++) {
        if (make_str_cmp(targets[i].name, name) == 0) {
            return &targets[i];
        }
    }
    return 0;
}

/* Makefile parser */
static int parse_makefile(const char *content, int content_len) {
    target_count = 0;
    var_count = 0;
    default_target[0] = 0;

    MakeTarget *current_target = 0;
    int pos = 0;

    while (pos < content_len) {
        /* Read one line */
        char line[MAX_CMD_LEN];
        int li = 0;
        while (pos < content_len && content[pos] != '\n' && li < MAX_CMD_LEN - 1) {
            if (content[pos] != '\r') {
                line[li++] = content[pos];
            }
            pos++;
        }
        line[li] = 0;
        if (pos < content_len) pos++; /* Skip \n */

        /* Skip empty lines */
        if (li == 0) continue;

        /* Skip comments */
        if (line[0] == '#') continue;

        /* Command line (starts with tab) */
        if (line[0] == '\t' && current_target) {
            if (current_target->cmd_count < MAX_TARGET_CMDS) {
                char expanded[MAX_CMD_LEN];
                expand_vars(line + 1, expanded, MAX_CMD_LEN);
                make_str_copy(current_target->commands[current_target->cmd_count],
                              expanded, MAX_CMD_LEN);
                current_target->cmd_count++;
            }
            continue;
        }

        /* .PHONY declaration */
        if (make_str_startswith(line, ".PHONY:")) {
            const char *p = line + 7;
            while (*p == ' ') p++;
            /* Mark target as phony */
            char pname[MAX_TARGET_NAME];
            int pi = 0;
            while (*p && *p != ' ' && pi < MAX_TARGET_NAME - 1) {
                pname[pi++] = *p++;
            }
            pname[pi] = 0;
            MakeTarget *t = find_target(pname);
            if (t) t->phony = true;
            continue;
        }

        /* Variable assignment: VAR = value */
        const char *eq = line;
        while (*eq && *eq != '=' && *eq != ':') eq++;
        if (*eq == '=' && (eq == line || *(eq - 1) != ':')) {
            char vname[MAX_VAR_NAME];
            int vi = 0;
            const char *p = line;
            while (p < eq && *p != ' ' && *p != '\t' && vi < MAX_VAR_NAME - 1) {
                vname[vi++] = *p++;
            }
            vname[vi] = 0;

            const char *val = eq + 1;
            while (*val == ' ' || *val == '\t') val++;
            char expanded_val[MAX_VAR_VAL];
            expand_vars(val, expanded_val, MAX_VAR_VAL);
            make_str_trim(expanded_val);
            var_set(vname, expanded_val);
            current_target = 0;
            continue;
        }

        /* Target rule: target: dep1 dep2 */
        const char *colon = line;
        while (*colon && *colon != ':') colon++;
        if (*colon == ':') {
            if (target_count >= MAX_TARGETS) continue;

            current_target = &targets[target_count];
            /* Zero out */
            for (int i = 0; i < (int)sizeof(MakeTarget); i++) {
                ((char *)current_target)[i] = 0;
            }

            /* Extract target name */
            int ni = 0;
            const char *p = line;
            while (p < colon && *p != ' ' && *p != '\t' && ni < MAX_TARGET_NAME - 1) {
                current_target->name[ni++] = *p++;
            }
            current_target->name[ni] = 0;

            /* First target is the default */
            if (target_count == 0 && current_target->name[0] != '.') {
                make_str_copy(default_target, current_target->name, MAX_TARGET_NAME);
            }

            /* Parse dependencies */
            p = colon + 1;
            while (*p == ' ') p++;
            while (*p && current_target->dep_count < MAX_DEPS) {
                char dep[MAX_TARGET_NAME];
                int di = 0;
                while (*p && *p != ' ' && *p != '\t' && di < MAX_TARGET_NAME - 1) {
                    dep[di++] = *p++;
                }
                dep[di] = 0;
                if (di > 0) {
                    char expanded_dep[MAX_TARGET_NAME];
                    expand_vars(dep, expanded_dep, MAX_TARGET_NAME);
                    make_str_copy(current_target->deps[current_target->dep_count],
                                  expanded_dep, MAX_TARGET_NAME);
                    current_target->dep_count++;
                }
                while (*p == ' ') p++;
            }

            target_count++;
        } else {
            current_target = 0;
        }
    }

    return 0;
}

/* Build target */
static int build_target(const char *name);

static int build_target(const char *name) {
    MakeTarget *t = find_target(name);
    if (!t) {
        c_puts("make: *** No rule to make target '");
        c_puts(name);
        c_puts("'. Stop.\n");
        return -1;
    }

    if (t->built) return 0;

    /* Build dependencies first */
    for (int i = 0; i < t->dep_count; i++) {
        MakeTarget *dep = find_target(t->deps[i]);
        if (dep) {
            int rc = build_target(t->deps[i]);
            if (rc != 0) return rc;
        }
    }

    /* Execute commands */
    for (int i = 0; i < t->cmd_count; i++) {
        const char *cmd = t->commands[i];
        /* Skip silent prefix '@' */
        bool silent = false;
        if (cmd[0] == '@') {
            silent = true;
            cmd++;
        }

        if (!silent) {
            set_attr(0x0B);
            c_puts(cmd);
            c_puts("\n");
            set_attr(0x07);
        }

        /* Try to execute via shell */
        int rc = cmd_dispatch(cmd);
        if (rc == -255) {
            /* Command not found -- print it as if executed (for display commands) */
            if (!silent) {
                /* Already printed above */
            }
        } else if (rc != 0) {
            set_attr(0x0C);
            c_puts("make: *** Error in recipe for target '");
            c_puts(t->name);
            c_puts("'. Stop.\n");
            set_attr(0x07);
            return rc;
        }
    }

    t->built = true;
    return 0;
}

/*  MAKE command entry point */
int cmd_make(const char *args) {
    /* Find Makefile in current directory */
    const char *makefile_name = "Makefile";
    char fullpath[256];

    /* Build full path */
    make_str_copy(fullpath, current_dir, 256);
    int plen = make_str_len(fullpath);
    if (plen > 0 && fullpath[plen - 1] != '/') {
        fullpath[plen++] = '/';
        fullpath[plen] = 0;
    }
    /* Append filename */
    int fi = 0;
    while (makefile_name[fi] && plen < 255) {
        fullpath[plen++] = makefile_name[fi++];
    }
    fullpath[plen] = 0;

    /* Search for the Makefile in filesystem */
    int found_idx = -1;
    for (int i = 0; i < fs_count; i++) {
        if (make_str_cmp(fs_table[i].name, fullpath) == 0 &&
            fs_table[i].type == 0) {
            found_idx = i;
            break;
        }
    }

    if (found_idx < 0) {
        const char *variants[] = {"Makefile", "makefile", "MAKEFILE",
                                   "GNUmakefile", "gnumakefile", 0};
        for (int i = 0; i < fs_count && found_idx < 0; i++) {
            if (fs_table[i].type != 0) continue;
            
            /* Entry name must be longer than current_dir */
            int dir_len = make_str_len(current_dir);
            int name_len = make_str_len(fs_table[i].name);
            if (name_len <= dir_len) continue;

            /* Entry must start with current_dir */
            if (!make_str_startswith(fs_table[i].name, current_dir)) continue;
            
            /* Get the filename part after the directory */
            const char *fname = fs_table[i].name + dir_len;
            if (*fname == '/') fname++; /* Skip the separator */

            /* Must be a direct child (no more slashes) */
            bool sub = false;
            for (int k = 0; fname[k]; k++) {
                if (fname[k] == '/') { sub = true; break; }
            }
            if (sub) continue;
            /* Check against variants */
            for (int v = 0; variants[v]; v++) {
                if (make_str_cmp(fname, variants[v]) == 0) {
                    found_idx = i;
                    break;
                }
            }
        }
    }

    if (found_idx < 0) {
        set_attr(0x0C);
        c_puts("make: *** No Makefile found in '");
        c_puts(current_dir);
        c_puts("'. Stop.\n");
        set_attr(0x07);
        c_puts("Hint: Create a Makefile with NANO Makefile\n");
        return -1;
    }

    /* Find file content */
    const char *content = 0;
    int content_len = 0;
    for (int i = 0; i < file_content_count; i++) {
        if (file_contents[i].file_idx == (uint16_t)found_idx) {
            content = file_contents[i].data;
            content_len = (int)file_contents[i].size;
            break;
        }
    }

    if (!content || content_len == 0) {
        set_attr(0x0C);
        c_puts("make: *** Makefile is empty.\n");
        set_attr(0x07);
        return -1;
    }

    /* Parse the Makefile */
    set_attr(0x0A);
    c_puts("[make] Parsing Makefile...\n");
    set_attr(0x07);

    int rc = parse_makefile(content, content_len);
    if (rc != 0) {
        set_attr(0x0C);
        c_puts("make: *** Parse error in Makefile.\n");
        set_attr(0x07);
        return rc;
    }

    if (target_count == 0) {
        c_puts("make: *** No targets found in Makefile.\n");
        return -1;
    }

    /* Determine target to build */
    char target_name[MAX_TARGET_NAME];
    if (args && args[0] && args[0] != ' ') {
        /* Skip leading spaces */
        while (*args == ' ') args++;
        int ti = 0;
        while (args[ti] && args[ti] != ' ' && ti < MAX_TARGET_NAME - 1) {
            target_name[ti] = args[ti];
            ti++;
        }
        target_name[ti] = 0;
    } else {
        make_str_copy(target_name, default_target, MAX_TARGET_NAME);
    }

    if (target_name[0] == 0) {
        c_puts("make: *** No target specified and no default target.\n");
        return -1;
    }

    set_attr(0x0A);
    c_puts("[make] Building target: ");
    c_puts(target_name);
    c_puts("\n");
    set_attr(0x07);

    /* Reset built flags */
    for (int i = 0; i < target_count; i++) {
        targets[i].built = false;
    }

    /* Build the target */
    rc = build_target(target_name);

    if (rc == 0) {
        set_attr(0x0A);
        c_puts("[make] Build complete.\n");
        set_attr(0x07);
    }

    return rc;
}