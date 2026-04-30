#ifndef AURION_PYTHON_H
#define AURION_PYTHON_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#define PY_VERSION "3.14.0"

typedef enum {
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

typedef struct PyObject {
    PyTypeTag type;
    int refcount;
    union {
        int64_t int_val;
        double float_val;
        bool bool_val;
        struct {
            char *data;
            size_t len;
        } str_val;
        struct {
            uint8_t *data;
            size_t len;
        } bytes_val;
        struct {
            struct PyObject **items;
            size_t size;
            size_t capacity;
        } list_val;
        struct {
            struct PyObject **keys;
            struct PyObject **values;
            size_t size;
            size_t capacity;
        } dict_val;
        struct {
            struct PyObject **items;
            size_t size;
            size_t capacity;
        } tuple_val;
        struct {
            struct PyObject **items;
            size_t size;
            size_t capacity;
        } set_val;
        struct {
            struct PyObject *name;
            struct PyObject *globals;
            struct PyObject *locals;
            uint8_t *bytecode;
            size_t bytecode_len;
            struct PyObject **cells;
            size_t nlocals;
            size_t nstack;
        } function_val;
        struct {
            void *func;
            struct PyObject *module;
        } cfunction_val;
        struct {
            struct PyObject *name;
            struct PyObject **attrs;
            size_t nattrs;
            PyTypeTag instance_type;
        } type_val;
        struct {
            struct PyObject *msg;
            struct PyObject *exc_type;
        } exception_val;
        struct {
            void *file;
            char *mode;
            char *name;
        } file_val;
        struct {
            uint8_t *buf;
            size_t len;
            size_t pos;
            bool closed;
        } bytesio_val;
        struct {
            struct PyFrame *prev;
            uint8_t *ip;
            struct PyObject **stack_top;
            struct PyObject *globals;
            struct PyObject *locals;
            struct PyObject **cells;
            size_t nlocals;
            size_t nstack;
            struct PyObject *result;
        } frame_val;
        struct {
            struct PyObject *code;
            struct PyObject *send_value;
            bool running;
        } coroutine_val;
        struct {
            struct PyFrame *frame;
            uint8_t *ip;
            struct PyObject *value;
            bool running;
        } generator_val;
    } val;
} PyObject;

typedef struct {
    const char *name;
    PyObject *(*func)(PyObject *, PyObject **, size_t);
    int min_args;
    int max_args;
} PyCFunction;

extern PyObject *py_none;
extern PyObject *py_true;
extern PyObject *py_false;

#define Py_INCREF(o) do { if (o) ((PyObject*)(o))->refcount++; } while(0)
#define Py_DECREF(o) do { if (o) { ((PyObject*)(o))->refcount--; if (((PyObject*)(o))->refcount <= 0) py_dealloc(o); } } while(0)

#define Py_RETURN_NONE return py_none
#define Py_RETURN_TRUE return py_true
#define Py_RETURN_FALSE return py_false

#define PyExceptionType(exc) (((PyObject*)(exc))->val.exception_val.exc_type)
#define PyExceptionMsg(exc) (((PyObject*)(exc))->val.exception_val.msg)

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

PyObject *py_new_object(PyTypeTag type);

void py_dealloc(PyObject *o);

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
PyObject *py_iter(PyObject *o);
PyObject *py_next(PyObject *o);
PyObject *py_len(PyObject *o);
PyObject *py_contains(PyObject *o, PyObject *val);
PyObject *py_subscript(PyObject *o, PyObject *key);
int py_subscript_assign(PyObject *o, PyObject *key, PyObject *val);

PyObject *py_call(PyObject *func, PyObject **args, size_t nargs);
PyObject *py_call_kw(PyObject *func, PyObject **args, size_t nargs, PyObject **kwargs, size_t nkwargs);
PyObject *py_method_call(PyObject *o, const char *name, PyObject **args, size_t nargs);

PyObject *py_import(const char *name);
PyObject *py_load_file(const char *path);

PyObject *py_exec(PyObject *code, PyObject *globals, PyObject *locals);
PyObject *py_eval(PyObject *code, PyObject *globals, PyObject *locals);

void py_init(void);
void py_shutdown(void);

void py_traceback(PyObject *exc);

extern PyObject *pybuiltins_dict;

PyObject *py_get_builtins(void);

typedef struct {
    const char *name;
    PyObject *value;
} PyConstant;

void py_add_builtin(const char *name, PyObject *value);
void py_add_builtin_int(const char *name, int64_t val);
void py_add_builtin_func(const char *name, PyCFunction func, int min_args, int max_args);

#define PY_T_OK 0
#define PY_T_ERROR -1
#define PY_T_EOF -2
#define PY_T_ILLEGAL -3

typedef struct {
    int type;
    union {
        int64_t int_val;
        double float_val;
        struct { char *data; size_t len; } str_val;
        struct { char *data; size_t len; } ident_val;
        struct { char *data; size_t len; } raw_val;
    } val;
    int lineno;
    int col_offset;
} PyToken;

typedef enum {
    TOKEN_INVALID = 0,
    TOKEN_EOF,
    TOKEN_NEWLINE,
    TOKEN_INDENT,
    TOKEN_DEDENT,
    TOKEN_LPAREN,
    TOKEN_RPAREN,
    TOKEN_LBRACKET,
    TOKEN_RBRACKET,
    TOKEN_LBRACE,
    TOKEN_RBRACE,
    TOKEN_COMMA,
    TOKEN_COLON,
    TOKEN_SEMI,
    TOKEN_DOT,
    TOKEN_ELLIPSIS,
    TOKEN_PLUS,
    TOKEN_MINUS,
    TOKEN_STAR,
    TOKEN_STAR_STAR,
    TOKEN_SLASH,
    TOKEN_SLASH_SLASH,
    TOKEN_PERCENT,
    TOKEN_LSHIFT,
    TOKEN_RSHIFT,
    TOKEN_AMPER,
    TOKEN_PIPE,
    TOKEN_CIRCUMFLEX,
    TOKEN_TILDE,
    TOKEN_LESS,
    TOKEN_GREATER,
    TOKEN_EQUAL,
    TOKEN_EXCLAIM,
    TOKEN_LESS_EQUAL,
    TOKEN_GREATER_EQUAL,
    TOKEN_EQUAL_EQUAL,
    TOKEN_NOT_EQUAL,
    TOKEN_LESS_LESS,
    TOKEN_GREATER_GREATER,
    TOKEN_PLUS_EQUAL,
    TOKEN_MINUS_EQUAL,
    TOKEN_STAR_EQUAL,
    TOKEN_STAR_STAR_EQUAL,
    TOKEN_SLASH_EQUAL,
    TOKEN_SLASH_SLASH_EQUAL,
    TOKEN_PERCENT_EQUAL,
    TOKEN_AMPER_EQUAL,
    TOKEN_PIPE_EQUAL,
    TOKEN_CIRCUMFLEX_EQUAL,
    TOKEN_LSHIFT_EQUAL,
    TOKEN_RSHIFT_EQUAL,
    TOKEN_AT,
    TOKEN_AT_EQUAL,
    TOKEN_COLON_EQUAL,
    TOKEN_ARROW,
    TOKEN_KEYWORD,
    TOKEN_INT,
    TOKEN_FLOAT,
    TOKEN_IMAG,
    TOKEN_STRING,
    TOKEN_BYTES,
    TOKEN_IDENT,
    TOKEN_ENCODING,
    TOKEN_ENDMARKER,
    TOKEN_N_TOKENS,
} TokenType;

typedef struct {
    const char *text;
    TokenType type;
} PyKeyword;

extern PyKeyword py_keywords[];

TokenType py_keyword_or_ident(const char *s, size_t len);

#define TOKEN_NAME(t) (#t)

#define IS_SOFT_KEYWORD(t) ((t) >= TOKEN_KEYWORD)

typedef enum {
    AST_INVALID = 0,
    AST_MODULE,
    AST_EXPR,
    AST_ASSIGN,
    AST_AUGASSIGN,
    AST_ANNOTATEDASSIGN,
    AST_PASS,
    AST_BREAK,
    AST_CONTINUE,
    AST_RETURN,
    AST_DELETE,
    AST_RAISE,
    AST_ASSERT,
    AST_IMPORT,
    AST_IMPORTFROM,
    AST_IMPORTSTAR,
    AST_GLOBAL,
    AST_NONLOCAL,
    AST_IF,
    AST_WHILE,
    AST_FOR,
    AST_ASYNCFOR,
    AST_TRY,
    AST_EXCEPTCLause,
    AST_FINALLY,
    AST_WITH,
    AST_WITHITEM,
    AST_MATCH,
    AST_PATTERNCLause,
    AST_PATTERNMATCHAS,
    AST_PATTERNMATCHOR,
    AST_PATTERNMAPPING,
    AST_PATTERNSEQUENCE,
    AST_PATTERNCLASS,
    AST_PATTERNCAPTURE,
    AST_PATTERNVALUE,
    AST_PATTERNLITERAL,
    AST_PATTERNWILDCARD,
    AST_PATTERNSEQUENCEANY,
    AST_PATTERNSEQUENCEEMPTY,
    AST_COMPREHENSION,
    AST_YIELD,
    AST_YIELDFROM,
    AST_AWAIT,
    AST_LAMBDA,
    AST_DOTTUPLE,
    AST_FUNCTIONDEF,
    AST_ASYNCFUNCTIONDEF,
    AST_CLASSDEF,
    AST_DECORATOR,
    AST_ARGUMENTS,
    AST_ARG,
    AST_RETURNTYPE,
    AST_COMMA,
    AST_AS,
    AST_EQ,
    AST_NOTEQ,
    AST_LT,
    AST_GT,
    AST_LTE,
    AST_GTE,
    AST_IS,
    AST_IN,
    AST_NOTIN,
    AST_AND,
    AST_OR,
    AST_NOT,
    AST_UADD,
    AST_USUB,
    AST_INVERT,
    AST_BINAND,
    AST_BINOR,
    AST_BINXOR,
    AST_LSHIFT,
    AST_RSHIFT,
    AST_ADD,
    AST_SUB,
    AST_MULT,
    AST_DIV,
    AST_FLOORDIV,
    AST_MOD,
    AST_POW,
    AST_MATRIX,
    AST_SUBSCRIPT,
    AST_SLICE,
    AST_EXTENDED_SLICE,
    AST_NAME,
    AST_CONST,
    AST_FORMATTEDVALUE,
    AST_JOIN,
    AST_ATTRIBUTE,
    AST_STARRED,
    AST_TUPLE,
    AST_LIST,
    AST_SET,
    AST_DICT,
    AST_DICTITEM,
    AST_N_TOKENS,
} ASTType;

typedef struct ASTNode {
    ASTType type;
    int lineno;
    int col_offset;
    int end_lineno;
    int end_col_offset;
    union {
        struct { struct ASTNode **stmts; size_t nstmts; } module;
        struct { struct ASTNode *value; } expr_stmt;
        struct { struct ASTNode *target; struct ASTNode *value; } assign_stmt;
        struct { struct ASTNode *target; int op; struct ASTNode *value; } augassign_stmt;
        struct { struct ASTNode *target; struct ASTNode *annotation; struct ASTNode *value; int simple; } annotatedassign;
        struct { struct ASTNode *test; struct ASTNode *msg; } assert_stmt;
        struct { struct ASTNode *names; } import_stmt;
        struct { struct ASTNode *module; struct ASTNode *names; int level; } importfrom_stmt;
        struct { char *name; } global_stmt;
        struct { char *name; } nonlocal_stmt;
        struct { struct ASTNode *test; struct ASTNode *body; struct ASTNode *orelse; } if_stmt;
        struct { struct ASTNode *target; struct ASTNode *iter; struct ASTNode *body; struct ASTNode *orelse; } for_stmt;
        struct { struct ASTNode *test; struct ASTNode *body; struct ASTNode *orelse; struct ASTNode *finalbody; } while_stmt;
        struct { struct ASTNode **handlers; struct ASTNode *body; struct ASTNode *orelse; struct ASTNode *finalbody; } try_stmt;
        struct { struct ASTNode *type; struct ASTNode *name; struct ASTNode *body; } except_clause;
        struct { struct ASTNode **items; struct ASTNode *body; } with_stmt;
        struct { struct ASTNode *context; struct ASTNode *asname; } withitem;
        struct { struct ASTNode *subject; struct ASTNode **cases; } match_stmt;
        struct { struct ASTNode *pattern; struct ASTNode *body; size_t guard_present; struct ASTNode *guard; } pattern_case;
        struct { struct ASTNode *value; struct ASTNode *asname; } pattern_match_as;
        struct { struct ASTNode **pats; } pattern_match_or;
        struct { struct ASTNode **keys; struct ASTNode **patterns; size_t n; } pattern_mapping;
        struct { struct ASTNode **patterns; size_t n; } pattern_sequence;
        struct { char *cls; struct ASTNode **args; struct ASTNode **keywords; size_t nargs; } pattern_class;
        struct { char *name; } pattern_capture;
        struct { struct ASTNode *value; } pattern_value;
        struct { struct ASTNode *value; } pattern_literal;
        struct { } pattern_wildcard;
        struct { } pattern_sequence_any;
        struct { } pattern_sequence_empty;
        struct { struct ASTNode *elt; struct ASTNode *generators; size_t ngenerators; } comprehension;
        struct { struct ASTNode *value; } yield_expr;
        struct { struct ASTNode *value; } yield_from_expr;
        struct { struct ASTNode *value; } await_expr;
        struct { struct ASTNode *args; struct ASTNode *body; } lambda_expr;
        struct { struct ASTNode *target; struct ASTNode *iter; struct ASTNode *body; struct ASTNode *is_async; struct ASTNode *generators; size_t ngenerators; } comp_for;
        struct { struct ASTNode *elt; struct ASTNode *generators; size_t ngenerators; } comp_iter;
        struct { struct ASTNode *elt; } comp_if;
        struct { struct ASTNode *elt; struct ASTNode *generators; size_t ngenerators; } list_comp;
        struct { struct ASTNode *elt; struct ASTNode *generators; size_t ngenerators; } set_comp;
        struct { struct ASTNode *elt; struct ASTNode *generators; size_t ngenerators; } dict_comp;
        struct { struct ASTNode *key; struct ASTNode *value; } dict_comp_item;
        struct { struct ASTNode *elt; } starred_expr;
        struct { char *id; } name_expr;
        struct { struct ASTNode *value; } const_expr;
        struct { struct ASTNode *elt; struct ASTNode *slice; } subscript_expr;
        struct { struct ASTNode *lower; struct ASTNode *upper; struct ASTNode *step; } slice_expr;
        struct { struct ASTNode *value; char *attr; } attribute_expr;
        struct { struct ASTNode **elts; } tuple_expr;
        struct { struct ASTNode **elts; } list_expr;
        struct { struct ASTNode **elts; } set_expr;
        struct { struct ASTNode **keys; struct ASTNode **values; size_t n; } dict_expr;
        struct { struct ASTNode *key; struct ASTNode *value; } dict_item;
        struct { struct ASTNode *func; struct ASTNode **args; struct ASTNode **keywords; size_t nargs; size_t nkeywords; } call_expr;
        struct { struct ASTNode *func; struct ASTNode **args; struct ASTNode **keywords; size_t nargs; size_t nkeywords; } await_call_expr;
        struct { struct ASTNode *left; int op; struct ASTNode *right; } binary_expr;
        struct { int op; struct ASTNode *operand; } unary_expr;
        struct { struct ASTNode *test; struct ASTNode *body; struct ASTNode *orelse; } ternary_expr;
        struct { char *name; struct ASTNode *args; struct ASTNode *body; struct ASTNode **decorators; size_t ndecorators; size_t nargs; size_t nkwonlyargs; struct ASTNode *returns; struct ASTNode *type_params; } function_def;
        struct { char *name; struct ASTNode *bases; struct ASTNode **keywords; size_t nbases; size_t nkeywords; struct ASTNode *body; struct ASTNode **decorators; size_t ndecorators; } class_def;
        struct { struct ASTNode **args; struct ASTNode *defaults; size_t nargs; size_t nkwonlyargs; struct ASTNode *kwonlydefaults; struct ASTNode *kwarg; struct ASTNode *vararg; } arguments;
        struct { char *arg; struct ASTNode *annotation; } arg_node;
        struct { struct ASTNode *type; char *name; } keyword_arg;
    } val;
} ASTNode;

typedef struct {
    int opcode;
    int arg;
    int lineno;
} PyInstruction;

typedef struct {
    PyInstruction *instructions;
    size_t ninstructions;
    size_t capacity;
    int nlocals;
    int nstack;
    int ncellvars;
    int nfreevars;
    int flags;
    PyObject *consts;
    PyObject *names;
    PyObject *varnames;
    PyObject *freevars;
    PyObject *cellvars;
    PyObject *filename;
    PyObject *name;
    int first_lineno;
} PyCodeObject;

typedef struct {
    PyCodeObject *code;
    PyObject *globals;
    PyObject *locals;
    PyObject **stack;
    PyObject **stack_top;
    PyObject **cells;
    uint8_t *ip;
    PyObject *exc_value;
    PyObject *generator;
    bool running;
    bool yield_flag;
    struct PyFrame *prev;
} PyFrame;

typedef struct {
    PyObject_HEAD
    PyObject *send_value;
    PyFrame *frame;
    bool running;
} PyGenerator;

typedef struct {
    PyObject_HEAD
    PyObject *coro;
} PyCoroutine;

typedef enum {
    FRAME_RETURN = 1,
    FRAME_EXCEPTION = 2,
    FRAME_YIELD = 3,
    FRAME_YIELD_FROM = 4,
} PyFrameStatus;

int py_frame_push(PyFrame *frame, PyCodeObject *code, PyObject *globals, PyObject *locals);
PyObject *py_frame_run(PyFrame *frame);
void py_frame_pop(PyFrame *frame);

typedef struct {
    const char *name;
    PyObject *(*getter)(PyObject *, void *);
    int (*setter)(PyObject *, PyObject *, void *);
} PyGetSet;

typedef struct {
    const char *name;
    int offset;
    int readonly;
    PyObject *(*get)(PyObject *, void *);
    int (*set)(PyObject *, PyObject *, void *);
} PyMember;

#define PyObject_HEAD PyTypeTag type; int refcount
#define PyVarObject_HEAD PyTypeTag type; int refcount; size_t size

typedef struct {
    PyVarObject_HEAD
    PyObject *wrappers;
    PyGetSet *getsets;
    PyMember *members;
    struct PyMethodDef *methods;
    struct PyTypeObject *base;
    PyObject *mro;
    PyObject *cache;
    PyObject *subclasses;
    PyObject *native_slots;
} PyTypeObject;

typedef PyObject *(*PyGetterFunc)(PyObject *, void *);
typedef int (*PySetterFunc)(PyObject *, PyObject *, void *);

typedef struct PyMethodDef {
    const char *ml_name;
    void *ml_meth;
    int ml_flags;
    const char *ml_doc;
} PyMethodDef;

#define METH_VARARGS 0x0001
#define METH_KEYWORDS 0x0002
#define METH_NOARGS 0x0004
#define METH_O 0x0008
#define METH_CLASS 0x0010
#define METH_STATIC 0x0020
#define METH_COEXIST 0x0040
#define METH_FASTCALL 0x0080
#define METH_FASTCALL_KEYWORDS 0x0100
#define METH_METHOD 0x0200
#define METH_STACKLESS 0x0400

extern PyTypeObject PyList_Type;
extern PyTypeObject PyDict_Type;
extern PyTypeObject PyTuple_Type;
extern PyTypeObject PySet_Type;
extern PyTypeObject PyStr_Type;
extern PyTypeObject PyBytes_Type;
extern PyTypeObject PyInt_Type;
extern PyTypeObject PyFloat_Type;
extern PyTypeObject PyBool_Type;
extern PyTypeObject PyNone_Type;
extern PyTypeObject PyType_Type;
extern PyTypeObject PyFunction_Type;
extern PyTypeObject PyCFunction_Type;
extern PyTypeObject PyModule_Type;
extern PyTypeObject PyFile_Type;
extern PyTypeObject PyBytesIO_Type;
extern PyTypeObject PyGenerator_Type;
extern PyTypeObject PyCoroutine_Type;
extern PyTypeObject PyFrame_Type;
extern PyTypeObject PyTraceback_Type;
extern PyTypeObject PyCell_Type;

int PyType_Ready(PyTypeObject *type);
PyObject *PyObject_GetAttrString(PyObject *o, const char *name);
int PyObject_SetAttrString(PyObject *o, const char *name, PyObject *val);
PyObject *PyObject_GetItem(PyObject *o, PyObject *key);
int PyObject_SetItem(PyObject *o, PyObject *key, PyObject *val);
int PyObject_DelItem(PyObject *o, PyObject *key);

int PyNumber_Check(PyObject *o);
PyObject *PyNumber_Add(PyObject *a, PyObject *b);
PyObject *PyNumber_Subtract(PyObject *a, PyObject *b);
PyObject *PyNumber_Multiply(PyObject *a, PyObject *b);
PyObject *PyNumber_TrueDivide(PyObject *a, PyObject *b);
PyObject *PyNumber_FloorDivide(PyObject *a, PyObject *b);
PyObject *PyNumber_Remainder(PyObject *a, PyObject *b);
PyObject *PyNumber_Power(PyObject *a, PyObject *b);
PyObject *PyNumber_Negative(PyObject *o);
PyObject *PyNumber_Positive(PyObject *o);
PyObject *PyNumber_Invert(PyObject *o);
PyObject *PyNumber_Lshift(PyObject *a, PyObject *b);
PyObject *PyNumber_Rshift(PyObject *a, PyObject *b);
PyObject *PyNumber_And(PyObject *a, PyObject *b);
PyObject *PyNumber_Or(PyObject *a, PyObject *b);
PyObject *PyNumber_Xor(PyObject *a, PyObject *b);

int PySequence_Check(PyObject *o);
Py_ssize_t PySequence_Size(PyObject *o);
PyObject *PySequence_GetItem(PyObject *o, Py_ssize_t i);
int PySequence_SetItem(PyObject *o, Py_ssize_t i, PyObject *v);
int PySequence_DelItem(PyObject *o, Py_ssize_t i);
PyObject *PySequence_Concat(PyObject *a, PyObject *b);
PyObject *PySequence_Repeat(PyObject *o, Py_ssize_t n);
PyObject *PySequence_In(PyObject *o, PyObject *v);
PyObject *PySequence_Index(PyObject *o, PyObject *v);

int PyMapping_Check(PyObject *o);
Py_ssize_t PyMapping_Size(PyObject *o);
PyObject *PyMapping_GetItemString(PyObject *o, const char *key);
int PyMapping_SetItemString(PyObject *o, const char *key, PyObject *val);
PyObject *PyMapping_Keys(PyObject *o);
PyObject *PyMapping_Values(PyObject *o);
PyObject *PyMapping_Items(PyObject *o);

PyObject *PyIter_Next(PyObject *o);

int PyCallable_Check(PyObject *o);
PyObject *PyObject_Call(PyObject *o, PyObject *args, PyObject *kwargs);
PyObject *PyObject_CallObject(PyObject *o, PyObject *args);
PyObject *PyObject_CallFunction(PyObject *fmt, ...);
PyObject *PyObject_CallMethod(PyObject *o, const char *method, ...);

Py_hash_t PyObject_Hash(PyObject *o);
PyObject *PyObject_Repr(PyObject *o);
PyObject *PyObject_Str(PyObject *o);
PyObject *PyObject_ASCII(PyObject *o);
PyObject *PyObject_Format(PyObject *o, PyObject *fmt);
int PyObject_Compare(PyObject *a, PyObject *b);
PyObject *PyObject_RichCompare(PyObject *a, PyObject *b, int op);
int PyObject_IsInstance(PyObject *inst, PyObject *cls);
int PyObject_IsSubclass(PyObject *derived, PyObject *cls);
PyObject *PyObject_Dir(PyObject *o);
PyObject *PyObject_GetIter(PyObject *o);
PyObject *PyObject_Bytes(PyObject *o);
PyObject *PyObject_Length(PyObject *o);
Py_ssize_t PyObject_LengthHint(PyObject *o, Py_ssize_t);
PyObject *PyObject_GetBuffer(PyObject *o, Py_buffer *view, int flags);
void PyBuffer_Release(Py_buffer *view);

typedef struct {
    void *buf;
    Py_ssize_t len;
    Py_ssize_t itemsize;
    int readonly;
    int ndim;
    char *format;
    Py_ssize_t *shape;
    Py_ssize_t *strides;
    Py_ssize_t *suboffsets;
    void *internal;
} Py_buffer;

int PyBuffer_FillInfo(Py_buffer *view, PyObject *o, void *buf, Py_ssize_t len, int readonly, int flags);
PyObject *PyBuffer_ReleaseAndGetMemory(PyObject *self);

PyObject *PyTuple_New(Py_ssize_t size);
PyObject *PyTuple_Pack(Py_ssize_t n, ...);
PyObject *PyTuple_GetItem(PyObject *o, Py_ssize_t i);
int PyTuple_SetItem(PyObject *o, Py_ssize_t i, PyObject *v);

PyObject *PyList_New(Py_ssize_t size);
int PyList_Append(PyObject *o, PyObject *v);
int PyList_Insert(PyObject *o, Py_ssize_t i, PyObject *v);
int PyList_Extend(PyObject *o, PyObject *v);
int PyList_Reverse(PyObject *o);
PyObject *PyList_Sort(PyObject *o);
PyObject *PyList_Index(PyObject *o, PyObject *v);
int PyList_GetSlice(PyObject *o, Py_ssize_t i1, Py_ssize_t i2, PyObject **v);
int PyList_SetSlice(PyObject *o, Py_ssize_t i1, Py_ssize_t i2, PyObject *v);

PyObject *PyDict_New(void);
int PyDict_SetItem(PyObject *o, PyObject *key, PyObject *val);
int PyDict_SetItemString(PyObject *o, const char *key, PyObject *val);
PyObject *PyDict_GetItem(PyObject *o, PyObject *key);
PyObject *PyDict_GetItemWithError(PyObject *o, PyObject *key);
PyObject *PyDict_GetItemString(PyObject *o, const char *key);
int PyDict_DelItem(PyObject *o, PyObject *key);
int PyDict_DelItemString(PyObject *o, const char *key);
void PyDict_Clear(PyObject *o);
PyObject *PyDict_Keys(PyObject *o);
PyObject *PyDict_Values(PyObject *o);
PyObject *PyDict_Items(PyObject *o);
Py_ssize_t PyDict_Size(PyObject *o);
int PyDict_Contains(PyObject *o, PyObject *key);
PyObject *PyDict_Copy(PyObject *o);
int PyDict_Update(PyObject *o, PyObject *other);
int PyDict_Merge(PyObject *o, PyObject *other, int override);
PyObject *PyDict_GetItemString(PyObject *dp, const char *key);

PyObject *PySet_New(PyObject *);
int PySet_Add(PyObject *set, PyObject *key);
int PySet_Discard(PyObject *set, PyObject *key);
PyObject *PySet_Pop(PyObject *set);
int PySet_Clear(PyObject *set);
Py_ssize_t PySet_Size(PyObject *set);
int PySet_Contains(PyObject *set, PyObject *key);

PyObject *PyUnicode_New(Py_ssize_t size, Py_UCS4 maxchar);
PyObject *PyUnicode_FromString(const char *u);
PyObject *PyUnicode_FromStringAndSize(const char *u, Py_ssize_t size);
PyObject *PyUnicode_Concat(PyObject *a, PyObject *b);
PyObject *PyUnicode_Split(PyObject *s, PyObject *sep, Py_ssize_t maxsplit);
PyObject *PyUnicode_Join(PyObject *sep, PyObject *seq);
PyObject *PyUnicode_Substring(PyObject *s, Py_ssize_t start, Py_ssize_t end);
PyObject *PyUnicode_Format(PyObject *format, PyObject *args);
PyObject *PyUnicode_FromFormatV(const char *format, va_list vargs);
PyObject *PyUnicode_FromFormat(const char *format, ...);
int PyUnicode_Compare(PyObject *a, PyObject *b);
int PyUnicode_CompareWithASCIIString(PyObject *u, const char *s);
PyObject *PyUnicode_RichCompare(PyObject *a, PyObject *b, int op);
Py_hash_t PyUnicode_Hash(PyObject *o);
PyObject *PyUnicode_Find(PyObject *haystack, PyObject *needle, Py_ssize_t start, Py_ssize_t end, int direction);
Py_ssize_t PyUnicode_Count(PyObject *haystack, PyObject *needle, Py_ssize_t start, Py_ssize_t end);
int PyUnicode_Startswith(PyObject *haystack, PyObject *suffix, Py_ssize_t start, Py_ssize_t end);
int PyUnicode_Endswith(PyObject *haystack, PyObject *suffix, Py_ssize_t start, Py_ssize_t end);
PyObject *PyUnicode_Replace(PyObject *s, PyObject *old, PyObject *newstr, Py_ssize_t maxcount);
PyObject *PyUnicode_DecodeUTF8(const char *u, Py_ssize_t size, const char *errors);
PyObject *PyUnicode_EncodeUTF8(PyObject *u, const char *errors);
PyObject *PyUnicode_AsUTF8String(PyObject *u);
char *PyUnicode_AsUTF8(PyObject *u);
char *PyUnicode_AsUTF8AndSize(PyObject *u, Py_ssize_t *size);
Py_UCS4 PyUnicode_ReadChar(PyObject *u, Py_ssize_t i);
int PyUnicode_WriteChar(PyObject *u, Py_ssize_t i, Py_UCS4 c);
PyObject *PyUnicode_TransformDecimalToUnicode(PyObject *u, PyObject *decimal_to_natvec);
int PyUnicode_IsDecimalDigit(Py_UCS4 c);
int PyUnicode_IsDigit(Py_UCS4 c);
int PyUnicode_IsLower(Py_UCS4 c);
int PyUnicode_IsUpper(Py_UCS4 c);
int PyUnicode_IsWhitespace(Py_UCS4 c);
int PyUnicode_IsLinebreak(Py_UCS4 c);
int PyUnicode_ToLower(Py_UCS4 c);
int PyUnicode_ToUpper(Py_UCS4 c);
int PyUnicode_ToTitleCase(Py_UCS4 c);
int PyUnicode_IsTitlecase(Py_UCS4 c);
PyObject *PyUnicode_Join(PyObject *separator, PyObject *seq);
PyObject *PyUnicode_Partition(PyObject *s, PyObject *sep);
PyObject *PyUnicode_RPartition(PyObject *s, PyObject *sep);
PyObject *PyUnicode_RSplit(PyObject *s, PyObject *sep, Py_ssize_t maxsplit);
PyObject *PyUnicode_Translate(PyObject *u, PyObject *table, const char *errors);
int PyUnicode_Contains(PyObject *container, PyObject *elem);
int PyUnicode_IsASCII(PyObject *u);

PyObject *PyBytes_FromString(const char *u);
PyObject *PyBytes_FromStringAndSize(const char *u, Py_ssize_t size);
PyObject *PyBytes_Concat(PyObject **a, PyObject *b);
void PyBytes_ConcatAndDel(PyObject **a, PyObject *b);
int PyBytes_Compare(PyObject *a, PyObject *b);
PyObject *PyBytes_Join(PyObject *s, PyObject *elem);
PyObject *PyBytes_Replace(PyObject *o, PyObject *old, PyObject *new, Py_ssize_t maxcount);
int PyBytes_Contains(PyObject *o, PyObject *arg);
PyObject *PyBytes_DecodeEscape(const char *s, Py_ssize_t size, const char *errors, Py_ssize_t go_ahead, int ignore);
Py_ssize_t PyBytes_Size(PyObject *o);
char *PyBytes_AsString(PyObject *o);
char *PyBytes_AS_STRING(PyObject *o);
int PyBytes_AsStringAndSize(PyObject *o, char **s, Py_ssize_t *len);
PyObject *PyBytes_Format(PyObject *fmt, PyObject *args);
PyObject *PyBytes_FromFormat(const char *format, ...);
PyObject *PyBytes_FromFormatV(const char *format, va_list vargs);
PyObject *PyBytes_AsStringEncoded(PyObject *u, const char *errors, const char *suffix);
int PyBytes_Resize(PyObject **o, Py_ssize_t newsize);

PyObject *PyInt_FromLong(long val);
long PyInt_AsLong(PyObject *o);
long PyInt_AsLongAndOverflow(PyObject *o, int *overflow);
PyObject *PyInt_FromSsize(Py_ssize_t val);
Py_ssize_t PyInt_AsSsize_t(PyObject *o);
unsigned long PyInt_AsUnsignedLong(PyObject *o);
unsigned long PyInt_AsUnsignedLongMask(PyObject *o);
int _PyInt_IsTrue(PyObject *o);

PyObject *PyFloat_FromDouble(double val);
double PyFloat_AsDouble(PyObject *o);

PyObject *PyBool_FromLong(long val);

PyObject *PyObject_Repr(PyObject *o);
PyObject *PyObject_Str(PyObject *o);
PyObject *PyObject_ASCII(PyObject *o);
PyObject *PyObject_Format(PyObject *o, PyObject *format_spec);
Py_hash_t PyObject_Hash(PyObject *o);
PyObject *PyObject_HashNotImplemented(PyObject *o);
int PyObject_Compare(PyObject *a, PyObject *b);
PyObject *PyObject_RichCompare(PyObject *a, PyObject *b, int op);
int PyObject_RichCompareBool(PyObject *a, PyObject *b, int op);
PyObject *PyObject_GetAttr(PyObject *o, PyObject *attr);
int PyObject_HasAttr(PyObject *o, PyObject *attr);
int PyObject_HasAttrString(PyObject *o, const char *attr);
int PyObject_SetAttr(PyObject *o, PyObject *attr, PyObject *v);
int PyObject_SetAttrString(PyObject *o, const char *attr, PyObject *v);
int PyObject_DelAttr(PyObject *o, PyObject *attr);
int PyObject_DelAttrString(PyObject *o, const char *attr);
PyObject *PyObject_GetItem(PyObject *o, PyObject *key);
int PyObject_SetItem(PyObject *o, PyObject *key, PyObject *v);
int PyObject_DelItem(PyObject *o, PyObject *key);
PyObject *PyObject_Dir(PyObject *o);
PyObject *PyObject_Dir0(PyObject *o);
PyObject *PyObject_Size(PyObject *o);
Py_ssize_t PyObject_Length(PyObject *o);
Py_ssize_t PyObject_LengthHint(PyObject *o, Py_ssize_t);
PyObject *PyObject_GetIter(PyObject *o);
PyObject *PyObject_GetAIter(PyObject *o);
PyObject *PyObject_GetANext(PyObject *o);
PyObject *PyObject_Bytes(PyObject *o);
PyObject *PyObject_Repr(PyObject *o);
PyObject *PyObject_Str(PyObject *o);
PyObject *PyObject_ASCII(PyObject *o);
PyObject *PyObject_Format(PyObject *o, PyObject *);
Py_hash_t PyObject_Hash(PyObject *o);
Py_hash_t PyObject_HashNotImplemented(PyObject *o);
int PyObject_IsTrue(PyObject *o);
int PyObject_Not(PyObject *o);
PyObject *_PyObject_StructSequence_GetItem(PyObject *, Py_ssize_t);
void _PyObject_StructSequence_InitType(PyObject *module, PyTypeObject *type, const char *name, const char *doc, Py_ssize_t n, Py_ssize_t ob_size);

#define Py_CLEAR(o) do { PyObject *_tmp = (PyObject *)(o); if (_tmp) { (o) = NULL; Py_DECREF(_tmp); } } while(0)
#define Py_VISIT(o) do { if (o) { int _v = visitor((PyObject *)(o), arg); if (_v) return _v; } } while(0)

#define PyExceptionInstance_Check(o) (Py_TYPE(o) == PyExc_BaseException || \
    PyObject_IsInstance(o, (PyObject *)PyExc_BaseException) > 0)

