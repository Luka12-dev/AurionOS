#include "aurion_python.h"
#include <stddef.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include <ctype.h>
#include <setjmp.h>
#include <time.h>

#define PY_MAX_FRAME_DEPTH 64
#define PY_STACK_SIZE 1024
#define PY_GC_THRESHOLD 700

static PyObject *py_globals = NULL;
static PyObject *py_builtins = NULL;
static jmp_buf py_jmp_buf;
static int py_jmp_set = 0;
static PyObject *py_current_exception = NULL;

PyObject *py_none = NULL;
PyObject *py_true = NULL;
PyObject *py_false = NULL;

static PyObject *py_empty_string = NULL;
static PyObject *py_empty_tuple = NULL;
static PyObject *py_ellipsis_obj = NULL;

#define PyExc_BaseException ((PyObject *)1)
#define PyExc_Exception ((PyObject *)2)
#define PyExc_TypeError ((PyObject *)3)
#define PyExc_AttributeError ((PyObject *)10)
#define PyExc_IndexError ((PyObject *)4)
#define PyExc_KeyError ((PyObject *)5)
#define PyExc_ValueError ((PyObject *)6)
#define PyExc_ZeroDivisionError ((PyObject *)7)
#define PyExc_RuntimeError ((PyObject *)8)
#define PyExc_StopIteration ((PyObject *)9)

static const char *py_type_names[] = {
    "None", "int", "float", "bool", "str", "bytes", "list", "dict",
    "tuple", "set", "function", "cfunction", "module", "type", "object",
    "Exception", "bytesio", "file", "frame", "coroutine", "generator"};

PyObject *py_new_int(int64_t val)
{
    PyObject *o = (PyObject *)malloc(sizeof(PyObject));
    if (!o)
        return NULL;
    o->type = PY_INT;
    o->refcount = 1;
    o->val.int_val = val;
    return o;
}

PyObject *py_new_float(double val)
{
    PyObject *o = (PyObject *)malloc(sizeof(PyObject));
    if (!o)
        return NULL;
    o->type = PY_FLOAT;
    o->refcount = 1;
    o->val.float_val = val;
    return o;
}

PyObject *py_new_bool(bool val)
{
    PyObject *o = val ? py_true : py_false;
    o->refcount++;
    return o;
}

PyObject *py_new_str(const char *s) { return py_new_strn(s, s ? strlen(s) : 0); }

PyObject *py_new_strn(const char *s, size_t len)
{
    PyObject *o = (PyObject *)malloc(sizeof(PyObject));
    if (!o)
        return NULL;
    o->type = PY_STR;
    o->refcount = 1;
    o->val.str_val.data = (char *)malloc(len + 1);
    if (!o->val.str_val.data)
    {
        free(o);
        return NULL;
    }
    memcpy(o->val.str_val.data, s, len);
    o->val.str_val.data[len] = '\0';
    o->val.str_val.len = len;
    return o;
}

PyObject *py_new_bytes(const uint8_t *data, size_t len)
{
    PyObject *o = (PyObject *)malloc(sizeof(PyObject));
    if (!o)
        return NULL;
    o->type = PY_BYTES;
    o->refcount = 1;
    o->val.bytes_val.data = (uint8_t *)malloc(len);
    if (!o->val.bytes_val.data)
    {
        free(o);
        return NULL;
    }
    memcpy(o->val.bytes_val.data, data, len);
    o->val.bytes_val.len = len;
    return o;
}

PyObject *py_new_list(size_t capacity)
{
    PyObject *o = (PyObject *)malloc(sizeof(PyObject));
    if (!o)
        return NULL;
    o->type = PY_LIST;
    o->refcount = 1;
    size_t cap = capacity > 0 ? capacity : 4;
    o->val.list_val.items = (PyObject **)malloc(sizeof(PyObject *) * cap);
    o->val.list_val.size = 0;
    o->val.list_val.capacity = cap;
    return o;
}

PyObject *py_new_dict(void)
{
    PyObject *o = (PyObject *)malloc(sizeof(PyObject));
    if (!o)
        return NULL;
    o->type = PY_DICT;
    o->refcount = 1;
    o->val.dict_val.keys = NULL;
    o->val.dict_val.values = NULL;
    o->val.dict_val.size = 0;
    o->val.dict_val.capacity = 0;
    return o;
}

PyObject *py_new_tuple(size_t size)
{
    PyObject *o = (PyObject *)malloc(sizeof(PyObject));
    if (!o)
        return NULL;
    o->type = PY_TUPLE;
    o->refcount = 1;
    o->val.tuple_val.items = (PyObject **)calloc(size, sizeof(PyObject *));
    o->val.tuple_val.size = size;
    o->val.tuple_val.capacity = size;
    return o;
}

PyObject *py_new_set(void)
{
    PyObject *o = (PyObject *)malloc(sizeof(PyObject));
    if (!o)
        return NULL;
    o->type = PY_SET;
    o->refcount = 1;
    o->val.set_val.items = (PyObject **)calloc(8, sizeof(PyObject *));
    o->val.set_val.size = 0;
    o->val.set_val.capacity = 8;
    return o;
}

PyObject *py_new_function(PyObject *name, PyObject *globals, uint8_t *bytecode, size_t bytecode_len, size_t nlocals, size_t nstack)
{
    PyObject *o = (PyObject *)malloc(sizeof(PyObject));
    if (!o)
        return NULL;
    o->type = PY_FUNCTION;
    o->refcount = 1;
    o->val.function_val.name = name;
    Py_INCREF(name);
    o->val.function_val.globals = globals;
    Py_INCREF(globals);
    o->val.function_val.locals = NULL;
    o->val.function_val.bytecode = bytecode;
    o->val.function_val.bytecode_len = bytecode_len;
    o->val.function_val.nlocals = nlocals;
    o->val.function_val.nstack = nstack;
    o->val.function_val.cells = (PyObject **)calloc(nlocals, sizeof(PyObject *));
    return o;
}

PyObject *py_new_cfunction(void *func, PyObject *module, const char *name)
{
    PyObject *o = (PyObject *)malloc(sizeof(PyObject));
    if (!o)
        return NULL;
    o->type = PY_CFUNCTION;
    o->refcount = 1;
    o->val.cfunction_val.func = func;
    o->val.cfunction_val.module = module;
    Py_INCREF(module);
    (void)name;
    return o;
}

PyObject *py_new_type(PyObject *name, PyTypeTag instance_type, PyObject **attrs, size_t nattrs)
{
    PyObject *o = (PyObject *)malloc(sizeof(PyObject));
    if (!o)
        return NULL;
    o->type = PY_TYPE;
    o->refcount = 1;
    o->val.type_val.name = name;
    Py_INCREF(name);
    o->val.type_val.instance_type = instance_type;
    o->val.type_val.attrs = attrs;
    o->val.type_val.nattrs = nattrs;
    return o;
}

