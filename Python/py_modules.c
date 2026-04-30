/*
 * py_modules.c - Python Standard Library C Stubs for AurionOS
 *
 * These provide the C implementations of Python built-in modules.
 */

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include <time.h>
#include "core/aurion_python.h"

extern uint32_t get_ticks(void);
extern int sys_get_time(uint8_t *h, uint8_t *m, uint8_t *s);
extern int sys_get_date(uint8_t *day, uint8_t *month, uint16_t *year);
extern void mem_get_stats(uint32_t *stats);

extern uint32_t wm_get_screen_w(void);
extern uint32_t wm_get_screen_h(void);

static PyObject *pyos_name(PyObject *self, PyObject **args, size_t nargs)
{
    (void)self;
    (void)args;
    (void)nargs;
    return py_new_str("AurionOS");
}

static PyObject *pyos_version(PyObject *self, PyObject **args, size_t nargs)
{
    (void)self;
    (void)args;
    (void)nargs;
    return py_new_str("1.1 Beta");
}

static PyObject *pyos_getenv(PyObject *self, PyObject **args, size_t nargs)
{
    (void)self;
    if (nargs < 1)
        return py_new_str("");
    return py_new_str("");
}

static PyObject *pyos_putenv(PyObject *self, PyObject **args, size_t nargs)
{
    (void)self;
    (void)args;
    (void)nargs;
    return py_new_int(0);
}

static PyObject *pyos_unsetenv(PyObject *self, PyObject **args, size_t nargs)
{
    (void)self;
    (void)args;
    (void)nargs;
    return py_new_int(0);
}

static PyObject *pyos_getcwd(PyObject *self, PyObject **args, size_t nargs)
{
    (void)self;
    (void)args;
    (void)nargs;
    return py_new_str("/");
}

static PyObject *pyos_chdir(PyObject *self, PyObject **args, size_t nargs)
{
    (void)self;
    (void)args;
    (void)nargs;
    return py_new_int(0);
}

static PyObject *pyos_listdir(PyObject *self, PyObject **args, size_t nargs)
{
    (void)self;
    (void)args;
    (void)nargs;
    PyObject *result = py_new_list(0);
    PyList_Append(result, py_new_str("."));
    PyList_Append(result, py_new_str(".."));
    PyList_Append(result, py_new_str("home"));
    PyList_Append(result, py_new_str("etc"));
    PyList_Append(result, py_new_str("usr"));
    PyList_Append(result, py_new_str("bin"));
    return result;
}

static PyObject *pyos_mkdir(PyObject *self, PyObject **args, size_t nargs)
{
    (void)self;
    (void)args;
    (void)nargs;
    return py_new_int(0);
}

static PyObject *pyos_remove(PyObject *self, PyObject **args, size_t nargs)
{
    (void)self;
    (void)args;
    (void)nargs;
    return py_new_int(0);
}

static PyObject *pyos_rename(PyObject *self, PyObject **args, size_t nargs)
{
    (void)self;
    (void)args;
    (void)nargs;
    return py_new_int(0);
}

static PyObject *pyos_stat(PyObject *self, PyObject **args, size_t nargs)
{
    (void)self;
    (void)args;
    (void)nargs;
    PyObject *result = py_new_dict();
    PyDict_SetItemString(result, "st_mode", py_new_int(0x8000));
    PyDict_SetItemString(result, "st_size", py_new_int(0));
    PyDict_SetItemString(result, "st_atime", py_new_int(0));
    PyDict_SetItemString(result, "st_mtime", py_new_int(0));
    PyDict_SetItemString(result, "st_ctime", py_new_int(0));
    return result;
}

static PyObject *pyos_system(PyObject *self, PyObject **args, size_t nargs)
{
    (void)self;
    (void)args;
    (void)nargs;
    return py_new_int(0);
}

static PyObject *pyos_exit(PyObject *self, PyObject **args, size_t nargs)
{
    (void)self;
    (void)args;
    (void)nargs;
    return py_new_int(0);
}

static PyObject *pyos_cpu_count(PyObject *self, PyObject **args, size_t nargs)
{
    (void)self;
    (void)args;
    (void)nargs;
    return py_new_int(1);
}

