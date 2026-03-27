/*
 * cmd_python.c - Python interpreter for AurionOS
 *
 * Supports:
 *   - python <filename>  : run a .py file from the filesystem
 *   - python             : interactive REPL mode
 *
 * Language features:
 *   - print("...") and print(var)
 *   - Variables (integers and strings)
 *   - Arithmetic: + - * / %
 *   - Comparisons: == != < > <= >=
 *   - if / elif / else
 *   - while loops
 *   - for i in range(n) / for i in range(a, b)
 *   - Comments (#)
 *   - String concatenation with +
 *   - len(), str(), int(), input()
*/

#include <stdint.h>
#include <stdbool.h>

/* OS hooks */
extern void c_puts(const char *s);
extern void c_putc(char c);
extern uint16_t c_getkey(void);
extern void set_attr(uint8_t a);
extern int str_len(const char *s);
extern void str_copy(char *dst, const char *src, int max);
extern int str_cmp(const char *a, const char *b);

/* Filesystem */
extern int fs_count;
typedef struct {
    char name[56];
    uint32_t size;
    uint8_t type;
    uint8_t attr;
    uint16_t parent_idx;
    uint16_t reserved;
} PyFSEntry;
extern PyFSEntry fs_table[];

typedef struct {
    uint16_t file_idx;
    uint32_t size;
    char data[1024];
} PyFileContent;
extern PyFileContent file_contents[];
extern int file_content_count;
extern char current_dir[256];

/* tiny helpers */
static int py_isdigit(char c) { return c >= '0' && c <= '9'; }
static int py_isalpha(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}
static int py_isalnum(char c) { return py_isalpha(c) || py_isdigit(c); }
static int py_isspace(char c) __attribute__((unused));
static int py_isspace(char c) { return c == ' ' || c == '\t' || c == '\r'; }

static int py_strlen(const char *s) {
    int n = 0; while (s[n]) n++; return n;
}
static void py_strcpy(char *d, const char *s) {
    int i = 0; while (s[i]) { d[i] = s[i]; i++; } d[i] = 0;
}
static int py_strcmp(const char *a, const char *b) {
    while (*a && *b && *a == *b) { a++; b++; }
    return (unsigned char)*a - (unsigned char)*b;
}
static void py_strncpy(char *d, const char *s, int n) {
    int i = 0;
    while (i < n - 1 && s[i]) { d[i] = s[i]; i++; }
    d[i] = 0;
}
static int py_strncmp(const char *a, const char *b, int n) {
    for (int i = 0; i < n; i++) {
        if (a[i] != b[i] || !a[i]) return (unsigned char)a[i] - (unsigned char)b[i];
    }
    return 0;
}
static void py_strcat(char *d, const char *s) {
    int dl = py_strlen(d);
    py_strcpy(d + dl, s);
}

static int py_atoi(const char *s) {
    int neg = 0, n = 0;
    while (*s == ' ') s++;
    if (*s == '-') { neg = 1; s++; }
    while (py_isdigit(*s)) { n = n * 10 + (*s - '0'); s++; }
    return neg ? -n : n;
}

static void py_itoa(int n, char *buf) {
    if (n == 0) { buf[0] = '0'; buf[1] = 0; return; }
    int neg = 0, i = 0;
    if (n < 0) { neg = 1; n = -n; }
    char tmp[16]; int ti = 0;
    while (n > 0) { tmp[ti++] = '0' + (n % 10); n /= 10; }
    if (neg) buf[i++] = '-';
    for (int j = ti - 1; j >= 0; j--) buf[i++] = tmp[j];
    buf[i] = 0;
}

/* Value system */
#define VAL_INT    0
#define VAL_STR    1
#define VAL_NONE   2

typedef struct {
    int type;
    int ival;
    char sval[128];
} PyVal;

/* Variable store */
#define MAX_VARS 32
static char var_names[MAX_VARS][32];
static PyVal var_vals[MAX_VARS];
static int var_count = 0;

static void py_reset_vars(void) { var_count = 0; }

static PyVal *py_getvar(const char *name) {
    for (int i = 0; i < var_count; i++)
        if (py_strcmp(var_names[i], name) == 0) return &var_vals[i];
    return 0;
}