typedef int (*visitproc)(PyObject *, void *);
typedef int (*traverseproc)(PyObject *, visitproc, void *);

void _Py_NewReference(PyObject *o);

typedef struct _gc_head {
    struct _gc_head *gc_next;
    struct _gc_head *gc_prev;
    Py_ssize_t gc_refs;
} PyGC_Head;

extern PyGC_Head *_PyGC_generation0;
#define _PyGC_REFS_UNTRACKED (-2)
#define _PyGC_REFS_pending (-3)
#define _PyGC_REFS_finalized (-4)
#define _Py_AS_GC(o) ((PyGC_Head *)(o) - 1)
#define _PyGC_REFS(o) (_Py_AS_GC(o)->gc_refs)

void PyGC_Track(PyObject *o);
void PyGC_UnTrack(PyObject *o);
void PyGC_Collect(void);
Py_ssize_t PyGC_Collect2(int);
int PyGC_Enable(void);
int PyGC_Disable(void);
int PyGC_IsEnabled(void);
void PyGC_SetThreshold(Py_ssize_t, Py_ssize_t);

void _PyGC_Init(PyObject *o);

typedef struct {
    const char *name;
    PyObject **dict_addr;
    int strict;
} PyModuleDef;

PyObject *PyModule_New(const char *name);
PyObject *PyModule_GetDict(PyObject *m);
PyObject *PyModule_GetNameObject(PyObject *m);
const char *PyModule_GetName(PyObject *m);
PyObject *PyModule_GetFilenameObject(PyObject *m);
const char *PyModule_GetFilename(PyObject *m);
int PyModule_AddObject(PyObject *m, const char *name, PyObject *o);
int PyModule_AddObjectRef(PyObject *m, const char *name, PyObject *o);
int PyModule_AddStringConstant(PyObject *m, const char *name, const char *value);
int PyModule_AddIntConstant(PyObject *m, const char *name, long value);
int PyModule_AddIntMacro(PyObject *m, const char *name, int macro);