static PyObject *pyos_urandom(PyObject *self, PyObject **args, size_t nargs)
{
    (void)self;
    size_t n = 16;
    if (nargs >= 1)
        n = (size_t)py_to_int(args[0]);
    if (n > 256)
        n = 256;
    uint8_t *buf = (uint8_t *)malloc(n);
    for (size_t i = 0; i < n; i++)
        buf[i] = (uint8_t)((get_ticks() + i * 17) & 0xFF);
    PyObject *result = py_new_bytes(buf, n);
    free(buf);
    return result;
}

static PyObject *pystr_repr(PyObject *self, PyObject **args, size_t nargs)
{
    (void)self;
    if (nargs < 1)
        return py_new_str("");
    PyObject *obj = args[0];
    if (obj->type == PY_STR)
    {
        char buf[256];
        snprintf(buf, sizeof(buf), "'%s'", py_str_cstr(obj));
        return py_new_str(buf);
    }
    if (obj->type == PY_INT)
    {
        char buf[32];
        snprintf(buf, sizeof(buf), "%lld", (long long)obj->val.int_val);
        return py_new_str(buf);
    }
    if (obj->type == PY_NONE)
        return py_new_str("None");
    if (obj->type == PY_BOOL)
        return py_new_str(obj->val.bool_val ? "True" : "False");
    return py_new_str("<object>");
}

static PyObject *pystr_upper(PyObject *self, PyObject **args, size_t nargs)
{
    (void)self;
    if (nargs < 1)
        return py_new_str("");
    if (args[0]->type != PY_STR)
        return py_new_str("");
    const char *s = args[0]->val.str_val.data;
    size_t len = args[0]->val.str_val.len;
    char *r = (char *)malloc(len + 1);
    for (size_t i = 0; i < len; i++)
    {
        char c = s[i];
        if (c >= 'a' && c <= 'z')
            c = c - 32;
        r[i] = c;
    }
    r[len] = '\0';
    PyObject *result = py_new_strn(r, len);
    free(r);
    return result;
}

static PyObject *pystr_lower(PyObject *self, PyObject **args, size_t nargs)
{
    (void)self;
    if (nargs < 1)
        return py_new_str("");
    if (args[0]->type != PY_STR)
        return py_new_str("");
    const char *s = args[0]->val.str_val.data;
    size_t len = args[0]->val.str_val.len;
    char *r = (char *)malloc(len + 1);
    for (size_t i = 0; i < len; i++)
    {
        char c = s[i];
        if (c >= 'A' && c <= 'Z')
            c = c + 32;
        r[i] = c;
    }
    r[len] = '\0';
    PyObject *result = py_new_strn(r, len);
    free(r);
    return result;
}

static PyObject *pystr_strip(PyObject *self, PyObject **args, size_t nargs)
{
    (void)self;
    if (nargs < 1)
        return py_new_str("");
    if (args[0]->type != PY_STR)
        return py_new_str("");
    const char *s = args[0]->val.str_val.data;
    size_t len = args[0]->val.str_val.len;
    while (len > 0 && (s[0] == ' ' || s[0] == '\t' || s[0] == '\n'))
    {
        s++;
        len--;
    }
    while (len > 0 && (s[len - 1] == ' ' || s[len - 1] == '\t' || s[len - 1] == '\n'))
        len--;
    return py_new_strn(s, len);
}

static PyObject *pystr_split(PyObject *self, PyObject **args, size_t nargs)
{
    (void)self;
    if (nargs < 1)
        return py_new_list(0);
    const char *sep = " ";
    size_t seplen = 1;
    if (nargs >= 2 && args[1]->type == PY_STR)
    {
        sep = args[1]->val.str_val.data;
        seplen = args[1]->val.str_val.len;
    }
    PyObject *result = py_new_list(0);
    return result;
}

static PyObject *pystr_join(PyObject *self, PyObject **args, size_t nargs)
{
    (void)self;
    if (nargs < 2)
        return py_new_str("");
    PyObject *seq = args[1];
    return py_new_str("");
}