PyObject *py_new_exception(PyObject *type, PyObject *msg)
{
    PyObject *o = (PyObject *)malloc(sizeof(PyObject));
    if (!o)
        return NULL;
    o->type = PY_EXCEPTION;
    o->refcount = 1;
    o->val.exception_val.exc_type = type;
    Py_INCREF(type);
    o->val.exception_val.msg = msg;
    Py_INCREF(msg);
    return o;
}

PyObject *py_new_module(PyObject *name)
{
    PyObject *o = (PyObject *)malloc(sizeof(PyObject));
    if (!o)
        return NULL;
    o->type = PY_MODULE;
    o->refcount = 1;
    o->val.type_val.name = name;
    Py_INCREF(name);
    o->val.type_val.attrs = (PyObject **)calloc(16, sizeof(PyObject *));
    o->val.type_val.nattrs = 0;
    o->val.type_val.instance_type = PY_OBJECT;
    return o;
}

PyObject *py_new_bytesio(void)
{
    PyObject *o = (PyObject *)malloc(sizeof(PyObject));
    if (!o)
        return NULL;
    o->type = PY_BYTESIO;
    o->refcount = 1;
    o->val.bytesio_val.buf = (uint8_t *)malloc(256);
    o->val.bytesio_val.len = 256;
    o->val.bytesio_val.pos = 0;
    o->val.bytesio_val.closed = false;
    return o;
}

PyObject *py_new_file(void *file, const char *mode, const char *name)
{
    PyObject *o = (PyObject *)malloc(sizeof(PyObject));
    if (!o)
        return NULL;
    o->type = PY_FILE;
    o->refcount = 1;
    o->val.file_val.file = file;
    o->val.file_val.mode = strdup(mode);
    o->val.file_val.name = strdup(name);
    return o;
}

void py_dealloc(PyObject *o)
{
    if (!o)
        return;
    switch (o->type)
    {
    case PY_STR:
    case PY_EXCEPTION:
        free(o->val.str_val.data);
        break;
    case PY_BYTES:
        free(o->val.bytes_val.data);
        break;
    case PY_LIST:
        for (size_t i = 0; i < o->val.list_val.size; i++)
            Py_DECREF(o->val.list_val.items[i]);
        free(o->val.list_val.items);
        break;
    case PY_DICT:
        for (size_t i = 0; i < o->val.dict_val.size; i++)
        {
            Py_DECREF(o->val.dict_val.keys[i]);
            Py_DECREF(o->val.dict_val.values[i]);
        }
        free(o->val.dict_val.keys);
        free(o->val.dict_val.values);
        break;
    case PY_TUPLE:
        for (size_t i = 0; i < o->val.tuple_val.size; i++)
            Py_DECREF(o->val.tuple_val.items[i]);
        free(o->val.tuple_val.items);
        break;
    case PY_SET:
        for (size_t i = 0; i < o->val.set_val.size; i++)
            Py_DECREF(o->val.set_val.items[i]);
        free(o->val.set_val.items);
        break;
    case PY_FUNCTION:
        Py_DECREF(o->val.function_val.name);
        Py_DECREF(o->val.function_val.globals);
        free(o->val.function_val.bytecode);
        free(o->val.function_val.cells);
        break;
    case PY_CFUNCTION:
        Py_DECREF(o->val.cfunction_val.module);
        break;
    case PY_TYPE:
    case PY_MODULE:
        Py_DECREF(o->val.type_val.name);
        for (size_t i = 0; i < o->val.type_val.nattrs; i++)
            Py_DECREF(o->val.type_val.attrs[i]);
        free(o->val.type_val.attrs);
        break;
    case PY_BYTESIO:
        free(o->val.bytesio_val.buf);
        break;
    case PY_FILE:
        free(o->val.file_val.mode);
        free(o->val.file_val.name);
        break;
    default:
        break;
    }
    free(o);
}

static PyObject *py_make_builtin(const char *name, void *func_ptr)
{
    PyObject *f = (PyObject *)malloc(sizeof(PyObject));
    f->type = PY_CFUNCTION;
    f->refcount = 1;
    f->val.cfunction_val.func = func_ptr;
    f->val.cfunction_val.module = py_builtins;
    Py_INCREF(py_builtins);
    PyDict_SetItemString(py_builtins, name, f);
    Py_DECREF(f);
    return f;
}

void py_init(void)
{
    py_none = (PyObject *)calloc(1, sizeof(PyObject));
    py_none->type = PY_NONE;
    py_none->refcount = 1;

    py_true = (PyObject *)calloc(1, sizeof(PyObject));
    py_true->type = PY_BOOL;
    py_true->refcount = 1;
    py_true->val.bool_val = true;

    py_false = (PyObject *)calloc(1, sizeof(PyObject));
    py_false->type = PY_BOOL;
    py_false->refcount = 1;
    py_false->val.bool_val = false;

    py_empty_string = py_new_str("");
    py_empty_tuple = py_new_tuple(0);
    py_ellipsis_obj = (PyObject *)calloc(1, sizeof(PyObject));
    py_ellipsis_obj->type = PY_INT;
    py_ellipsis_obj->refcount = 1;
    py_ellipsis_obj->val.int_val = -1;

    py_globals = py_new_dict();
    py_builtins = py_new_dict();

    PyObject *exc_module = py_new_module(py_new_str("exceptions"));
    PyDict_SetItemString(py_builtins, "BaseException", (PyObject *)PyExc_BaseException);
    PyDict_SetItemString(py_builtins, "Exception", (PyObject *)PyExc_Exception);
    PyDict_SetItemString(py_builtins, "TypeError", (PyObject *)PyExc_TypeError);
    PyDict_SetItemString(py_builtins, "IndexError", (PyObject *)PyExc_IndexError);
    PyDict_SetItemString(py_builtins, "KeyError", (PyObject *)PyExc_KeyError);
    PyDict_SetItemString(py_builtins, "ValueError", (PyObject *)PyExc_ValueError);
    PyDict_SetItemString(py_builtins, "ZeroDivisionError", (PyObject *)PyExc_ZeroDivisionError);
    PyDict_SetItemString(py_builtins, "RuntimeError", (PyObject *)PyExc_RuntimeError);
    PyDict_SetItemString(py_builtins, "StopIteration", (PyObject *)PyExc_StopIteration);
    Py_DECREF(exc_module);

    PyObject *open_module = py_new_module(py_new_str("builtins"));
    PyDict_SetItemString(py_builtins, "__builtins__", open_module);
    Py_DECREF(open_module);
}

void py_shutdown(void)
{
    Py_DECREF(py_globals);
    Py_DECREF(py_builtins);
    Py_DECREF(py_empty_string);
    Py_DECREF(py_empty_tuple);
    Py_DECREF(py_none);
    Py_DECREF(py_true);
    Py_DECREF(py_false);
    Py_DECREF(py_ellipsis_obj);
}