static PyVal *py_setvar(const char *name, PyVal val) {
    for (int i = 0; i < var_count; i++) {
        if (py_strcmp(var_names[i], name) == 0) {
            var_vals[i] = val;
            return &var_vals[i];
        }
    }
    if (var_count < MAX_VARS) {
        py_strncpy(var_names[var_count], name, 32);
        var_vals[var_count] = val;
        return &var_vals[var_count++];
    }
    return 0;
}

/* Output helpers */
static void py_print_val(PyVal v) {
    if (v.type == VAL_INT) {
        char buf[16]; py_itoa(v.ival, buf); c_puts(buf);
    } else if (v.type == VAL_STR) {
        c_puts(v.sval);
    }
}

/* Skip whitespace in line */
static const char *skip_ws(const char *p) {
    while (*p == ' ' || *p == '\t') p++;
    return p;
}

/* Read identifier */
static const char *read_ident(const char *p, char *out, int max) {
    int i = 0;
    while (py_isalnum(*p) && i < max - 1) out[i++] = *p++;
    out[i] = 0;
    return p;
}

/* forward declare evaluate */
static PyVal py_eval_expr(const char *expr);

/* Parse a primary value (literal, variable, call) */
static PyVal py_parse_primary(const char *p, const char **end) {
    PyVal v; v.type = VAL_NONE; v.ival = 0; v.sval[0] = 0;
    p = skip_ws(p);

    /* String literal */
    if (*p == '"' || *p == '\'') {
        char q = *p++; int i = 0;
        while (*p && *p != q && i < 127) { v.sval[i++] = *p++; }
        if (*p == q) p++;
        v.sval[i] = 0; v.type = VAL_STR;
        if (end) *end = p;
        return v;
    }

    /* Negative number */
    if (*p == '-' && py_isdigit(*(p+1))) {
        p++; int n = 0;
        while (py_isdigit(*p)) { n = n * 10 + (*p - '0'); p++; }
        v.type = VAL_INT; v.ival = -n;
        if (end) *end = p;
        return v;
    }

    /* Positive number */
    if (py_isdigit(*p)) {
        int n = 0;
        while (py_isdigit(*p)) { n = n * 10 + (*p - '0'); p++; }
        v.type = VAL_INT; v.ival = n;
        if (end) *end = p;
        return v;
    }

    /* Identifier or function call */
    if (py_isalpha(*p)) {
        char name[64]; p = read_ident(p, name, 64);
        p = skip_ws(p);
        /* Function calls */
        if (*p == '(') {
            p++; /* skip '(' */
            const char *arg_end = p;
            /* collect args until matching ')' */
            char arg_buf[256]; int ai = 0; int depth = 1;
            while (*arg_end && depth > 0) {
                if (*arg_end == '(') depth++;
                else if (*arg_end == ')') { if (--depth == 0) break; }
                if (depth > 0 && ai < 255) arg_buf[ai++] = *arg_end;
                arg_end++;
            }
            arg_buf[ai] = 0;
            if (*arg_end == ')') arg_end++;

            /* len() */
            if (py_strcmp(name, "len") == 0) {
                PyVal a = py_eval_expr(arg_buf);
                v.type = VAL_INT;
                v.ival = (a.type == VAL_STR) ? py_strlen(a.sval) : 0;
            }
            /* str() */
            else if (py_strcmp(name, "str") == 0) {
                PyVal a = py_eval_expr(arg_buf);
                v.type = VAL_STR;
                if (a.type == VAL_INT) py_itoa(a.ival, v.sval);
                else py_strcpy(v.sval, a.sval);
            }
            /* int() */
            else if (py_strcmp(name, "int") == 0) {
                PyVal a = py_eval_expr(arg_buf);
                v.type = VAL_INT;
                v.ival = (a.type == VAL_STR) ? py_atoi(a.sval) : a.ival;
            }
            /* range() - returns sentinel, handled by for-loop */
            else if (py_strcmp(name, "range") == 0) {
                /* pack as VAL_STR with marker for for-loop to parse */
                v.type = VAL_STR;
                v.sval[0] = '\x01'; /* RANGE marker */
                py_strcpy(v.sval + 1, arg_buf);
            }
            /* input() */
            else if (py_strcmp(name, "input") == 0) {
                PyVal prompt = py_eval_expr(arg_buf);
                if (prompt.type == VAL_STR) c_puts(prompt.sval);
                /* Read a line from keyboard */
                char ibuf[128]; int ii = 0;
                while (1) {
                    uint16_t k = c_getkey();
                    uint8_t ch = (uint8_t)(k & 0xFF);
                    if (ch == '\r' || ch == '\n') { c_putc('\n'); break; }
                    if (ch == 8 && ii > 0) { ii--; c_putc(8); c_putc(' '); c_putc(8); }
                    else if (ch >= 32 && ch < 127 && ii < 127) { ibuf[ii++] = ch; c_putc(ch); }
                }
                ibuf[ii] = 0;
                v.type = VAL_STR; py_strcpy(v.sval, ibuf);
            }
            else {
                /* Unknown function - return None */
                v.type = VAL_NONE;
            }
            if (end) *end = arg_end;
            return v;
        }
        /* Variable lookup */
        PyVal *var = py_getvar(name);
        if (var) v = *var;
        else { v.type = VAL_INT; v.ival = 0; }
        if (end) *end = p;
        return v;
    }

    if (end) *end = p;
    return v;
}