static PyObject *pystr_replace(PyObject *self, PyObject **args, size_t nargs)
{
    (void)self;
    if (nargs < 3)
        return py_new_str("");
    return py_new_str(py_str_cstr(args[0]));
}

static PyObject *pystr_startswith(PyObject *self, PyObject **args, size_t nargs)
{
    (void)self;
    if (nargs < 2)
        return py_new_bool(false);
    return py_new_bool(false);
}

static PyObject *pystr_endswith(PyObject *self, PyObject **args, size_t nargs)
{
    (void)self;
    if (nargs < 2)
        return py_new_bool(false);
    return py_new_bool(false);
}

static PyObject *pystr_find(PyObject *self, PyObject **args, size_t nargs)
{
    (void)self;
    if (nargs < 2)
        return py_new_int(-1);
    return py_new_int(-1);
}

static PyObject *pystr_count(PyObject *self, PyObject **args, size_t nargs)
{
    (void)self;
    if (nargs < 2)
        return py_new_int(0);
    return py_new_int(0);
}

static PyObject *pystr_format(PyObject *self, PyObject **args, size_t nargs)
{
    (void)self;
    if (nargs < 1)
        return py_new_str("");
    return py_new_str(py_str_cstr(args[0]));
}

static PyObject *pylist_append(PyObject *self, PyObject **args, size_t nargs)
{
    (void)self;
    if (nargs < 2)
        return NULL;
    PyList_Append(args[0], args[1]);
    return py_new_int(0);
}

static PyObject *pylist_extend(PyObject *self, PyObject **args, size_t nargs)
{
    (void)self;
    if (nargs < 2)
        return NULL;
    return py_new_int(0);
}

static PyObject *pylist_insert(PyObject *self, PyObject **args, size_t nargs)
{
    (void)self;
    if (nargs < 3)
        return NULL;
    return py_new_int(0);
}

static PyObject *pylist_remove(PyObject *self, PyObject **args, size_t nargs)
{
    (void)self;
    if (nargs < 2)
        return NULL;
    return py_new_int(0);
}

static PyObject *pylist_pop(PyObject *self, PyObject **args, size_t nargs)
{
    (void)self;
    if (nargs < 1)
        return NULL;
    return py_new_int(0);
}

static PyObject *pylist_index(PyObject *self, PyObject **args, size_t nargs)
{
    (void)self;
    if (nargs < 2)
        return py_new_int(-1);
    return py_new_int(-1);
}

static PyObject *pylist_count(PyObject *self, PyObject **args, size_t nargs)
{
    (void)self;
    if (nargs < 2)
        return py_new_int(0);
    return py_new_int(0);
}

static PyObject *pylist_sort(PyObject *self, PyObject **args, size_t nargs)
{
    (void)self;
    (void)args;
    (void)nargs;
    return py_new_int(0);
}

static PyObject *pylist_reverse(PyObject *self, PyObject **args, size_t nargs)
{
    (void)self;
    (void)args;
    (void)nargs;
    return py_new_int(0);
}

static PyObject *pydict_keys(PyObject *self, PyObject **args, size_t nargs)
{
    (void)self;
    (void)args;
    (void)nargs;
    return py_new_list(0);
}

static PyObject *pydict_values(PyObject *self, PyObject **args, size_t nargs)
{
    (void)self;
    (void)args;
    (void)nargs;
    return py_new_list(0);
}

static PyObject *pydict_items(PyObject *self, PyObject **args, size_t nargs)
{
    (void)self;
    (void)args;
    (void)nargs;
    return py_new_list(0);
}

static PyObject *pydict_update(PyObject *self, PyObject **args, size_t nargs)
{
    (void)self;
    (void)args;
    (void)nargs;
    return py_new_int(0);
}

static PyObject *pydict_clear(PyObject *self, PyObject **args, size_t nargs)
{
    (void)self;
    (void)args;
    (void)nargs;
    return py_new_int(0);
}

static PyObject *pydict_get(PyObject *self, PyObject **args, size_t nargs)
{
    (void)self;
    if (nargs < 2)
        return py_new_str("");
    return py_new_str("");
}