bool py_is_true(PyObject *o)
{
    if (!o)
        return false;
    switch (o->type)
    {
    case PY_NONE:
        return false;
    case PY_BOOL:
        return o->val.bool_val;
    case PY_INT:
        return o->val.int_val != 0;
    case PY_FLOAT:
        return o->val.float_val != 0.0;
    case PY_STR:
        return o->val.str_val.len > 0;
    case PY_BYTES:
        return o->val.bytes_val.len > 0;
    case PY_LIST:
        return o->val.list_val.size > 0;
    case PY_DICT:
        return o->val.dict_val.size > 0;
    case PY_TUPLE:
        return o->val.tuple_val.size > 0;
    case PY_SET:
        return o->val.set_val.size > 0;
    default:
        return true;
    }
}

int64_t py_to_int(PyObject *o)
{
    if (!o)
        return 0;
    switch (o->type)
    {
    case PY_INT:
        return o->val.int_val;
    case PY_BOOL:
        return o->val.bool_val ? 1 : 0;
    case PY_FLOAT:
        return (int64_t)o->val.float_val;
    case PY_STR:
        return atoll(o->val.str_val.data);
    default:
        return 0;
    }
}

double py_to_float(PyObject *o)
{
    if (!o)
        return 0.0;
    switch (o->type)
    {
    case PY_FLOAT:
        return o->val.float_val;
    case PY_INT:
        return (double)o->val.int_val;
    case PY_BOOL:
        return o->val.bool_val ? 1.0 : 0.0;
    case PY_STR:
        return atof(o->val.str_val.data);
    default:
        return 0.0;
    }
}

const char *py_str_cstr(PyObject *o)
{
    if (!o)
        return "";
    if (o->type == PY_STR)
        return o->val.str_val.data;
    return "";
}

PyObject *py_add(PyObject *a, PyObject *b)
{
    if (a->type == PY_INT && b->type == PY_INT)
        return py_new_int(a->val.int_val + b->val.int_val);
    if (a->type == PY_FLOAT || b->type == PY_FLOAT)
        return py_new_float(py_to_float(a) + py_to_float(b));
    if (a->type == PY_STR && b->type == PY_STR)
    {
        size_t len = a->val.str_val.len + b->val.str_val.len;
        char *s = (char *)malloc(len + 1);
        if (!s)
            return NULL;
        memcpy(s, a->val.str_val.data, a->val.str_val.len);
        memcpy(s + a->val.str_val.len, b->val.str_val.data, b->val.str_val.len);
        s[len] = '\0';
        PyObject *r = py_new_strn(s, len);
        free(s);
        return r;
    }
    if (a->type == PY_LIST && b->type == PY_LIST)
    {
        PyObject *r = py_new_list(a->val.list_val.size + b->val.list_val.size);
        for (size_t i = 0; i < a->val.list_val.size; i++)
        {
            r->val.list_val.items[r->val.list_val.size++] = a->val.list_val.items[i];
            Py_INCREF(a->val.list_val.items[i]);
        }
        for (size_t i = 0; i < b->val.list_val.size; i++)
        {
            r->val.list_val.items[r->val.list_val.size++] = b->val.list_val.items[i];
            Py_INCREF(b->val.list_val.items[i]);
        }
        return r;
    }
    if (a->type == PY_BYTES && b->type == PY_BYTES)
    {
        size_t len = a->val.bytes_val.len + b->val.bytes_val.len;
        uint8_t *data = (uint8_t *)malloc(len);
        if (!data)
            return NULL;
        memcpy(data, a->val.bytes_val.data, a->val.bytes_val.len);
        memcpy(data + a->val.bytes_val.len, b->val.bytes_val.data, b->val.bytes_val.len);
        PyObject *r = py_new_bytes(data, len);
        free(data);
        return r;
    }
    return py_new_exception(PyExc_TypeError, py_new_str("unsupported operand type(s) for +"));
}

PyObject *py_sub(PyObject *a, PyObject *b)
{
    if (a->type == PY_INT && b->type == PY_INT)
        return py_new_int(a->val.int_val - b->val.int_val);
    return py_new_float(py_to_float(a) - py_to_float(b));
}

PyObject *py_mul(PyObject *a, PyObject *b)
{
    if (a->type == PY_INT && b->type == PY_INT)
        return py_new_int(a->val.int_val * b->val.int_val);
    if (a->type == PY_INT && b->type == PY_STR)
    {
        int64_t n = a->val.int_val;
        if (n <= 0)
            return py_new_str("");
        size_t total_len = b->val.str_val.len * n;
        char *s = (char *)malloc(total_len + 1);
        if (!s)
            return NULL;
        for (int64_t i = 0; i < n; i++)
            memcpy(s + i * b->val.str_val.len, b->val.str_val.data, b->val.str_val.len);
        s[total_len] = '\0';
        PyObject *r = py_new_strn(s, total_len);
        free(s);
        return r;
    }
    if (a->type == PY_STR && b->type == PY_INT)
        return py_mul(b, a);
    return py_new_float(py_to_float(a) * py_to_float(b));
}

PyObject *py_div(PyObject *a, PyObject *b)
{
    double div = py_to_float(b);
    if (div == 0.0)
        return py_new_exception(PyExc_ZeroDivisionError, py_new_str("division by zero"));
    return py_new_float(py_to_float(a) / div);
}

PyObject *py_floordiv(PyObject *a, PyObject *b)
{
    double div = py_to_float(b);
    if (div == 0.0)
        return py_new_exception(PyExc_ZeroDivisionError, py_new_str("floor division by zero"));
    return py_new_int((int64_t)(floor(py_to_float(a) / div)));
}

PyObject *py_mod(PyObject *a, PyObject *b)
{
    if (a->type == PY_INT && b->type == PY_INT)
    {
        int64_t div = b->val.int_val;
        if (div == 0)
            return py_new_exception(PyExc_ZeroDivisionError, py_new_str("modulo by zero"));
        return py_new_int(a->val.int_val % div);
    }
    double div = py_to_float(b);
    if (div == 0.0)
        return py_new_exception(PyExc_ZeroDivisionError, py_new_str("modulo by zero"));
    return py_new_float(fmod(py_to_float(a), div));
}

PyObject *py_pow(PyObject *a, PyObject *b)
{
    if (a->type == PY_INT && b->type == PY_INT)
    {
        int64_t base = a->val.int_val;
        int64_t exp = b->val.int_val;
        if (exp < 0)
            return py_new_float(pow((double)base, (double)exp));
        int64_t result = 1;
        while (exp > 0)
        {
            if (exp & 1)
                result *= base;
            base *= base;
            exp >>= 1;
        }
        return py_new_int(result);
    }
    return py_new_float(pow(py_to_float(a), py_to_float(b)));
}

PyObject *py_neg(PyObject *a)
{
    if (a->type == PY_INT)
        return py_new_int(-a->val.int_val);
    return py_new_float(-py_to_float(a));
}

PyObject *py_pos(PyObject *a)
{
    if (a->type == PY_INT)
        return py_new_int(a->val.int_val);
    return py_new_float(py_to_float(a));
}

PyObject *py_invert(PyObject *a)
{
    if (a->type == PY_INT)
        return py_new_int(~a->val.int_val);
    return NULL;
}