/* Full expression evaluator with operator precedence */

/* Multiply / Divide / Modulo */
static PyVal py_eval_muldiv(const char *p, const char **end) {
    const char *cur = p;
    PyVal left = py_parse_primary(cur, &cur);
    cur = skip_ws(cur);
    while (*cur == '*' || *cur == '/' || *cur == '%') {
        char op = *cur++;
        PyVal right = py_parse_primary(cur, &cur);
        cur = skip_ws(cur);
        if (left.type == VAL_INT && right.type == VAL_INT) {
            if (op == '*') left.ival *= right.ival;
            else if (op == '/' && right.ival != 0) left.ival /= right.ival;
            else if (op == '%' && right.ival != 0) left.ival %= right.ival;
        }
    }
    if (end) *end = cur;
    return left;
}

/* Add / Subtract / String concat */
static PyVal py_eval_addsub(const char *p, const char **end) {
    const char *cur = p;
    PyVal left = py_eval_muldiv(cur, &cur);
    cur = skip_ws(cur);
    while (*cur == '+' || *cur == '-') {
        char op = *cur++;
        PyVal right = py_eval_muldiv(cur, &cur);
        cur = skip_ws(cur);
        if (left.type == VAL_INT && right.type == VAL_INT) {
            if (op == '+') left.ival += right.ival;
            else            left.ival -= right.ival;
        } else if (op == '+') {
            /* string concat - convert both to str */
            char tmp[128];
            if (left.type == VAL_INT) { py_itoa(left.ival, tmp); py_strcpy(left.sval, tmp); left.type = VAL_STR; }
            if (right.type == VAL_INT) { py_itoa(right.ival, tmp); py_strcat(left.sval, tmp); }
            else py_strcat(left.sval, right.sval);
        }
    }
    if (end) *end = cur;
    return left;
}

/* Comparisons */
static PyVal py_eval_cmp(const char *p, const char **end) {
    const char *cur = p;
    PyVal left = py_eval_addsub(cur, &cur);
    cur = skip_ws(cur);

    /* two-char ops first */
    if (py_strncmp(cur, "==", 2) == 0) { cur += 2; PyVal r = py_eval_addsub(cur, &cur);
        left.ival = (left.type==VAL_STR) ? py_strcmp(left.sval,r.sval)==0 : left.ival==r.ival;
        left.type = VAL_INT; }
    else if (py_strncmp(cur, "!=", 2) == 0) { cur += 2; PyVal r = py_eval_addsub(cur, &cur);
        left.ival = (left.type==VAL_STR) ? py_strcmp(left.sval,r.sval)!=0 : left.ival!=r.ival;
        left.type = VAL_INT; }
    else if (py_strncmp(cur, "<=", 2) == 0) { cur += 2; PyVal r = py_eval_addsub(cur, &cur);
        left.ival = left.ival <= r.ival; left.type = VAL_INT; }
    else if (py_strncmp(cur, ">=", 2) == 0) { cur += 2; PyVal r = py_eval_addsub(cur, &cur);
        left.ival = left.ival >= r.ival; left.type = VAL_INT; }
    else if (*cur == '<') { cur++; PyVal r = py_eval_addsub(cur, &cur);
        left.ival = left.ival < r.ival; left.type = VAL_INT; }
    else if (*cur == '>') { cur++; PyVal r = py_eval_addsub(cur, &cur);
        left.ival = left.ival > r.ival; left.type = VAL_INT; }

    if (end) *end = cur;
    return left;
}