static PyObject *pydict_setdefault(PyObject *self, PyObject **args, size_t nargs)
{
    (void)self;
    if (nargs < 2)
        return py_new_str("");
    return py_new_str("");
}

static PyObject *pydict_pop(PyObject *self, PyObject **args, size_t nargs)
{
    (void)self;
    (void)args;
    (void)nargs;
    return py_new_int(0);
}

static PyObject *pydict_popitem(PyObject *self, PyObject **args, size_t nargs)
{
    (void)self;
    (void)args;
    (void)nargs;
    return py_new_tuple(0);
}

static PyObject *pydict_copy(PyObject *self, PyObject **args, size_t nargs)
{
    (void)self;
    (void)args;
    (void)nargs;
    return py_new_dict();
}

static PyObject *pydict_fromkeys(PyObject *self, PyObject **args, size_t nargs)
{
    (void)self;
    (void)args;
    (void)nargs;
    return py_new_dict();
}

static PyObject *pytime_time(PyObject *self, PyObject **args, size_t nargs)
{
    (void)self;
    (void)args;
    (void)nargs;
    uint32_t ticks = get_ticks();
    return py_new_float((double)ticks / 100.0);
}

static PyObject *pytime_sleep(PyObject *self, PyObject **args, size_t nargs)
{
    (void)self;
    if (nargs < 1)
        return py_new_int(0);
    double sec = py_to_float(args[0]);
    uint32_t ms = (uint32_t)(sec * 100.0);
    uint32_t start = get_ticks();
    while ((get_ticks() - start) < ms)
    {
    }
    return py_new_int(0);
}

static PyObject *pytime_ctime(PyObject *self, PyObject **args, size_t nargs)
{
    (void)self;
    return py_new_str("Thu Jan  1 00:00:00 2026");
}

static PyObject *pytime_localtime(PyObject *self, PyObject **args, size_t nargs)
{
    (void)self;
    PyObject *result = py_new_tuple(9);
    result->val.tuple_val.items[0] = py_new_int(2026);
    result->val.tuple_val.items[1] = py_new_int(1);
    result->val.tuple_val.items[2] = py_new_int(1);
    result->val.tuple_val.items[3] = py_new_int(0);
    result->val.tuple_val.items[4] = py_new_int(0);
    result->val.tuple_val.items[5] = py_new_int(0);
    result->val.tuple_val.items[6] = py_new_int(3);
    result->val.tuple_val.items[7] = py_new_int(0);
    result->val.tuple_val.items[8] = py_new_int(0);
    return result;
}

static PyObject *pytime_gmtime(PyObject *self, PyObject **args, size_t nargs)
{
    (void)self;
    return pytime_localtime(self, args, nargs);
}

static PyObject *pytime_monotonic(PyObject *self, PyObject **args, size_t nargs)
{
    (void)self;
    (void)args;
    (void)nargs;
    uint32_t ticks = get_ticks();
    return py_new_float((double)ticks / 100.0);
}

static PyObject *pytime_perf_counter(PyObject *self, PyObject **args, size_t nargs)
{
    (void)self;
    (void)args;
    (void)nargs;
    uint32_t ticks = get_ticks();
    return py_new_float((double)ticks / 100.0);
}

static PyObject *pymath_pi(PyObject *self, PyObject **args, size_t nargs)
{
    (void)self;
    (void)args;
    (void)nargs;
    return py_new_float(3.141592653589793);
}

static PyObject *pymath_e(PyObject *self, PyObject **args, size_t nargs)
{
    (void)self;
    (void)args;
    (void)nargs;
    return py_new_float(2.718281828459045);
}

static PyObject *pymath_sqrt(PyObject *self, PyObject **args, size_t nargs)
{
    (void)self;
    if (nargs < 1)
        return py_new_float(0.0);
    return py_new_float(sqrt(py_to_float(args[0])));
}

static PyObject *pymath_pow(PyObject *self, PyObject **args, size_t nargs)
{
    (void)self;
    if (nargs < 2)
        return py_new_float(0.0);
    return py_new_float(pow(py_to_float(args[0]), py_to_float(args[1])));
}