PyObject *py_lshift(PyObject *a, PyObject *b)
{
    if (a->type == PY_INT && b->type == PY_INT)
        return py_new_int(a->val.int_val << b->val.int_val);
    return NULL;
}

PyObject *py_rshift(PyObject *a, PyObject *b)
{
    if (a->type == PY_INT && b->type == PY_INT)
        return py_new_int(a->val.int_val >> b->val.int_val);
    return NULL;
}

PyObject *py_and(PyObject *a, PyObject *b)
{
    if (a->type == PY_INT && b->type == PY_INT)
        return py_new_int(a->val.int_val & b->val.int_val);
    return py_new_bool(py_is_true(a) && py_is_true(b));
}

PyObject *py_or(PyObject *a, PyObject *b)
{
    if (a->type == PY_INT && b->type == PY_INT)
        return py_new_int(a->val.int_val | b->val.int_val);
    return py_new_bool(py_is_true(a) || py_is_true(b));
}

PyObject *py_xor(PyObject *a, PyObject *b)
{
    if (a->type == PY_INT && b->type == PY_INT)
        return py_new_int(a->val.int_val ^ b->val.int_val);
    return py_new_bool(py_is_true(a) != py_is_true(b));
}

bool py_less(PyObject *a, PyObject *b)
{
    if (a->type == PY_INT && b->type == PY_INT)
        return a->val.int_val < b->val.int_val;
    if (a->type == PY_FLOAT || b->type == PY_FLOAT)
        return py_to_float(a) < py_to_float(b);
    if (a->type == PY_STR && b->type == PY_STR)
        return strcmp(a->val.str_val.data, b->val.str_val.data) < 0;
    return false;
}

bool py_less_equal(PyObject *a, PyObject *b)
{
    if (a->type == PY_INT && b->type == PY_INT)
        return a->val.int_val <= b->val.int_val;
    if (a->type == PY_FLOAT || b->type == PY_FLOAT)
        return py_to_float(a) <= py_to_float(b);
    if (a->type == PY_STR && b->type == PY_STR)
        return strcmp(a->val.str_val.data, b->val.str_val.data) <= 0;
    return false;
}

bool py_greater(PyObject *a, PyObject *b)
{
    if (a->type == PY_INT && b->type == PY_INT)
        return a->val.int_val > b->val.int_val;
    if (a->type == PY_FLOAT || b->type == PY_FLOAT)
        return py_to_float(a) > py_to_float(b);
    if (a->type == PY_STR && b->type == PY_STR)
        return strcmp(a->val.str_val.data, b->val.str_val.data) > 0;
    return false;
}

bool py_greater_equal(PyObject *a, PyObject *b)
{
    if (a->type == PY_INT && b->type == PY_INT)
        return a->val.int_val >= b->val.int_val;
    if (a->type == PY_FLOAT || b->type == PY_FLOAT)
        return py_to_float(a) >= py_to_float(b);
    if (a->type == PY_STR && b->type == PY_STR)
        return strcmp(a->val.str_val.data, b->val.str_val.data) >= 0;
    return false;
}

bool py_equal(PyObject *a, PyObject *b)
{
    if (a->type != b->type)
    {
        if (a->type == PY_BOOL && b->type == PY_INT)
            return (a->val.bool_val ? 1 : 0) == b->val.int_val;
        if (b->type == PY_BOOL && a->type == PY_INT)
            return (b->val.bool_val ? 1 : 0) == a->val.int_val;
        return false;
    }
    switch (a->type)
    {
    case PY_NONE:
        return true;
    case PY_INT:
        return a->val.int_val == b->val.int_val;
    case PY_FLOAT:
        return fabs(a->val.float_val - b->val.float_val) < 1e-9;
    case PY_BOOL:
        return a->val.bool_val == b->val.bool_val;
    case PY_STR:
        return a->val.str_val.len == b->val.str_val.len && memcmp(a->val.str_val.data, b->val.str_val.data, a->val.str_val.len) == 0;
    case PY_BYTES:
        return a->val.bytes_val.len == b->val.bytes_val.len && memcmp(a->val.bytes_val.data, b->val.bytes_val.data, a->val.bytes_val.len) == 0;
    default:
        return false;
    }
}

bool py_not_equal(PyObject *a, PyObject *b) { return !py_equal(a, b); }

PyObject *py_getitem(PyObject *o, PyObject *key)
{
    if (o->type == PY_LIST)
    {
        if (key->type != PY_INT)
            return NULL;
        int64_t idx = key->val.int_val;
        if (idx < 0)
            idx += o->val.list_val.size;
        if (idx < 0 || (size_t)idx >= o->val.list_val.size)
            return py_new_exception(PyExc_IndexError, py_new_str("list index out of range"));
        return o->val.list_val.items[idx];
    }
    if (o->type == PY_DICT)
    {
        for (size_t i = 0; i < o->val.dict_val.size; i++)
            if (py_equal(o->val.dict_val.keys[i], key))
                return o->val.dict_val.values[i];
        return NULL;
    }
    if (o->type == PY_TUPLE)
    {
        if (key->type != PY_INT)
            return NULL;
        int64_t idx = key->val.int_val;
        if (idx < 0)
            idx += o->val.tuple_val.size;
        if (idx < 0 || (size_t)idx >= o->val.tuple_val.size)
            return py_new_exception(PyExc_IndexError, py_new_str("tuple index out of range"));
        return o->val.tuple_val.items[idx];
    }
    if (o->type == PY_STR)
    {
        if (key->type != PY_INT)
            return NULL;
        int64_t idx = key->val.int_val;
        if (idx < 0)
            idx += o->val.str_val.len;
        if (idx < 0 || (size_t)idx >= o->val.str_val.len)
            return py_new_exception(PyExc_IndexError, py_new_str("string index out of range"));
        char s[2] = {o->val.str_val.data[idx], '\0'};
        return py_new_str(s);
    }
    if (o->type == PY_BYTES)
    {
        if (key->type != PY_INT)
            return NULL;
        int64_t idx = key->val.int_val;
        if (idx < 0)
            idx += o->val.bytes_val.len;
        if (idx < 0 || (size_t)idx >= o->val.bytes_val.len)
            return py_new_exception(PyExc_IndexError, py_new_str("bytes index out of range"));
        return py_new_int(o->val.bytes_val.data[idx]);
    }
    return NULL;
}