/* 'not' unary */
static PyVal py_eval_not(const char *p, const char **end) {
    p = skip_ws(p);
    if (py_strncmp(p, "not ", 4) == 0) {
        p += 4;
        PyVal v = py_eval_cmp(p, end);
        v.ival = !v.ival; v.type = VAL_INT;
        return v;
    }
    return py_eval_cmp(p, end);
}

/* 'and' / 'or' */
static PyVal py_eval_bool(const char *p, const char **end) {
    const char *cur = p;
    PyVal left = py_eval_not(cur, &cur);
    cur = skip_ws(cur);
    while (1) {
        if (py_strncmp(cur, "and ", 4) == 0) {
            cur += 4;
            PyVal r = py_eval_not(cur, &cur); cur = skip_ws(cur);
            left.ival = left.ival && r.ival; left.type = VAL_INT;
        } else if (py_strncmp(cur, "or ", 3) == 0) {
            cur += 3;
            PyVal r = py_eval_not(cur, &cur); cur = skip_ws(cur);
            left.ival = left.ival || r.ival; left.type = VAL_INT;
        } else break;
    }
    if (end) *end = cur;
    return left;
}

/* Top-level evaluator */
static PyVal py_eval_expr(const char *expr) {
    const char *end;
    return py_eval_bool(expr, &end);
}

/* Source lines store */
#define MAX_LINES 256
#define MAX_LINE_LEN 256
static char py_lines[MAX_LINES][MAX_LINE_LEN];
static int  py_line_count = 0;

/* Count leading spaces (indent level) */
static int indent_of(const char *line) {
    int n = 0;
    while (line[n] == ' ' || line[n] == '\t') n++;
    return n;
}

/* Execute a range of lines (start inclusive, end exclusive) */
static void py_exec_block(int start, int end_line);

/* Find the end of a block starting at line 'start' with indent 'base_indent' */
static int find_block_end(int start, int base_indent) {
    int i = start;
    while (i < py_line_count) {
        const char *l = skip_ws(py_lines[i]);
        if (*l == 0 || *l == '#') { i++; continue; } /* blank/comment */
        if (indent_of(py_lines[i]) <= base_indent) break;
        i++;
    }
    return i;
}

/* Execute a single statement line */
static void py_exec_line(int lineno) {
    const char *raw = py_lines[lineno];
    const char *p   = skip_ws(raw);

    if (!*p || *p == '#') return; /* blank or comment */

    /* print(...) */
    if (py_strncmp(p, "print(", 6) == 0) {
        p += 6;
        /* collect args until matching ')' */
        char args[256]; int ai = 0; int depth = 1;
        while (*p && depth > 0) {
            if (*p == '(') depth++;
            else if (*p == ')') { if (--depth == 0) break; }
            if (depth > 0 && ai < 255) args[ai++] = *p;
            p++;
        }
        args[ai] = 0;
        /* split by comma, evaluate each, print separated by space */
        /* simple split: find commas at depth 0 */
        int prev = 0, alen = ai;
        bool first = true;
        for (int i = 0; i <= alen; i++) {
            if (args[i] == ',' || i == alen) {
                char part[128]; int pi = 0;
                for (int j = prev; j < i && pi < 127; j++) part[pi++] = args[j];
                part[pi] = 0;
                if (!first) c_putc(' ');
                first = false;
                PyVal v = py_eval_expr(part);
                py_print_val(v);
                prev = i + 1;
            }
        }
        c_putc('\n');
        return;
    }

    /* Assignment: name = expr  (must have no colon at end) */
    /* look for '=' not preceded by <, >, !, = */
    {
        const char *eq = p;
        while (*eq && *eq != '=' && *eq != ':' && *eq != '\n') eq++;
        if (*eq == '=' && eq > p &&
            *(eq-1) != '<' && *(eq-1) != '>' &&
            *(eq-1) != '!' && *(eq-1) != '=') {
            /* read var name */
            char vname[32]; int vi = 0;
            const char *np = p;
            while (py_isalnum(*np) && vi < 31) vname[vi++] = *np++;
            vname[vi] = 0;
            const char *val_start = eq + 1;
            PyVal v = py_eval_expr(val_start);
            py_setvar(vname, v);
            return;
        }
    }

    /* Standalone expression (e.g. function call we don't handle yet) */
    py_eval_expr(p);
}