#define PyModuleDef_Init(m) ((PyObject *) m)
#define PyModule_GetState(m) ((void *)0)

typedef PyObject *(*PyModule_Slot)(PyObject *, int);
typedef struct {
    int slot;
    PyModule_Slot func;
} PyModuleDef_Slot;

#define Py_mod_create 1
#define Py_mod_exec 2
#define Py_mod_multiple_interpreters 3
#define Py_mod_gil 4

#define PyModuleDef_InitFunc(fn) \
    static PyObject *fn(void) { return NULL; }

#define PY_SPECIAL_METHOD 0
#define PY_WRAP_METHOD 1

typedef struct {
    int flags;
    const char *name;
    Py_ssize_t basicsize;
    Py_ssize_t itemsize;
    void (*destructor)(PyObject *);
    Py_PRINTF_FUNC printfunc print;
    Py_GETFUNC get;
    Py_SETFUNC set;
    Py_REPRFUNC repr;
    Py_as_number nb_add;
    Py_as_number nb_subtract;
    Py_as_number nb_multiply;
    Py_as_number nb_remainder;
    Py_as_number nb_divmod;
    Py_as_number nb_power;
    Py_as_number nb_negative;
    Py_as_number nb_positive;
    Py_as_number nb_absolute;
    Py_inquiry nb_bool;
    Py_unaryfunc nb_invert;
    Py_binaryfunc nb_lshift;
    Py_binaryfunc nb_rshift;
    Py_binaryfunc nb_and;
    Py_binaryfunc nb_xor;
    Py_binaryfunc nb_or;
    Py_tenaryfunc nb_power;
    Py_binaryfunc nb_subtract;
    void *nb_reserved;
    Py_binaryfunc nb_true_divide;
    Py_binaryfunc nb_floor_divide;
    Py_binaryfunc nb_index;
    Py_binaryfunc nb_matrix_multiply;
    Py_binaryfunc nb_inplace_matrix_multiply;
    Py_sequence sq_length;
    Py_concat_func concat;
    Py_repeat_func repeat;
    Py_ssizeargfunc sq_item;
    void *was_sq_slice;
    Py_voidfunc sq_clear;
    Py_objobjproc sq_contains;
    Py_binaryfunc sq_inplace_concat;
    Py_ssizeobjargproc sq_inplace_repeat;
    Py_setnextsqfunc sq_ass_item;
    void *was_sq_ass_slice;
    Py_objobjproc sq_contains;
    Py_destructor mp_length;
    Py_binaryfunc mp_subscript;
    Py_setitemproc mp_ass_subscript;
    Py_containsproc mp_contains;
    Py_context_mp_subscript mp_subscript;
    Py_context_ni Binary func mp_ass_subscript;
} PyNumberMethods;

typedef struct {
    lenfunc mp_length;
    binaryfunc mp_subscript;
    objobjargproc mp_ass_subscript;
    char *padded;
} PyMappingMethods;

typedef struct {
    lenfunc sq_length;
    binaryfunc sq_concat;
    ssizeargfunc sq_repeat;
    ssizeargfunc sq_item;
    void *was_sq_slice;
    ssizeobjargproc sq_ass_item;
    void *was_sq_ass_slice;
    objobjproc sq_contains;
    binaryfunc sq_inplace_concat;
    ssizeargfunc sq_inplace_repeat;
} PySequenceMethods;

typedef struct {
    hashfunc bf_hash;
    reprfunc bf_repr;
    getbufferproc bf_getbuffer;
    void *bf_releasebuffer;
} PyBufferProcs;

typedef struct {
    Py_ssize_t bp_index;
    char bp_buffer_format;
    Py_ssize_t bp_buffer;
} PyBuffer;