int py_setitem(PyObject *o, PyObject *key, PyObject *val)
{
    if (o->type == PY_LIST)
    {
        if (key->type != PY_INT)
            return -1;
        int64_t idx = key->val.int_val;
        if (idx < 0)
            idx += o->val.list_val.size;
        if (idx < 0 || (size_t)idx >= o->val.list_val.size)
            return -1;
        Py_DECREF(o->val.list_val.items[idx]);
        o->val.list_val.items[idx] = val;
        Py_INCREF(val);
        return 0;
    }
    if (o->type == PY_DICT)
    {
        for (size_t i = 0; i < o->val.dict_val.size; i++)
        {
            if (py_equal(o->val.dict_val.keys[i], key))
            {
                Py_DECREF(o->val.dict_val.values[i]);
                o->val.dict_val.values[i] = val;
                Py_INCREF(val);
                return 0;
            }
        }
        if (o->val.dict_val.size >= o->val.dict_val.capacity)
        {
            size_t new_cap = o->val.dict_val.capacity * 2 + 4;
            PyObject **new_keys = (PyObject **)realloc(o->val.dict_val.keys, new_cap * sizeof(PyObject *));
            PyObject **new_values = (PyObject **)realloc(o->val.dict_val.values, new_cap * sizeof(PyObject *));
            if (!new_keys || !new_values)
                return -1;
            o->val.dict_val.keys = new_keys;
            o->val.dict_val.values = new_values;
            o->val.dict_val.capacity = new_cap;
        }
        o->val.dict_val.keys[o->val.dict_val.size] = key;
        o->val.dict_val.values[o->val.dict_val.size] = val;
        o->val.dict_val.size++;
        Py_INCREF(key);
        Py_INCREF(val);
        return 0;
    }
    return -1;
}

int py_delitem(PyObject *o, PyObject *key)
{
    if (o->type == PY_LIST)
    {
        if (key->type != PY_INT)
            return -1;
        int64_t idx = key->val.int_val;
        if (idx < 0)
            idx += o->val.list_val.size;
        if (idx < 0 || (size_t)idx >= o->val.list_val.size)
            return -1;
        for (size_t i = idx; i < o->val.list_val.size - 1; i++)
            o->val.list_val.items[i] = o->val.list_val.items[i + 1];
        o->val.list_val.size--;
        return 0;
    }
    if (o->type == PY_DICT)
    {
        for (size_t i = 0; i < o->val.dict_val.size; i++)
        {
            if (py_equal(o->val.dict_val.keys[i], key))
            {
                Py_DECREF(o->val.dict_val.keys[i]);
                Py_DECREF(o->val.dict_val.values[i]);
                for (size_t j = i; j < o->val.dict_val.size - 1; j++)
                {
                    o->val.dict_val.keys[j] = o->val.dict_val.keys[j + 1];
                    o->val.dict_val.values[j] = o->val.dict_val.values[j + 1];
                }
                o->val.dict_val.size--;
                return 0;
            }
        }
        return -1;
    }
    return -1;
}

PyObject *py_getattr(PyObject *o, const char *name)
{
    if (o->type == PY_MODULE || o->type == PY_TYPE)
    {
        for (size_t i = 0; i < o->val.type_val.nattrs; i++)
        {
            if (strcmp(py_str_cstr(o->val.type_val.attrs[i * 2]), name) == 0)
                return o->val.type_val.attrs[i * 2 + 1];
        }
        return NULL;
    }
    return NULL;
}

int py_setattr(PyObject *o, const char *name, PyObject *val)
{
    if (o->type == PY_MODULE || o->type == PY_TYPE)
    {
        for (size_t i = 0; i < o->val.type_val.nattrs; i++)
        {
            if (strcmp(py_str_cstr(o->val.type_val.attrs[i * 2]), name) == 0)
            {
                Py_DECREF(o->val.type_val.attrs[i * 2 + 1]);
                o->val.type_val.attrs[i * 2 + 1] = val;
                Py_INCREF(val);
                return 0;
            }
        }
        if (o->val.type_val.nattrs % 8 == 0)
        {
            o->val.type_val.attrs = (PyObject **)realloc(o->val.type_val.attrs, (o->val.type_val.nattrs + 8) * 2 * sizeof(PyObject *));
        }
        o->val.type_val.attrs[o->val.type_val.nattrs * 2] = py_new_str(name);
        o->val.type_val.attrs[o->val.type_val.nattrs * 2 + 1] = val;
        o->val.type_val.nattrs++;
        Py_INCREF(val);
        return 0;
    }
    return -1;
}

size_t py_hash(PyObject *o)
{
    switch (o->type)
    {
    case PY_INT:
        return (size_t)o->val.int_val;
    case PY_STR:
    {
        size_t h = 0;
        for (size_t i = 0; i < o->val.str_val.len; i++)
            h = h * 31 + (unsigned char)o->val.str_val.data[i];
        return h;
    }
    case PY_TUPLE:
    {
        size_t h = 0;
        for (size_t i = 0; i < o->val.tuple_val.size; i++)
            h = h * 31 ^ py_hash(o->val.tuple_val.items[i]);
        return h;
    }
    case PY_BYTES:
    {
        size_t h = 0;
        for (size_t i = 0; i < o->val.bytes_val.len; i++)
            h = h * 31 + o->val.bytes_val.data[i];
        return h;
    }
    default:
        return (size_t)(intptr_t)o;
    }
}

PyObject *py_len(PyObject *o)
{
    switch (o->type)
    {
    case PY_STR:
        return py_new_int(o->val.str_val.len);
    case PY_BYTES:
        return py_new_int(o->val.bytes_val.len);
    case PY_LIST:
        return py_new_int(o->val.list_val.size);
    case PY_DICT:
        return py_new_int(o->val.dict_val.size);
    case PY_TUPLE:
        return py_new_int(o->val.tuple_val.size);
    case PY_SET:
        return py_new_int(o->val.set_val.size);
    default:
        return NULL;
    }
}

PyObject *py_contains(PyObject *o, PyObject *val)
{
    switch (o->type)
    {
    case PY_LIST:
    {
        for (size_t i = 0; i < o->val.list_val.size; i++)
            if (py_equal(o->val.list_val.items[i], val))
                return py_true;
        return py_false;
    }
    case PY_STR:
    {
        if (val->type != PY_STR)
            return py_false;
        return strstr(o->val.str_val.data, val->val.str_val.data) ? py_true : py_false;
    }
    case PY_DICT:
    {
        for (size_t i = 0; i < o->val.dict_val.size; i++)
            if (py_equal(o->val.dict_val.keys[i], val))
                return py_true;
        return py_false;
    }
    case PY_SET:
    {
        for (size_t i = 0; i < o->val.set_val.size; i++)
            if (py_equal(o->val.set_val.items[i], val))
                return py_true;
        return py_false;
    }
    default:
        return py_false;
    }
}

PyObject *py_iter(PyObject *o)
{
    if (o->type == PY_LIST || o->type == PY_TUPLE || o->type == PY_STR || o->type == PY_BYTES)
    {
        PyObject *it = (PyObject *)malloc(sizeof(PyObject));
        it->type = PY_OBJECT;
        it->refcount = 1;
        it->val.type_val.name = py_new_str("<iterator>");
        Py_INCREF(py_new_str("<iterator>"));
        it->val.type_val.attrs = (PyObject **)calloc(4, sizeof(PyObject *));
        it->val.type_val.attrs[0] = py_new_str("index");
        it->val.type_val.attrs[1] = py_new_int(0);
        it->val.type_val.nattrs = 1;
        it->val.type_val.instance_type = PY_OBJECT;
        return it;
    }
    return NULL;
}

PyObject *py_next(PyObject *o)
{
    (void)o;
    return NULL;
}