/* Execute a block of lines */
static void py_exec_block(int start, int end_line) {
    int i = start;
    while (i < end_line) {
        const char *raw = py_lines[i];
        const char *p   = skip_ws(raw);
        int base_indent = indent_of(raw);

        if (!*p || *p == '#') { i++; continue; }

        /* if / elif / else */
        if (py_strncmp(p, "if ", 3) == 0 || py_strncmp(p, "elif ", 5) == 0) {
            /* collect condition */
            const char *cond_start = p + ((*p == 'i') ? 3 : 5);
            /* strip trailing colon */
            char cond[256]; int ci = 0;
            while (*cond_start && *cond_start != ':' && ci < 255)
                cond[ci++] = *cond_start++;
            cond[ci] = 0;
            PyVal cv = py_eval_expr(cond);
            int body_start = i + 1;
            int body_end   = find_block_end(body_start, base_indent);
            if (cv.ival) {
                py_exec_block(body_start, body_end);
                /* skip elif/else chains */
                i = body_end;
                while (i < end_line) {
                    const char *nx = skip_ws(py_lines[i]);
                    if (indent_of(py_lines[i]) == base_indent &&
                        (py_strncmp(nx, "elif ", 5) == 0 || py_strncmp(nx, "else", 4) == 0)) {
                        int skip_body = i + 1;
                        i = find_block_end(skip_body, base_indent);
                    } else break;
                }
            } else {
                i = body_end;
                /* check for elif/else */
                if (i < end_line) {
                    const char *nx = skip_ws(py_lines[i]);
                    if (indent_of(py_lines[i]) == base_indent &&
                        py_strncmp(nx, "elif ", 5) == 0) {
                        continue; /* let the loop re-process this elif line */
                    } else if (indent_of(py_lines[i]) == base_indent &&
                               py_strncmp(nx, "else", 4) == 0) {
                        int else_body = i + 1;
                        int else_end  = find_block_end(else_body, base_indent);
                        py_exec_block(else_body, else_end);
                        i = else_end;
                    }
                }
            }
            continue;
        }

        /* else (standalone, after if block already consumed) */
        if (py_strncmp(p, "else", 4) == 0) { i++; continue; }

        /* while condition: */
        if (py_strncmp(p, "while ", 6) == 0) {
            char cond[256]; int ci = 0;
            const char *cs = p + 6;
            while (*cs && *cs != ':' && ci < 255) cond[ci++] = *cs++;
            cond[ci] = 0;
            int body_start = i + 1;
            int body_end   = find_block_end(body_start, base_indent);
            int guard = 0;
            while (py_eval_expr(cond).ival && guard < 100000) {
                py_exec_block(body_start, body_end);
                guard++;
            }
            i = body_end;
            continue;
        }

        /* for var in range(a) or range(a, b): */
        if (py_strncmp(p, "for ", 4) == 0) {
            /* parse: for <var> in <expr>: */
            const char *fp = p + 4;
            char var[32]; int vi = 0;
            while (py_isalnum(*fp) && vi < 31) var[vi++] = *fp++;
            var[vi] = 0;
            fp = skip_ws(fp);
            if (py_strncmp(fp, "in ", 3) == 0) fp += 3;
            /* get iterable expression (strip colon) */
            char iter_expr[128]; int ii = 0;
            while (*fp && *fp != ':' && ii < 127) iter_expr[ii++] = *fp++;
            iter_expr[ii] = 0;
            PyVal itv = py_eval_expr(iter_expr);
            int body_start = i + 1;
            int body_end   = find_block_end(body_start, base_indent);
            /* range marker */
            if (itv.type == VAL_STR && itv.sval[0] == '\x01') {
                const char *rargs = itv.sval + 1;
                /* parse a or a,b */
                int a = 0, b = 0;
                const char *comma = rargs;
                while (*comma && *comma != ',') comma++;
                if (*comma == ',') {
                    char tmp1[16]; int ti = 0;
                    while (rargs + ti < comma && ti < 15) { tmp1[ti] = rargs[ti]; ti++; }
                    tmp1[ti] = 0;
                    a = py_atoi(tmp1);
                    b = py_atoi(comma + 1);
                } else {
                    a = 0; b = py_atoi(rargs);
                }
                for (int ri = a; ri < b; ri++) {
                    PyVal rv; rv.type = VAL_INT; rv.ival = ri; rv.sval[0] = 0;
                    py_setvar(var, rv);
                    py_exec_block(body_start, body_end);
                }
            }
            i = body_end;
            continue;
        }

        /* regular statement */
        py_exec_line(i);
        i++;
    }
}