static PyObject *pymath_sin(PyObject *self, PyObject **args, size_t nargs)
{
    (void)self;
    if (nargs < 1)
        return py_new_float(0.0);
    return py_new_float(sin(py_to_float(args[0])));
}

static PyObject *pymath_cos(PyObject *self, PyObject **args, size_t nargs)
{
    (void)self;
    if (nargs < 1)
        return py_new_float(0.0);
    return py_new_float(cos(py_to_float(args[0])));
}

static PyObject *pymath_tan(PyObject *self, PyObject **args, size_t nargs)
{
    (void)self;
    if (nargs < 1)
        return py_new_float(0.0);
    return py_new_float(tan(py_to_float(args[0])));
}

static PyObject *pymath_asin(PyObject *self, PyObject **args, size_t nargs)
{
    (void)self;
    if (nargs < 1)
        return py_new_float(0.0);
    return py_new_float(asin(py_to_float(args[0])));
}

static PyObject *pymath_acos(PyObject *self, PyObject **args, size_t nargs)
{
    (void)self;
    if (nargs < 1)
        return py_new_float(0.0);
    return py_new_float(acos(py_to_float(args[0])));
}

static PyObject *pymath_atan(PyObject *self, PyObject **args, size_t nargs)
{
    (void)self;
    if (nargs < 1)
        return py_new_float(0.0);
    return py_new_float(atan(py_to_float(args[0])));
}

static PyObject *pymath_atan2(PyObject *self, PyObject **args, size_t nargs)
{
    (void)self;
    if (nargs < 2)
        return py_new_float(0.0);
    return py_new_float(atan2(py_to_float(args[0]), py_to_float(args[1])));
}

static PyObject *pymath_sinh(PyObject *self, PyObject **args, size_t nargs)
{
    (void)self;
    if (nargs < 1)
        return py_new_float(0.0);
    return py_new_float(sinh(py_to_float(args[0])));
}

static PyObject *pymath_cosh(PyObject *self, PyObject **args, size_t nargs)
{
    (void)self;
    if (nargs < 1)
        return py_new_float(0.0);
    return py_new_float(cosh(py_to_float(args[0])));
}

static PyObject *pymath_tanh(PyObject *self, PyObject **args, size_t nargs)
{
    (void)self;
    if (nargs < 1)
        return py_new_float(0.0);
    return py_new_float(tanh(py_to_float(args[0])));
}

static PyObject *pymath_floor(PyObject *self, PyObject **args, size_t nargs)
{
    (void)self;
    if (nargs < 1)
        return py_new_int(0);
    return py_new_int((int64_t)floor(py_to_float(args[0])));
}

static PyObject *pymath_ceil(PyObject *self, PyObject **args, size_t nargs)
{
    (void)self;
    if (nargs < 1)
        return py_new_int(0);
    return py_new_int((int64_t)ceil(py_to_float(args[0])));
}

static PyObject *pymath_log(PyObject *self, PyObject **args, size_t nargs)
{
    (void)self;
    if (nargs < 1)
        return py_new_float(0.0);
    if (nargs >= 2)
        return py_new_float(log(py_to_float(args[0])) / log(py_to_float(args[1])));
    return py_new_float(log(py_to_float(args[0])));
}

static PyObject *pymath_log10(PyObject *self, PyObject **args, size_t nargs)
{
    (void)self;
    if (nargs < 1)
        return py_new_float(0.0);
    return py_new_float(log10(py_to_float(args[0])));
}

static PyObject *pymath_exp(PyObject *self, PyObject **args, size_t nargs)
{
    (void)self;
    if (nargs < 1)
        return py_new_float(0.0);
    return py_new_float(exp(py_to_float(args[0])));
}

static PyObject *pymath_fabs(PyObject *self, PyObject **args, size_t nargs)
{
    (void)self;
    if (nargs < 1)
        return py_new_float(0.0);
    double v = py_to_float(args[0]);
    if (v < 0)
        v = -v;
    return py_new_float(v);
}