static const char *py_get_cfunc_name(PyObject *func)
{
    if (func->type != PY_CFUNCTION)
        return "";
    if (!func->val.cfunction_val.module)
        return "";
    if (func->val.cfunction_val.module->type != PY_MODULE)
        return "";
    return py_str_cstr(func->val.cfunction_val.module);
}

PyObject *py_call(PyObject *func, PyObject **args, size_t nargs)
{
    if (!func)
        return NULL;
    if (func->type == PY_CFUNCTION)
    {
        const char *name = py_get_cfunc_name(func);

        if (strstr(name, "print"))
        {
            for (size_t i = 0; i < nargs; i++)
            {
                if (i > 0)
                    printf(" ");
                if (args[i]->type == PY_STR)
                    printf("%s", args[i]->val.str_val.data);
                else if (args[i]->type == PY_INT)
                    printf("%lld", (long long)args[i]->val.int_val);
                else if (args[i]->type == PY_FLOAT)
                    printf("%g", args[i]->val.float_val);
                else if (args[i]->type == PY_BOOL)
                    printf("%s", args[i]->val.bool_val ? "True" : "False");
                else if (args[i]->type == PY_NONE)
                    printf("None");
                else
                    printf("<%s>", py_type_names[args[i]->type]);
            }
            printf("\n");
            return py_none;
        }
        if (strstr(name, "len"))
        {
            if (nargs < 1)
                return py_new_exception(PyExc_TypeError, py_new_str("len() takes exactly one argument"));
            return py_len(args[0]);
        }
        if (strstr(name, "range"))
        {
            int64_t start = 0, stop = 0, step = 1;
            if (nargs == 1)
                stop = py_to_int(args[0]);
            else if (nargs >= 2)
            {
                start = py_to_int(args[0]);
                stop = py_to_int(args[1]);
                if (nargs >= 3)
                    step = py_to_int(args[2]);
            }
            PyObject *result = py_new_list(0);
            int64_t val = start;
            if (step > 0)
            {
                while (val < stop)
                {
                    PyList_Append(result, py_new_int(val));
                    val += step;
                }
            }
            else if (step < 0)
            {
                while (val > stop)
                {
                    PyList_Append(result, py_new_int(val));
                    val += step;
                }
            }
            return result;
        }
        if (strstr(name, "str"))
        {
            if (nargs < 1)
                return py_new_str("");
            PyObject *arg = args[0];
            if (arg->type == PY_STR)
            {
                Py_INCREF(arg);
                return arg;
            }
            if (arg->type == PY_INT)
            {
                char buf[32];
                snprintf(buf, sizeof(buf), "%lld", (long long)arg->val.int_val);
                return py_new_str(buf);
            }
            if (arg->type == PY_FLOAT)
            {
                char buf[64];
                snprintf(buf, sizeof(buf), "%g", arg->val.float_val);
                return py_new_str(buf);
            }
            if (arg->type == PY_BOOL)
                return py_new_str(arg->val.bool_val ? "True" : "False");
            if (arg->type == PY_NONE)
                return py_new_str("None");
            return py_new_str("<object>");
        }
        if (strstr(name, "int"))
        {
            if (nargs < 1)
                return py_new_int(0);
            PyObject *arg = args[0];
            if (arg->type == PY_INT)
            {
                Py_INCREF(arg);
                return arg;
            }
            if (arg->type == PY_STR)
                return py_new_int(atoll(arg->val.str_val.data));
            if (arg->type == PY_FLOAT)
                return py_new_int((int64_t)arg->val.float_val);
            return py_new_int(0);
        }
        if (strstr(name, "float"))
        {
            if (nargs < 1)
                return py_new_float(0.0);
            return py_new_float(py_to_float(args[0]));
        }
        if (strstr(name, "list"))
        {
            if (nargs < 1)
                return py_new_list(0);
            PyObject *arg = args[0];
            if (arg->type == PY_LIST)
            {
                PyObject *result = py_new_list(arg->val.list_val.size);
                for (size_t i = 0; i < arg->val.list_val.size; i++)
                {
                    result->val.list_val.items[i] = arg->val.list_val.items[i];
                    Py_INCREF(arg->val.list_val.items[i]);
                    result->val.list_val.size++;
                }
                return result;
            }
            if (arg->type == PY_STR)
            {
                PyObject *result = py_new_list(arg->val.str_val.len);
                for (size_t i = 0; i < arg->val.str_val.len; i++)
                {
                    char s[2] = {arg->val.str_val.data[i], '\0'};
                    result->val.list_val.items[i] = py_new_str(s);
                    result->val.list_val.size++;
                }
                return result;
            }
            return py_new_list(0);
        }
        if (strstr(name, "dict"))
            return py_new_dict();
        if (strstr(name, "tuple"))
        {
            if (nargs < 1)
                return py_new_tuple(0);
            PyObject *arg = args[0];
            if (arg->type == PY_LIST)
            {
                PyObject *result = py_new_tuple(arg->val.list_val.size);
                for (size_t i = 0; i < arg->val.list_val.size; i++)
                {
                    result->val.tuple_val.items[i] = arg->val.list_val.items[i];
                    Py_INCREF(arg->val.list_val.items[i]);
                }
                return result;
            }
            return py_new_tuple(0);
        }
        if (strstr(name, "set"))
            return py_new_set();
        if (strstr(name, "type"))
        {
            if (nargs < 1)
                return py_new_str("None");
            return py_new_str(py_type_names[args[0]->type]);
        }
        if (strstr(name, "abs"))
        {
            if (nargs < 1)
                return NULL;
            PyObject *arg = args[0];
            if (arg->type == PY_INT)
            {
                int64_t v = arg->val.int_val;
                return py_new_int(v < 0 ? -v : v);
            }
            if (arg->type == PY_FLOAT)
            {
                double v = arg->val.float_val;
                return py_new_float(v < 0 ? -v : v);
            }
            return NULL;
        }
        if (strstr(name, "min"))
        {
            if (nargs < 1)
                return NULL;
            PyObject *result = args[0];
            for (size_t i = 1; i < nargs; i++)
                if (py_less(args[i], result))
                    result = args[i];
            Py_INCREF(result);
            return result;
        }
        if (strstr(name, "max"))
        {
            if (nargs < 1)
                return NULL;
            PyObject *result = args[0];
            for (size_t i = 1; i < nargs; i++)
                if (py_greater(args[i], result))
                    result = args[i];
            Py_INCREF(result);
            return result;
        }
        if (strstr(name, "sum"))
        {
            if (nargs < 1)
                return py_new_int(0);
            int64_t total = 0;
            PyObject *iterable = args[0];
            if (iterable->type == PY_LIST)
                for (size_t i = 0; i < iterable->val.list_val.size; i++)
                    total += py_to_int(iterable->val.list_val.items[i]);
            return py_new_int(total);
        }
        if (strstr(name, "sorted"))
        {
            if (nargs < 1)
                return py_new_list(0);
            PyObject *result = py_new_list(0);
            PyObject *iterable = args[0];
            if (iterable->type == PY_LIST)
                for (size_t i = 0; i < iterable->val.list_val.size; i++)
                    PyList_Append(result, iterable->val.list_val.items[i]);
            return result;
        }
        if (strstr(name, "reversed"))
        {
            if (nargs < 1)
                return py_new_list(0);
            PyObject *result = py_new_list(0);
            PyObject *iterable = args[0];
            if (iterable->type == PY_LIST)
                for (size_t i = iterable->val.list_val.size; i > 0; i--)
                    PyList_Append(result, iterable->val.list_val.items[i - 1]);
            return result;
        }
        if (strstr(name, "enumerate"))
        {
            if (nargs < 1)
                return py_new_list(0);
            PyObject *result = py_new_list(0);
            PyObject *iterable = args[0];
            if (iterable->type == PY_LIST)
            {
                for (size_t i = 0; i < iterable->val.list_val.size; i++)
                {
                    PyObject *pair = py_new_tuple(2);
                    pair->val.tuple_val.items[0] = py_new_int(i);
                    pair->val.tuple_val.items[1] = iterable->val.list_val.items[i];
                    Py_INCREF(iterable->val.list_val.items[i]);
                    PyList_Append(result, pair);
                }
            }
            return result;
        }
        if (strstr(name, "zip"))
        {
            PyObject *result = py_new_list(0);
            if (nargs < 2)
                return result;
            PyObject *a = args[0], *b = args[1];
            if (a->type == PY_LIST && b->type == PY_LIST)
            {
                size_t max_i = a->val.list_val.size < b->val.list_val.size ? a->val.list_val.size : b->val.list_val.size;
                for (size_t i = 0; i < max_i; i++)
                {
                    PyObject *pair = py_new_tuple(2);
                    pair->val.tuple_val.items[0] = a->val.list_val.items[i];
                    pair->val.tuple_val.items[1] = b->val.list_val.items[i];
                    Py_INCREF(a->val.list_val.items[i]);
                    Py_INCREF(b->val.list_val.items[i]);
                    PyList_Append(result, pair);
                }
            }
            return result;
        }
        if (strstr(name, "map"))
        {
            PyObject *result = py_new_list(0);
            if (nargs < 2)
                return result;
            PyObject *func = args[0];
            PyObject *iterable = args[1];
            if (iterable->type == PY_LIST)
            {
                for (size_t i = 0; i < iterable->val.list_val.size; i++)
                {
                    PyObject *argv[1] = {iterable->val.list_val.items[i]};
                    PyObject *r = py_call(func, argv, 1);
                    PyList_Append(result, r);
                    Py_DECREF(r);
                }
            }
            return result;
        }
        if (strstr(name, "filter"))
        {
            PyObject *result = py_new_list(0);
            if (nargs < 2)
                return result;
            PyObject *func = args[0];
            PyObject *iterable = args[1];
            if (iterable->type == PY_LIST)
            {
                for (size_t i = 0; i < iterable->val.list_val.size; i++)
                {
                    PyObject *argv[1] = {iterable->val.list_val.items[i]};
                    PyObject *r = py_call(func, argv, 1);
                    if (py_is_true(r))
                        PyList_Append(result, iterable->val.list_val.items[i]);
                    Py_DECREF(r);
                }
            }
            return result;
        }
        if (strstr(name, "any"))
        {
            if (nargs < 1)
                return py_false;
            PyObject *iterable = args[0];
            if (iterable->type == PY_LIST)
            {
                for (size_t i = 0; i < iterable->val.list_val.size; i++)
                    if (py_is_true(iterable->val.list_val.items[i]))
                        return py_true;
            }
            return py_false;
        }
        if (strstr(name, "all"))
        {
            if (nargs < 1)
                return py_true;
            PyObject *iterable = args[0];
            if (iterable->type == PY_LIST)
            {
                for (size_t i = 0; i < iterable->val.list_val.size; i++)
                    if (!py_is_true(iterable->val.list_val.items[i]))
                        return py_false;
            }
            return py_true;
        }
        if (strstr(name, "round"))
        {
            if (nargs < 1)
                return py_new_int(0);
            double val = py_to_float(args[0]);
            return py_new_int((int64_t)(val + 0.5));
        }
        if (strstr(name, "pow"))
        {
            if (nargs < 2)
                return py_new_int(0);
            return py_pow(args[0], args[1]);
        }
        if (strstr(name, "divmod"))
        {
            if (nargs < 2)
                return py_new_tuple(0);
            PyObject *a = args[0], *b = args[1];
            PyObject *q = py_new_int(py_to_int(a) / py_to_int(b));
            PyObject *r = py_new_int(py_to_int(a) % py_to_int(b));
            PyObject *result = py_new_tuple(2);
            result->val.tuple_val.items[0] = q;
            result->val.tuple_val.items[1] = r;
            return result;
        }
        if (strstr(name, "chr"))
        {
            if (nargs < 1)
                return py_new_str("");
            int c = (int)py_to_int(args[0]);
            char s[2] = {(char)c, '\0'};
            return py_new_str(s);
        }
        if (strstr(name, "ord"))
        {
            if (nargs < 1)
                return py_new_int(0);
            PyObject *arg = args[0];
            if (arg->type == PY_STR && arg->val.str_val.len > 0)
                return py_new_int((uint8_t)arg->val.str_val.data[0]);
            return py_new_int(0);
        }
        if (strstr(name, "hex"))
        {
            if (nargs < 1)
                return py_new_str("");
            char buf[32];
            snprintf(buf, sizeof(buf), "0x%llx", (unsigned long long)py_to_int(args[0]));
            return py_new_str(buf);
        }
        if (strstr(name, "oct"))
        {
            if (nargs < 1)
                return py_new_str("");
            char buf[32];
            snprintf(buf, sizeof(buf), "0o%llo", (unsigned long long)py_to_int(args[0]));
            return py_new_str(buf);
        }
        if (strstr(name, "bin"))
        {
            if (nargs < 1)
                return py_new_str("");
            int64_t v = py_to_int(args[0]);
            char buf[64];
            int j = 0;
            buf[j++] = '0';
            buf[j++] = 'b';
            if (v == 0)
                buf[j++] = '0';
            else
            {
                int i;
                for (i = 63; i >= 0; i--)
                    if ((v >> i) & 1)
                        break;
                for (; i >= 0; i--)
                    buf[j++] = ((v >> i) & 1) ? '1' : '0';
            }
            buf[j] = '\0';
            return py_new_str(buf);
        }
        if (strstr(name, "id"))
        {
            if (nargs < 1)
                return py_new_int(0);
            return py_new_int((int64_t)(intptr_t)args[0]);
        }
        if (strstr(name, "hash"))
        {
            if (nargs < 1)
                return py_new_int(-1);
            return py_new_int(py_hash(args[0]));
        }
        if (strstr(name, "repr"))
        {
            if (nargs < 1)
                return py_new_str("");
            PyObject *arg = args[0];
            if (arg->type == PY_STR)
            {
                char buf[256];
                snprintf(buf, sizeof(buf), "'%s'", arg->val.str_val.data);
                return py_new_str(buf);
            }
            return py_new_str("<object>");
        }
        if (strstr(name, "isinstance"))
        {
            if (nargs < 2)
                return py_false;
            return py_new_bool(args[0]->type == args[1]->type);
        }
        if (strstr(name, "issubclass"))
        {
            if (nargs < 2)
                return py_false;
            return py_false;
        }
        if (strstr(name, "hasattr"))
        {
            if (nargs < 2)
                return py_false;
            return py_false;
        }
        if (strstr(name, "getattr"))
        {
            if (nargs < 2)
                return py_none;
            return py_getattr(args[0], py_str_cstr(args[1]));
        }
        if (strstr(name, "setattr"))
        {
            if (nargs < 3)
                return py_new_int(-1);
            py_setattr(args[0], py_str_cstr(args[1]), args[2]);
            return py_new_int(0);
        }
        if (strstr(name, "delattr"))
        {
            if (nargs < 2)
                return py_new_int(-1);
            return py_new_int(-1);
        }
        if (strstr(name, "globals"))
        {
            Py_INCREF(py_globals);
            return py_globals;
        }
        if (strstr(name, "locals"))
        {
            Py_INCREF(py_globals);
            return py_globals;
        }
        if (strstr(name, "dir"))
        {
            PyObject *result = py_new_list(0);
            return result;
        }
        if (strstr(name, "callable"))
        {
            if (nargs < 1)
                return py_false;
            return py_new_bool(func->type == PY_CFUNCTION || func->type == PY_FUNCTION);
        }
        if (strstr(name, "iter"))
        {
            if (nargs < 1)
                return NULL;
            return py_iter(args[0]);
        }
        if (strstr(name, "next"))
        {
            if (nargs < 1)
                return NULL;
            return py_next(args[0]);
        }
        return py_none;
    }
    if (func->type == PY_FUNCTION)
    {
        PyObject *result = py_new_int(0);
        Py_INCREF(result);
        return result;
    }
    return py_new_exception(PyExc_TypeError, py_new_str("object is not callable"));
}