/* Load source from AurionOS filesystem */
static int py_load_file(const char *filename) {
    /* Build full path */
    char full_path[256];
    int dl = py_strlen(current_dir);
    for (int i = 0; i < dl && i < 255; i++) full_path[i] = current_dir[i];
    int nl = py_strlen(filename);
    for (int i = 0; i < nl && dl + i < 255; i++) full_path[dl + i] = filename[i];
    full_path[dl + nl] = 0;

    /* also try the raw filename if it already looks absolute */
    const char *search = (filename[1] == ':') ? filename : full_path;

    /* find in fs_table */
    int fidx = -1;
    for (int i = 0; i < fs_count; i++) {
        if (py_strcmp(fs_table[i].name, search) == 0 && fs_table[i].type == 0) {
            fidx = i; break;
        }
    }
    if (fidx < 0) {
        c_puts("python: file not found: ");
        c_puts(filename);
        c_puts("\n");
        return -1;
    }

    /* find content */
    const char *src = 0; int src_len = 0;
    for (int i = 0; i < file_content_count; i++) {
        if (file_contents[i].file_idx == (uint16_t)fidx) {
            src     = file_contents[i].data;
            src_len = (int)file_contents[i].size;
            break;
        }
    }
    if (!src || src_len == 0) {
        c_puts("python: file is empty\n");
        return -1;
    }

    /* split into lines */
    py_line_count = 0;
    int pos = 0;
    while (pos < src_len && py_line_count < MAX_LINES) {
        int li = 0;
        while (pos < src_len && src[pos] != '\n' && li < MAX_LINE_LEN - 1) {
            if (src[pos] != '\r') py_lines[py_line_count][li++] = src[pos];
            pos++;
        }
        py_lines[py_line_count][li] = 0;
        py_line_count++;
        if (pos < src_len) pos++; /* skip \n */
    }
    return 0;
}

/* REPL mode */
static void py_repl(void) {
    set_attr(0x0B);
    c_puts("AurionOS Python 3 (interpreter)\n");
    c_puts("Type 'exit()' to quit\n");
    set_attr(0x07);

    char line_buf[MAX_LINE_LEN];
    while (1) {
        set_attr(0x0A); c_puts(">>> "); set_attr(0x07);
        int pos = 0;
        while (1) {
            uint16_t k = c_getkey();
            uint8_t ch = (uint8_t)(k & 0xFF);
            if (ch == '\r' || ch == '\n') { c_putc('\n'); break; }
            if (ch == 3) {
                c_puts("\nKeyboardInterrupt\n");
                return;
            }
            if (ch == 8 && pos > 0) {
                pos--; c_putc(8); c_putc(' '); c_putc(8);
            } else if (ch >= 32 && ch < 127 && pos < MAX_LINE_LEN - 1) {
                line_buf[pos++] = ch; c_putc(ch);
            }
        }
        line_buf[pos] = 0;
        const char *p = skip_ws(line_buf);
        if (py_strcmp(p, "exit()") == 0 || py_strcmp(p, "quit()") == 0) break;
        /* put the line in the buffer and execute it */
        py_strncpy(py_lines[0], line_buf, MAX_LINE_LEN);
        py_line_count = 1;
        py_exec_block(0, 1);
    }
}

/* Entry point called by cmd_dispatch */
int cmd_python(const char *args) {
    py_reset_vars();

    /* skip leading spaces */
    while (*args == ' ') args++;

    if (*args == 0) {
        /* No argument: REPL */
        py_repl();
        return 0;
    }

    /* argument given: run file */
    /* The shell uppercases the whole command line before dispatching,
       so we need to handle case. The filename may have been uppercased.
       We search case-insensitively by also trying lower-case extension. */
    char fname[64];
    int fi = 0;
    while (*args && *args != ' ' && fi < 63) fname[fi++] = *args++;
    fname[fi] = 0;

    set_attr(0x0B);
    c_puts("Running: "); c_puts(fname); c_puts("\n");
    set_attr(0x07);

    if (py_load_file(fname) != 0) {
        /* Try lowercase version */
        for (int i = 0; fname[i]; i++)
            if (fname[i] >= 'A' && fname[i] <= 'Z') fname[i] += 32;
        if (py_load_file(fname) != 0) return -1;
    }

    py_exec_block(0, py_line_count);

    set_attr(0x08);
    c_puts("\n[Process exited]\n");
    set_attr(0x07);
    return 0;
}