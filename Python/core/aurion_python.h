#ifndef AURION_PYTHON_H
#define AURION_PYTHON_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

typedef intptr_t Py_ssize_t;

#define PY_VERSION "3.14.0"

typedef enum
{
    PY_NONE = 0,
    PY_INT,
    PY_FLOAT,
    PY_BOOL,
    PY_STR,
    PY_BYTES,
    PY_LIST,
    PY_DICT,
    PY_TUPLE,
    PY_SET,
    PY_FUNCTION,
    PY_CFUNCTION,
    PY_MODULE,
    PY_TYPE,
    PY_OBJECT,
    PY_EXCEPTION,
    PY_BYTESIO,
    PY_FILE,
    PY_FRAME,
    PY_COROUTINE,
    PY_GENERATOR,
} PyTypeTag;

typedef struct PyObject
{
    PyTypeTag type;
    int refcount;
    union
    {
        int64_t int_val;
        double float_val;
        bool bool_val;
        struct
        {
            char *data;
            size_t len;
        } str_val;
        struct
        {
            uint8_t *data;
            size_t len;
        } bytes_val;
        struct
        {
            struct PyObject **items;
            size_t size;
            size_t capacity;
        } list_val;
        struct
        {
            struct PyObject **keys;
            struct PyObject **values;
            size_t size;
            size_t capacity;
        } dict_val;
        struct
        {
            struct PyObject **items;
            size_t size;
            size_t capacity;
        } tuple_val;
        struct
        {
            struct PyObject **items;
            size_t size;
            size_t capacity;
        } set_val;
        struct
        {
            struct PyObject *name;
            struct PyObject *globals;
            struct PyObject *locals;
            uint8_t *bytecode;
            size_t bytecode_len;
            struct PyObject **cells;
            size_t nlocals;
            size_t nstack;
        } function_val;
        struct
        {
            void *func;
            struct PyObject *module;
        } cfunction_val;
        struct
        {
            struct PyObject *name;
            struct PyObject **attrs;
            size_t nattrs;
            PyTypeTag instance_type;
        } type_val;
        struct
        {
            struct PyObject *msg;
            struct PyObject *exc_type;
        } exception_val;
        struct
        {
            void *file;
            char *mode;
            char *name;
        } file_val;
        struct
        {
            uint8_t *buf;
            size_t len;
            size_t pos;
            bool closed;
        } bytesio_val;
        struct
        {
            struct PyObject *globals;
            struct PyObject *locals;
            struct PyObject *stack[16];
            size_t stack_top;
            size_t pc;
        } frame_val;
    } val;
} PyObject;

#define Py_INCREF(o)         \
    do                       \
    {                        \
        if (o)               \
            (o)->refcount++; \
    } while (0)
#define Py_DECREF(o)                \
    do                              \
    {                               \
        if (o)                      \
        {                           \
            (o)->refcount--;        \
            if ((o)->refcount <= 0) \
                py_dealloc(o);      \
        }                           \
    } while (0)

typedef PyObject *(*PyCFunction)(PyObject *, PyObject **, size_t);

extern PyObject *py_none;
extern PyObject *py_true;
extern PyObject *py_false;

PyObject *py_new_int(int64_t val);
PyObject *py_new_float(double val);
PyObject *py_new_bool(bool val);
PyObject *py_new_str(const char *s);
PyObject *py_new_strn(const char *s, size_t len);
PyObject *py_new_bytes(const uint8_t *data, size_t len);
PyObject *py_new_list(size_t capacity);
PyObject *py_new_dict(void);
PyObject *py_new_tuple(size_t size);
PyObject *py_new_set(void);
PyObject *py_new_function(PyObject *name, PyObject *globals, uint8_t *bytecode, size_t bytecode_len, size_t nlocals, size_t nstack);
PyObject *py_new_cfunction(void *func, PyObject *module, const char *name);
PyObject *py_new_type(PyObject *name, PyTypeTag instance_type, PyObject **attrs, size_t nattrs);
PyObject *py_new_exception(PyObject *type, PyObject *msg);
PyObject *py_new_module(PyObject *name);
PyObject *py_new_bytesio(void);
PyObject *py_new_file(void *file, const char *mode, const char *name);

void py_dealloc(PyObject *o);

void py_init(void);
void py_shutdown(void);

bool py_is_true(PyObject *o);
int64_t py_to_int(PyObject *o);
double py_to_float(PyObject *o);
const char *py_str_cstr(PyObject *o);

PyObject *py_add(PyObject *a, PyObject *b);
PyObject *py_sub(PyObject *a, PyObject *b);
PyObject *py_mul(PyObject *a, PyObject *b);
PyObject *py_div(PyObject *a, PyObject *b);
PyObject *py_floordiv(PyObject *a, PyObject *b);
PyObject *py_mod(PyObject *a, PyObject *b);
PyObject *py_pow(PyObject *a, PyObject *b);
PyObject *py_neg(PyObject *a);
PyObject *py_pos(PyObject *a);
PyObject *py_invert(PyObject *a);
PyObject *py_lshift(PyObject *a, PyObject *b);
PyObject *py_rshift(PyObject *a, PyObject *b);
PyObject *py_and(PyObject *a, PyObject *b);
PyObject *py_or(PyObject *a, PyObject *b);
PyObject *py_xor(PyObject *a, PyObject *b);

bool py_less(PyObject *a, PyObject *b);
bool py_less_equal(PyObject *a, PyObject *b);
bool py_greater(PyObject *a, PyObject *b);
bool py_greater_equal(PyObject *a, PyObject *b);
bool py_equal(PyObject *a, PyObject *b);
bool py_not_equal(PyObject *a, PyObject *b);

PyObject *py_getitem(PyObject *o, PyObject *key);
int py_setitem(PyObject *o, PyObject *key, PyObject *val);
int py_delitem(PyObject *o, PyObject *key);

PyObject *py_getattr(PyObject *o, const char *name);
int py_setattr(PyObject *o, const char *name, PyObject *val);

size_t py_hash(PyObject *o);
PyObject *py_len(PyObject *o);
PyObject *py_contains(PyObject *o, PyObject *val);
PyObject *py_iter(PyObject *o);
PyObject *py_next(PyObject *o);

PyObject *py_call(PyObject *func, PyObject **args, size_t nargs);
PyObject *py_call_kw(PyObject *func, PyObject **args, size_t nargs, PyObject **kwargs, size_t nkwargs);
PyObject *py_method_call(PyObject *o, const char *name, PyObject **args, size_t nargs);
PyObject *py_import(const char *name);
PyObject *py_load_file(const char *path);
PyObject *py_exec(PyObject *code, PyObject *globals, PyObject *locals);
PyObject *py_eval(PyObject *code, PyObject *globals, PyObject *locals);

void py_traceback(PyObject *exc);
PyObject *py_get_builtins(void);

void py_add_builtin(const char *name, PyObject *value);
void py_add_builtin_int(const char *name, int64_t val);
void py_add_builtin_func(const char *name, PyCFunction func, int min_args, int max_args);

int PyDict_SetItemString(PyObject *o, const char *key, PyObject *val);
PyObject *PyDict_GetItemString(PyObject *o, const char *key);
int PyList_Append(PyObject *o, PyObject *v);
int PySequence_Contains(PyObject *o, PyObject *val);
PyObject *PySequence_GetItem(PyObject *o, Py_ssize_t i);
int PySequence_SetItem(PyObject *o, Py_ssize_t i, PyObject *v);
Py_ssize_t PySequence_Size(PyObject *o);

#endif