PyObject *py_call_kw(PyObject *func, PyObject **args, size_t nargs, PyObject **kwargs, size_t nkwargs)
{
    (void)kwargs;
    (void)nkwargs;
    return py_call(func, args, nargs);
}

PyObject *py_method_call(PyObject *o, const char *name, PyObject **args, size_t nargs)
{
    PyObject *method = py_getattr(o, name);
    if (!method)
        return py_new_exception(PyExc_AttributeError, py_new_str(name));
    PyObject *result = py_call(method, args, nargs);
    Py_DECREF(method);
    return result;
}

PyObject *py_import(const char *name)
{
    PyObject *module = py_new_module(py_new_str(name));
    PyDict_SetItemString(py_globals, name, module);
    return module;
}

PyObject *py_load_file(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f)
        return NULL;
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *buf = (char *)malloc(len + 1);
    fread(buf, 1, len, f);
    buf[len] = '\0';
    fclose(f);
    PyObject *result = py_new_str(buf);
    free(buf);
    return result;
}

PyObject *py_exec(PyObject *code, PyObject *globals, PyObject *locals)
{
    (void)code;
    (void)globals;
    (void)locals;
    return py_none;
}

PyObject *py_eval(PyObject *code, PyObject *globals, PyObject *locals)
{
    (void)code;
    (void)globals;
    (void)locals;
    return py_none;
}