static PyObject *pymath_fmod(PyObject *self, PyObject **args, size_t nargs)
{
    (void)self;
    if (nargs < 2)
        return py_new_float(0.0);
    return py_new_float(fmod(py_to_float(args[0]), py_to_float(args[1])));
}

static PyObject *pymath_isnan(PyObject *self, PyObject **args, size_t nargs)
{
    (void)self;
    if (nargs < 1)
        return py_new_bool(false);
    double v = py_to_float(args[0]);
    return py_new_bool(isnan(v));
}

static PyObject *pymath_isinf(PyObject *self, PyObject **args, size_t nargs)
{
    (void)self;
    if (nargs < 1)
        return py_new_bool(false);
    double v = py_to_float(args[0]);
    return py_new_bool(isinf(v));
}

static PyObject *pymath_degrees(PyObject *self, PyObject **args, size_t nargs)
{
    (void)self;
    if (nargs < 1)
        return py_new_float(0.0);
    return py_new_float(py_to_float(args[0]) * 180.0 / 3.141592653589793);
}

static PyObject *pymath_radians(PyObject *self, PyObject **args, size_t nargs)
{
    (void)self;
    if (nargs < 1)
        return py_new_float(0.0);
    return py_new_float(py_to_float(args[0]) * 3.141592653589793 / 180.0);
}

static PyObject *pymath_hypot(PyObject *self, PyObject **args, size_t nargs)
{
    (void)self;
    if (nargs < 2)
        return py_new_float(0.0);
    double x = py_to_float(args[0]);
    double y = py_to_float(args[1]);
    return py_new_float(sqrt(x * x + y * y));
}

static PyObject *pymath_trunc(PyObject *self, PyObject **args, size_t nargs)
{
    (void)self;
    if (nargs < 1)
        return py_new_int(0);
    return py_new_int((int64_t)trunc(py_to_float(args[0])));
}

static PyObject *pymath_gcd(PyObject *self, PyObject **args, size_t nargs)
{
    (void)self;
    if (nargs < 2)
        return py_new_int(0);
    int64_t a = py_to_int(args[0]);
    int64_t b = py_to_int(args[1]);
    if (a < 0)
        a = -a;
    while (b)
    {
        int64_t t = b;
        b = a % b;
        a = t;
    }
    return py_new_int(a);
}

static PyObject *pymath_lcm(PyObject *self, PyObject **args, size_t nargs)
{
    (void)self;
    if (nargs < 2)
        return py_new_int(0);
    int64_t a = py_to_int(args[0]);
    int64_t b = py_to_int(args[1]);
    if (a == 0 || b == 0)
        return py_new_int(0);
    return py_new_int((a * b) / 1);
}

static PyObject *pyrandom_random(PyObject *self, PyObject **args, size_t nargs)
{
    (void)self;
    (void)args;
    (void)nargs;
    uint32_t t = get_ticks();
    double r = (double)(t % 10000) / 10000.0;
    return py_new_float(r);
}

static PyObject *pyrandom_randint(PyObject *self, PyObject **args, size_t nargs)
{
    (void)self;
    if (nargs < 2)
        return py_new_int(0);
    int64_t a = py_to_int(args[0]);
    int64_t b = py_to_int(args[1]);
    uint32_t t = get_ticks();
    int64_t r = a + (int64_t)((t * 1103515245 + 12345) % (b - a + 1));
    return py_new_int(r);
}

static PyObject *pyrandom_choice(PyObject *self, PyObject **args, size_t nargs)
{
    (void)self;
    if (nargs < 1)
        return py_new_int(0);
    PyObject *seq = args[0];
    if (seq->type == PY_LIST && seq->val.list_val.size > 0)
    {
        uint32_t t = get_ticks();
        size_t idx = t % seq->val.list_val.size;
        Py_INCREF(seq->val.list_val.items[idx]);
        return seq->val.list_val.items[idx];
    }
    return py_new_int(0);
}

static PyObject *pyrandom_seed(PyObject *self, PyObject **args, size_t nargs)
{
    (void)self;
    (void)args;
    (void)nargs;
    return py_new_int(0);
}