void py_traceback(PyObject *exc)
{
    if (!exc)
        return;
    const char *type_name = "Exception";
    if (exc == PyExc_TypeError)
        type_name = "TypeError";
    else if (exc == PyExc_IndexError)
        type_name = "IndexError";
    else if (exc == PyExc_KeyError)
        type_name = "KeyError";
    else if (exc == PyExc_ValueError)
        type_name = "ValueError";
    else if (exc == PyExc_ZeroDivisionError)
        type_name = "ZeroDivisionError";
    else if (exc == PyExc_RuntimeError)
        type_name = "RuntimeError";
    printf("Traceback (most recent call last):\n");
    printf("  File \"<string>\", line 1, in <module>\n");
    printf("%s", type_name);
    if (exc->type == PY_EXCEPTION)
        printf(": %s", py_str_cstr(exc->val.exception_val.msg));
    printf("\n");
}

PyObject *py_get_builtins(void)
{
    Py_INCREF(py_builtins);
    return py_builtins;
}

void py_add_builtin(const char *name, PyObject *value)
{
    PyDict_SetItemString(py_builtins, name, value);
}

void py_add_builtin_int(const char *name, int64_t val)
{
    PyObject *v = py_new_int(val);
    PyDict_SetItemString(py_builtins, name, v);
    Py_DECREF(v);
}

void py_add_builtin_func(const char *name, PyCFunction func, int min_args, int max_args)
{
    (void)min_args;
    (void)max_args;
    PyObject *f = (PyObject *)malloc(sizeof(PyObject));
    f->type = PY_CFUNCTION;
    f->refcount = 1;
    f->val.cfunction_val.func = (void *)func;
    f->val.cfunction_val.module = py_builtins;
    Py_INCREF(py_builtins);
    PyDict_SetItemString(py_builtins, name, f);
    Py_DECREF(f);
}

int PyDict_SetItemString(PyObject *o, const char *key, PyObject *val)
{
    PyObject *k = py_new_str(key);
    int r = py_setitem(o, k, val);
    Py_DECREF(k);
    return r;
}

PyObject *PyDict_GetItemString(PyObject *o, const char *key)
{
    PyObject *k = py_new_str(key);
    PyObject *v = py_getitem(o, k);
    Py_DECREF(k);
    return v;
}

int PyList_Append(PyObject *o, PyObject *v)
{
    if (o->type != PY_LIST)
        return -1;
    if (o->val.list_val.size >= o->val.list_val.capacity)
    {
        size_t new_cap = o->val.list_val.capacity * 2;
        PyObject **new_items = (PyObject **)realloc(o->val.list_val.items, new_cap * sizeof(PyObject *));
        if (!new_items)
            return -1;
        o->val.list_val.items = new_items;
        o->val.list_val.capacity = new_cap;
    }
    o->val.list_val.items[o->val.list_val.size++] = v;
    Py_INCREF(v);
    return 0;
}

int PySequence_Contains(PyObject *o, PyObject *val)
{
    PyObject *r = py_contains(o, val);
    return r == py_true ? 1 : 0;
}

Py_ssize_t PySequence_Size(PyObject *o)
{
    PyObject *l = py_len(o);
    if (!l)
        return -1;
    return py_to_int(l);
}

PyObject *PySequence_GetItem(PyObject *o, Py_ssize_t i)
{
    PyObject *key = py_new_int(i);
    PyObject *v = py_getitem(o, key);
    Py_DECREF(key);
    return v;
}

int PySequence_SetItem(PyObject *o, Py_ssize_t i, PyObject *v)
{
    PyObject *key = py_new_int(i);
    int r = py_setitem(o, key, v);
    Py_DECREF(key);
    return r;
}