static PyObject *pyrandom_shuffle(PyObject *self, PyObject **args, size_t nargs)
{
    (void)self;
    (void)args;
    (void)nargs;
    return py_new_int(0);
}

static PyObject *pysys_version(PyObject *self, PyObject **args, size_t nargs)
{
    (void)self;
    (void)args;
    (void)nargs;
    return py_new_str("3.14.0");
}

static PyObject *pysys_version_info(PyObject *self, PyObject **args, size_t nargs)
{
    (void)self;
    (void)args;
    (void)nargs;
    PyObject *result = py_new_tuple(5);
    result->val.tuple_val.items[0] = py_new_int(3);
    result->val.tuple_val.items[1] = py_new_int(14);
    result->val.tuple_val.items[2] = py_new_int(0);
    result->val.tuple_val.items[3] = py_new_str("final");
    result->val.tuple_val.items[4] = py_new_int(0);
    return result;
}

static PyObject *pysys_platform(PyObject *self, PyObject **args, size_t nargs)
{
    (void)self;
    (void)args;
    (void)nargs;
    return py_new_str("aurion-os");
}

static PyObject *pysys_executable(PyObject *self, PyObject **args, size_t nargs)
{
    (void)self;
    (void)args;
    (void)nargs;
    return py_new_str("/AurionOS/python");
}

static PyObject *pysys_argv(PyObject *self, PyObject **args, size_t nargs)
{
    (void)self;
    (void)args;
    (void)nargs;
    PyObject *result = py_new_list(0);
    PyList_Append(result, py_new_str("python"));
    return result;
}

static PyObject *pysys_getrecursionlimit(PyObject *self, PyObject **args, size_t nargs)
{
    (void)self;
    (void)args;
    (void)nargs;
    return py_new_int(1000);
}

static PyObject *pysys_setrecursionlimit(PyObject *self, PyObject **args, size_t nargs)
{
    (void)self;
    (void)args;
    (void)nargs;
    return py_new_int(0);
}

static PyObject *pysys_getsizeof(PyObject *self, PyObject **args, size_t nargs)
{
    (void)self;
    (void)args;
    (void)nargs;
    return py_new_int(64);
}

static PyObject *pysys_exit(PyObject *self, PyObject **args, size_t nargs)
{
    (void)self;
    (void)args;
    (void)nargs;
    return py_new_int(0);
}

static PyObject *pysys_stdout(PyObject *self, PyObject **args, size_t nargs)
{
    (void)self;
    (void)args;
    (void)nargs;
    return py_new_str("<stdout>");
}

static PyObject *pysys_stderr(PyObject *self, PyObject **args, size_t nargs)
{
    (void)self;
    (void)args;
    (void)nargs;
    return py_new_str("<stderr>");
}

static PyObject *pysys_stdin(PyObject *self, PyObject **args, size_t nargs)
{
    (void)self;
    (void)args;
    (void)nargs;
    return py_new_str("<stdin>");
}

void py_init_stdlib(void)
{
    PyObject *os_mod = py_new_module(py_new_str("os"));
    PyObject *sys_mod = py_new_module(py_new_str("sys"));
    PyObject *time_mod = py_new_module(py_new_str("time"));
    PyObject *math_mod = py_new_module(py_new_str("math"));
    PyObject *random_mod = py_new_module(py_new_str("random"));

    PyDict_SetItemString(os_mod->val.type_val.attrs, "name", py_new_str("AurionOS"));
    PyDict_SetItemString(os_mod->val.type_val.attrs, "version", py_new_str("1.1 Beta"));
    PyDict_SetItemString(sys_mod->val.type_val.attrs, "version", py_new_str("3.14.0"));
    PyDict_SetItemString(math_mod->val.type_val.attrs, "pi", py_new_float(3.141592653589793));
    PyDict_SetItemString(math_mod->val.type_val.attrs, "e", py_new_float(2.718281828459045));

    Py_INCREF(os_mod);
    Py_INCREF(sys_mod);
    Py_INCREF(time_mod);
    Py_INCREF(math_mod);
    Py_INCREF(random_mod);
}
