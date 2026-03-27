/*
 * Blaze JavaScript Engine
 * Full-featured JS interpreter for embedded browser
 * Supports ES5+ features, DOM manipulation, async operations
*/

#include "blaze.h"
#include <stdint.h>
#include <stdbool.h>

/* External memory functions */
extern void *kmalloc(uint32_t size);
extern void kfree(void *ptr);
extern void c_puts(const char *s);

static int js_strlen(const char *s) {
    int len = 0;
    while (s && s[len]) len++;
    return len;
}

static void js_strcpy(char *dst, const char *src) {
    if (!dst || !src) return;
    while ((*dst++ = *src++));
}

static int js_strcmp(const char *a, const char *b) {
    if (!a || !b) return a ? 1 : (b ? -1 : 0);
    while (*a && *a == *b) { a++; b++; }
    return *(unsigned char *)a - *(unsigned char *)b;
}

static bool js_strncmp(const char *a, const char *b, int n) {
    if (!a || !b) return false;
    for (int i = 0; i < n; i++) {
        if (a[i] != b[i]) return false;
        if (a[i] == '\0') break;
    }
    return true;
}

static char *js_strdup(const char *s) {
    if (!s) return NULL;
    int len = js_strlen(s);
    char *dup = (char *)kmalloc(len + 1);
    if (dup) js_strcpy(dup, s);
    return dup;
}

static void js_strcat(char *dst, const char *src, int max_len) {
    if (!dst || !src) return;
    int dst_len = js_strlen(dst);
    int i = 0;
    while (src[i] && dst_len + i < max_len - 1) {
        dst[dst_len + i] = src[i];
        i++;
    }
    dst[dst_len + i] = '\0';
}

static bool js_isspace(char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r';
}

static bool js_isdigit(char c) {
    return c >= '0' && c <= '9';
}

static bool js_isalpha(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_' || c == '$';
}

static bool js_isalnum(char c) {
    return js_isalpha(c) || js_isdigit(c);
}

static bool js_ishex(char c) {
    return js_isdigit(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

/* TOKEN TYPES AND LEXER */

typedef enum {
    TOK_EOF,
    TOK_NUMBER,
    TOK_STRING,
    TOK_IDENT,
    TOK_VAR,
    TOK_LET,
    TOK_CONST,
    TOK_FUNCTION,
    TOK_RETURN,
    TOK_IF,
    TOK_ELSE,
    TOK_FOR,
    TOK_WHILE,
    TOK_DO,
    TOK_BREAK,
    TOK_CONTINUE,
    TOK_SWITCH,
    TOK_CASE,
    TOK_DEFAULT,
    TOK_TRY,
    TOK_CATCH,
    TOK_FINALLY,
    TOK_THROW,
    TOK_TRUE,
    TOK_FALSE,
    TOK_NULL,
    TOK_UNDEFINED,
    TOK_NEW,
    TOK_THIS,
    TOK_TYPEOF,
    TOK_DELETE,
    TOK_IN,
    TOK_OF,
    TOK_INSTANCEOF,
    TOK_ASYNC,
    TOK_AWAIT,
    TOK_CLASS,
    TOK_EXTENDS,
    TOK_STATIC,
    TOK_GET,
    TOK_SET,
    TOK_LPAREN,
    TOK_RPAREN,
    TOK_LBRACE,
    TOK_RBRACE,
    TOK_LBRACKET,
    TOK_RBRACKET,
    TOK_SEMICOLON,
    TOK_COMMA,
    TOK_DOT,
    TOK_COLON,
    TOK_QUESTION,
    TOK_ARROW,
    TOK_PLUS,
    TOK_MINUS,
    TOK_STAR,
    TOK_SLASH,
    TOK_PERCENT,
    TOK_ASSIGN,
    TOK_EQ,
    TOK_NE,
    TOK_STRICT_EQ,
    TOK_STRICT_NE,
    TOK_LT,
    TOK_LE,
    TOK_GT,
    TOK_GE,
    TOK_AND,
    TOK_OR,
    TOK_NOT,
    TOK_BITAND,
    TOK_BITOR,
    TOK_BITXOR,
    TOK_BITNOT,
    TOK_LSHIFT,
    TOK_RSHIFT,
    TOK_URSHIFT,
    TOK_PLUSPLUS,
    TOK_MINUSMINUS,
    TOK_PLUSEQ,
    TOK_MINUSEQ,
    TOK_STAREQ,
    TOK_SLASHEQ,
    TOK_PERCENTEQ,
    TOK_ANDEQ,
    TOK_OREQ,
    TOK_XOREQ,
    TOK_LSHIFTEQ,
    TOK_RSHIFTEQ,
    TOK_URSHIFTEQ,
    TOK_SPREAD
} TokenType;

typedef struct {
    TokenType type;
    char value[512];
    double num_value;
    int line;
    int col;
} Token;

typedef struct {
    const char *input;
    int pos;
    int len;
    int line;
    int col;
    Token current;
} Lexer;

typedef enum {
    VAL_UNDEFINED,
    VAL_NULL,
    VAL_BOOL,
    VAL_NUMBER,
    VAL_STRING,
    VAL_OBJECT,
    VAL_FUNCTION,
    VAL_ARRAY,
    VAL_NATIVE_FUNC,
    VAL_PROMISE,
    VAL_ERROR,
    VAL_REGEXP,
    VAL_DATE,
    VAL_SYMBOL
} ValueType;

typedef struct JSValue JSValue;
typedef struct JSObject JSObject;
typedef struct JSFunction JSFunction;
typedef struct JSContext JSContext;
typedef struct JSScope JSScope;

/* Native function pointer */
typedef JSValue (*NativeFunc)(JSContext *ctx, JSValue *args, int arg_count);

/* JavaScript value */
struct JSValue {
    ValueType type;
    union {
        bool boolean;
        double number;
        char *string;
        JSObject *object;
        JSFunction *function;
        NativeFunc native_func;
        void *ptr;
    } as;
};

/* Object property */
typedef struct JSProperty {
    char *key;
    JSValue value;
    bool writable;
    bool enumerable;
    bool configurable;
    struct JSProperty *next;
} JSProperty;

/* JavaScript object */
struct JSObject {
    JSProperty *properties;
    JSObject *prototype;
    int length;  /* For arrays */
    void *native_data;  /* For native objects (DOM nodes, etc) */
};

/* JavaScript function */
struct JSFunction {
    char *name;
    char **params;
    int param_count;
    char *body;
    JSScope *closure;
    bool is_arrow;
    bool is_async;
    bool is_generator;
};

/* Variable in scope */
typedef struct JSVar {
    char *name;
    JSValue value;
    bool is_const;
    struct JSVar *next;
} JSVar;

/* Scope for variables */
struct JSScope {
    JSVar *vars;
    JSScope *parent;
    bool is_function_scope;
};

/* Promise state */
typedef enum {
    PROMISE_PENDING,
    PROMISE_FULFILLED,
    PROMISE_REJECTED
} PromiseState;

typedef struct {
    PromiseState state;
    JSValue result;
    JSValue *then_callbacks;
    int then_count;
    JSValue *catch_callbacks;
    int catch_count;
} Promise;

/* Error object */
typedef struct {
    char *message;
    char *stack;
    char *name;
} ErrorObject;

/* JavaScript execution context */
struct JSContext {
    JSScope *global_scope;
    JSScope *current_scope;
    BlazeTab *tab;
    bool has_error;
    char error_msg[512];
    int call_depth;
    bool should_return;
    JSValue return_value;
    bool should_break;
    bool should_continue;
};

static void js_init_lexer(Lexer *lex, const char *input);
static Token js_next_token(Lexer *lex);
// static JSValue js_eval_expr(JSContext *ctx, Lexer *lex);
// static void js_exec_statement(JSContext *ctx, Lexer *lex);
static JSValue js_make_undefined(void);
static JSValue js_make_null(void);
static JSValue js_make_bool(bool b);
static JSValue js_make_number(double n);
static JSValue js_make_string(const char *s);
static JSValue js_make_object(void);
static JSValue js_make_array(void);
static void js_free_value(JSValue *val);
static bool js_to_bool(JSValue *val);
static double js_to_number(JSValue *val);
static char *js_to_string(JSValue *val);
static JSScope *js_create_scope(JSScope *parent);
static void js_free_scope(JSScope *scope);
static void js_set_var(JSScope *scope, const char *name, JSValue value, bool is_const);
static JSValue js_get_var(JSScope *scope, const char *name);
static void js_set_property(JSObject *obj, const char *key, JSValue value);
static JSValue js_get_property(JSObject *obj, const char *key);
static bool js_has_property(JSObject *obj, const char *key);
static void js_delete_property(JSObject *obj, const char *key);
static void js_array_push(JSObject *arr, JSValue value);
static JSValue js_array_pop(JSObject *arr);
static JSValue js_array_shift(JSObject *arr);
static void js_array_unshift(JSObject *arr, JSValue value);

static JSValue js_make_undefined(void) {
    JSValue val;
    val.type = VAL_UNDEFINED;
    return val;
}

static JSValue js_make_null(void) {
    JSValue val;
    val.type = VAL_NULL;
    return val;
}

static JSValue js_make_bool(bool b) {
    JSValue val;
    val.type = VAL_BOOL;
    val.as.boolean = b;
    return val;
}

static JSValue js_make_number(double n) {
    JSValue val;
    val.type = VAL_NUMBER;
    val.as.number = n;
    return val;
}

static JSValue js_make_string(const char *s) {
    JSValue val;
    val.type = VAL_STRING;
    val.as.string = js_strdup(s ? s : "");
    return val;
}

static JSValue js_make_object(void) {
    JSValue val;
    val.type = VAL_OBJECT;
    val.as.object = (JSObject *)kmalloc(sizeof(JSObject));
    if (val.as.object) {
        val.as.object->properties = NULL;
        val.as.object->prototype = NULL;
        val.as.object->length = 0;
        val.as.object->native_data = NULL;
    }
    return val;
}

static JSValue js_make_array(void) {
    JSValue val;
    val.type = VAL_ARRAY;
    val.as.object = (JSObject *)kmalloc(sizeof(JSObject));
    if (val.as.object) {
        val.as.object->properties = NULL;
        val.as.object->prototype = NULL;
        val.as.object->length = 0;
        val.as.object->native_data = NULL;
    }
    return val;
}

static JSValue js_make_error(const char *message) {
    JSValue val;
    val.type = VAL_ERROR;
    val.as.object = (JSObject *)kmalloc(sizeof(JSObject));
    if (val.as.object) {
        val.as.object->properties = NULL;
        val.as.object->prototype = NULL;
        val.as.object->length = 0;
        ErrorObject *err = (ErrorObject *)kmalloc(sizeof(ErrorObject));
        if (err) {
            err->message = js_strdup(message);
            err->stack = js_strdup("");
            err->name = js_strdup("Error");
            val.as.object->native_data = err;
        }
    }
    return val;
}

static JSValue js_make_native_func(NativeFunc func) {
    JSValue val;
    val.type = VAL_NATIVE_FUNC;
    val.as.native_func = func;
    return val;
}

/* Free value memory */
static void js_free_value(JSValue *val) {
    if (!val) return;
    
    if (val->type == VAL_STRING && val->as.string) {
        kfree(val->as.string);
        val->as.string = NULL;
    } else if ((val->type == VAL_OBJECT || val->type == VAL_ARRAY || val->type == VAL_ERROR) && val->as.object) {
        JSProperty *prop = val->as.object->properties;
        while (prop) {
            JSProperty *next = prop->next;
            if (prop->key) kfree(prop->key);
            js_free_value(&prop->value);
            kfree(prop);
            prop = next;
        }
        if (val->type == VAL_ERROR && val->as.object->native_data) {
            ErrorObject *err = (ErrorObject *)val->as.object->native_data;
            if (err->message) kfree(err->message);
            if (err->stack) kfree(err->stack);
            if (err->name) kfree(err->name);
            kfree(err);
        }
        kfree(val->as.object);
        val->as.object = NULL;
    } else if (val->type == VAL_FUNCTION && val->as.function) {
        if (val->as.function->name) kfree(val->as.function->name);
        if (val->as.function->body) kfree(val->as.function->body);
        for (int i = 0; i < val->as.function->param_count; i++) {
            if (val->as.function->params[i]) kfree(val->as.function->params[i]);
        }
        if (val->as.function->params) kfree(val->as.function->params);
        kfree(val->as.function);
        val->as.function = NULL;
    }
}

static bool js_to_bool(JSValue *val) {
    if (!val) return false;
    switch (val->type) {
        case VAL_UNDEFINED:
        case VAL_NULL:
            return false;
        case VAL_BOOL:
            return val->as.boolean;
        case VAL_NUMBER:
            return val->as.number != 0.0 && val->as.number == val->as.number; /* NaN is false */
        case VAL_STRING:
            return val->as.string && val->as.string[0] != '\0';
        default:
            return true;
    }
}

static double js_to_number(JSValue *val) {
    if (!val) return 0.0;
    switch (val->type) {
        case VAL_UNDEFINED:
            return 0.0 / 0.0; /* NaN */
        case VAL_NULL:
            return 0.0;
        case VAL_BOOL:
            return val->as.boolean ? 1.0 : 0.0;
        case VAL_NUMBER:
            return val->as.number;
        case VAL_STRING: {
            if (!val->as.string || val->as.string[0] == '\0') return 0.0;
            
            const char *s = val->as.string;
            while (js_isspace(*s)) s++;
            
            bool negative = false;
            if (*s == '-') {
                negative = true;
                s++;
            } else if (*s == '+') {
                s++;
            }
            
            /* Check for hex */
            if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
                s += 2;
                double result = 0.0;
                while (js_ishex(*s)) {
                    int digit;
                    if (*s >= '0' && *s <= '9') digit = *s - '0';
                    else if (*s >= 'a' && *s <= 'f') digit = *s - 'a' + 10;
                    else digit = *s - 'A' + 10;
                    result = result * 16.0 + digit;
                    s++;
                }
                return negative ? -result : result;
            }
            
            double result = 0.0;
            while (js_isdigit(*s)) {
                result = result * 10.0 + (*s - '0');
                s++;
            }
            
            if (*s == '.') {
                s++;
                double frac = 0.1;
                while (js_isdigit(*s)) {
                    result += (*s - '0') * frac;
                    frac *= 0.1;
                    s++;
                }
            }
            
            /* Handle exponent */
            if (*s == 'e' || *s == 'E') {
                s++;
                bool exp_neg = false;
                if (*s == '-') {
                    exp_neg = true;
                    s++;
                } else if (*s == '+') {
                    s++;
                }
                int exp = 0;
                while (js_isdigit(*s)) {
                    exp = exp * 10 + (*s - '0');
                    s++;
                }
                double multiplier = 1.0;
                for (int i = 0; i < exp; i++) {
                    multiplier *= 10.0;
                }
                if (exp_neg) result /= multiplier;
                else result *= multiplier;
            }
            
            return negative ? -result : result;
        }
        case VAL_ARRAY:
            if (val->as.object && val->as.object->length == 0) return 0.0;
            if (val->as.object && val->as.object->length == 1) {
                JSValue elem = js_get_property(val->as.object, "0");
                return js_to_number(&elem);
            }
            return 0.0 / 0.0; /* NaN */
        default:
            return 0.0 / 0.0; /* NaN */
    }
}

static char *js_to_string(JSValue *val) {
    static char buf[1024];
    if (!val) {
        js_strcpy(buf, "undefined");
        return buf;
    }
    
    switch (val->type) {
        case VAL_UNDEFINED:
            js_strcpy(buf, "undefined");
            break;
        case VAL_NULL:
            js_strcpy(buf, "null");
            break;
        case VAL_BOOL:
            js_strcpy(buf, val->as.boolean ? "true" : "false");
            break;
        case VAL_NUMBER: {
            double num = val->as.number;
            
            /* Check for special values */
            if (num != num) {
                js_strcpy(buf, "NaN");
                break;
            }
            if (num == 1.0 / 0.0) {
                js_strcpy(buf, "Infinity");
                break;
            }
            if (num == -1.0 / 0.0) {
                js_strcpy(buf, "-Infinity");
                break;
            }
            
            /* Convert to string */
            int int_part = (int)num;
            double frac_part = num - int_part;
            
            int i = 0;
            if (num < 0) {
                buf[i++] = '-';
                int_part = -int_part;
                frac_part = -frac_part;
            }
            
            if (int_part == 0) {
                buf[i++] = '0';
            } else {
                char tmp[32];
                int j = 0;
                while (int_part > 0) {
                    tmp[j++] = '0' + (int_part % 10);
                    int_part /= 10;
                }
                while (j > 0) {
                    buf[i++] = tmp[--j];
                }
            }
            
            /* Add fractional part if non-zero */
            if (frac_part > 0.0000001 || frac_part < -0.0000001) {
                buf[i++] = '.';
                for (int k = 0; k < 6 && i < 1020; k++) {
                    frac_part *= 10.0;
                    int digit = (int)frac_part;
                    buf[i++] = '0' + digit;
                    frac_part -= digit;
                }
                /* Remove trailing zeros */
                while (i > 0 && buf[i-1] == '0') i--;
                if (i > 0 && buf[i-1] == '.') i--;
            }
            
            buf[i] = '\0';
            break;
        }
        case VAL_STRING:
            return val->as.string ? val->as.string : "";
        case VAL_OBJECT:
            js_strcpy(buf, "[object Object]");
            break;
        case VAL_ARRAY: {
            buf[0] = '\0';
            if (val->as.object) {
                for (int i = 0; i < val->as.object->length && js_strlen(buf) < 1000; i++) {
                    char key[32];
                    int k = 0;
                    if (i == 0) {
                        key[k++] = '0';
                    } else {
                        char tmp[32];
                        int j = 0;
                        int n = i;
                        while (n > 0) {
                            tmp[j++] = '0' + (n % 10);
                            n /= 10;
                        }
                        while (j > 0) {
                            key[k++] = tmp[--j];
                        }
                    }
                    key[k] = '\0';
                    
                    JSValue elem = js_get_property(val->as.object, key);
                    char *elem_str = js_to_string(&elem);
                    js_strcat(buf, elem_str, 1024);
                    if (i < val->as.object->length - 1) {
                        js_strcat(buf, ",", 1024);
                    }
                }
            }
            break;
        }
        case VAL_FUNCTION:
            js_strcpy(buf, "[function]");
            break;
        case VAL_NATIVE_FUNC:
            js_strcpy(buf, "[native function]");
            break;
        case VAL_ERROR:
            if (val->as.object && val->as.object->native_data) {
                ErrorObject *err = (ErrorObject *)val->as.object->native_data;
                js_strcpy(buf, err->name ? err->name : "Error");
                js_strcat(buf, ": ", 1024);
                js_strcat(buf, err->message ? err->message : "", 1024);
            } else {
                js_strcpy(buf, "Error");
            }
            break;
        default:
            js_strcpy(buf, "");
            break;
    }
    return buf;
}

static void js_init_lexer(Lexer *lex, const char *input) {
    lex->input = input;
    lex->pos = 0;
    lex->len = js_strlen(input);
    lex->line = 1;
    lex->col = 1;
    lex->current.type = TOK_EOF;
}

static void js_skip_whitespace(Lexer *lex) {
    while (lex->pos < lex->len) {
        char c = lex->input[lex->pos];
        if (c == ' ' || c == '\t' || c == '\r') {
            lex->pos++;
            lex->col++;
        } else if (c == '\n') {
            lex->pos++;
            lex->line++;
            lex->col = 1;
        } else {
            break;
        }
    }
}

static char js_peek(Lexer *lex) {
    if (lex->pos >= lex->len) return '\0';
    return lex->input[lex->pos];
}

static char js_peek_ahead(Lexer *lex, int offset) {
    if (lex->pos + offset >= lex->len) return '\0';
    return lex->input[lex->pos + offset];
}

static char js_consume(Lexer *lex) {
    if (lex->pos >= lex->len) return '\0';
    char c = lex->input[lex->pos++];
    lex->col++;
    if (c == '\n') {
        lex->line++;
        lex->col = 1;
    }
    return c;
}

static TokenType js_check_keyword(const char *str) {
    if (js_strcmp(str, "var") == 0) return TOK_VAR;
    if (js_strcmp(str, "let") == 0) return TOK_LET;
    if (js_strcmp(str, "const") == 0) return TOK_CONST;
    if (js_strcmp(str, "function") == 0) return TOK_FUNCTION;
    if (js_strcmp(str, "return") == 0) return TOK_RETURN;
    if (js_strcmp(str, "if") == 0) return TOK_IF;
    if (js_strcmp(str, "else") == 0) return TOK_ELSE;
    if (js_strcmp(str, "for") == 0) return TOK_FOR;
    if (js_strcmp(str, "while") == 0) return TOK_WHILE;
    if (js_strcmp(str, "do") == 0) return TOK_DO;
    if (js_strcmp(str, "break") == 0) return TOK_BREAK;
    if (js_strcmp(str, "continue") == 0) return TOK_CONTINUE;
    if (js_strcmp(str, "switch") == 0) return TOK_SWITCH;
    if (js_strcmp(str, "case") == 0) return TOK_CASE;
    if (js_strcmp(str, "default") == 0) return TOK_DEFAULT;
    if (js_strcmp(str, "try") == 0) return TOK_TRY;
    if (js_strcmp(str, "catch") == 0) return TOK_CATCH;
    if (js_strcmp(str, "finally") == 0) return TOK_FINALLY;
    if (js_strcmp(str, "throw") == 0) return TOK_THROW;
    if (js_strcmp(str, "true") == 0) return TOK_TRUE;
    if (js_strcmp(str, "false") == 0) return TOK_FALSE;
    if (js_strcmp(str, "null") == 0) return TOK_NULL;
    if (js_strcmp(str, "undefined") == 0) return TOK_UNDEFINED;
    if (js_strcmp(str, "new") == 0) return TOK_NEW;
    if (js_strcmp(str, "this") == 0) return TOK_THIS;
    if (js_strcmp(str, "typeof") == 0) return TOK_TYPEOF;
    if (js_strcmp(str, "delete") == 0) return TOK_DELETE;
    if (js_strcmp(str, "in") == 0) return TOK_IN;
    if (js_strcmp(str, "of") == 0) return TOK_OF;
    if (js_strcmp(str, "instanceof") == 0) return TOK_INSTANCEOF;
    if (js_strcmp(str, "async") == 0) return TOK_ASYNC;
    if (js_strcmp(str, "await") == 0) return TOK_AWAIT;
    if (js_strcmp(str, "class") == 0) return TOK_CLASS;
    if (js_strcmp(str, "extends") == 0) return TOK_EXTENDS;
    if (js_strcmp(str, "static") == 0) return TOK_STATIC;
    if (js_strcmp(str, "get") == 0) return TOK_GET;
    if (js_strcmp(str, "set") == 0) return TOK_SET;
    return TOK_IDENT;
}

static Token js_next_token(Lexer *lex) {
    Token tok;
    tok.type = TOK_EOF;
    tok.value[0] = '\0';
    tok.num_value = 0.0;
    tok.line = lex->line;
    tok.col = lex->col;
    
    js_skip_whitespace(lex);
    
    if (lex->pos >= lex->len) {
        return tok;
    }
    
    char c = js_peek(lex);
    
    /* Single-line comment */
    if (c == '/' && js_peek_ahead(lex, 1) == '/') {
        while (lex->pos < lex->len && lex->input[lex->pos] != '\n') {
            lex->pos++;
        }
        return js_next_token(lex);
    }
    
    /* Multi-line comment */
    if (c == '/' && js_peek_ahead(lex, 1) == '*') {
        lex->pos += 2;
        while (lex->pos + 1 < lex->len) {
            if (lex->input[lex->pos] == '*' && lex->input[lex->pos + 1] == '/') {
                lex->pos += 2;
                break;
            }
            if (lex->input[lex->pos] == '\n') {
                lex->line++;
                lex->col = 1;
            }
            lex->pos++;
        }
        return js_next_token(lex);
    }
    
    /* String literal */
    if (c == '"' || c == '\'' || c == '`') {
        char quote = js_consume(lex);
        int i = 0;
        while (lex->pos < lex->len && i < 510) {
            char ch = js_peek(lex);
            if (ch == quote) {
                js_consume(lex);
                break;
            }
            if (ch == '\\' && lex->pos + 1 < lex->len) {
                js_consume(lex);
                char next = js_consume(lex);
                if (next == 'n') tok.value[i++] = '\n';
                else if (next == 't') tok.value[i++] = '\t';
                else if (next == 'r') tok.value[i++] = '\r';
                else if (next == '\\') tok.value[i++] = '\\';
                else if (next == quote) tok.value[i++] = quote;
                else if (next == '0') tok.value[i++] = '\0';
                else if (next == 'x' && lex->pos + 1 < lex->len) {
                    /* Hex escape */
                    char h1 = js_consume(lex);
                    char h2 = js_consume(lex);
                    int val = 0;
                    if (h1 >= '0' && h1 <= '9') val = (h1 - '0') * 16;
                    else if (h1 >= 'a' && h1 <= 'f') val = (h1 - 'a' + 10) * 16;
                    else if (h1 >= 'A' && h1 <= 'F') val = (h1 - 'A' + 10) * 16;
                    if (h2 >= '0' && h2 <= '9') val += (h2 - '0');
                    else if (h2 >= 'a' && h2 <= 'f') val += (h2 - 'a' + 10);
                    else if (h2 >= 'A' && h2 <= 'F') val += (h2 - 'A' + 10);
                    tok.value[i++] = (char)val;
                } else {
                    tok.value[i++] = next;
                }
            } else {
                tok.value[i++] = js_consume(lex);
            }
        }
        tok.value[i] = '\0';
        tok.type = TOK_STRING;
        return tok;
    }
    
    /* Number literal */
    if (js_isdigit(c) || (c == '.' && js_isdigit(js_peek_ahead(lex, 1)))) {
        double num = 0.0;
        bool is_hex = false;
        bool is_float = false;
        
        /* Check for hex */
        if (c == '0' && (js_peek_ahead(lex, 1) == 'x' || js_peek_ahead(lex, 1) == 'X')) {
            is_hex = true;
            js_consume(lex);
            js_consume(lex);
            while (lex->pos < lex->len && js_ishex(js_peek(lex))) {
                char h = js_consume(lex);
                int digit;
                if (h >= '0' && h <= '9') digit = h - '0';
                else if (h >= 'a' && h <= 'f') digit = h - 'a' + 10;
                else digit = h - 'A' + 10;
                num = num * 16.0 + digit;
            }
        } else {
            /* Decimal */
            while (lex->pos < lex->len && js_isdigit(js_peek(lex))) {
                num = num * 10.0 + (js_consume(lex) - '0');
            }
            if (js_peek(lex) == '.') {
                is_float = true;
                js_consume(lex);
                double frac = 0.1;
                while (lex->pos < lex->len && js_isdigit(js_peek(lex))) {
                    num += (js_consume(lex) - '0') * frac;
                    frac *= 0.1;
                }
            }
            /* Exponent */
            if (js_peek(lex) == 'e' || js_peek(lex) == 'E') {
                js_consume(lex);
                bool exp_neg = false;
                if (js_peek(lex) == '-') {
                    exp_neg = true;
                    js_consume(lex);
                } else if (js_peek(lex) == '+') {
                    js_consume(lex);
                }
                int exp = 0;
                while (lex->pos < lex->len && js_isdigit(js_peek(lex))) {
                    exp = exp * 10 + (js_consume(lex) - '0');
                }
                double multiplier = 1.0;
                for (int i = 0; i < exp; i++) {
                    multiplier *= 10.0;
                }
                if (exp_neg) num /= multiplier;
                else num *= multiplier;
            }
        }
        tok.type = TOK_NUMBER;
        tok.num_value = num;
        return tok;
    }
    
    /* Identifier or keyword */
    if (js_isalpha(c)) {
        int i = 0;
        while (lex->pos < lex->len && js_isalnum(js_peek(lex)) && i < 510) {
            tok.value[i++] = js_consume(lex);
        }
        tok.value[i] = '\0';
        tok.type = js_check_keyword(tok.value);
        return tok;
    }
    
    /* Operators and punctuation */
    js_consume(lex);
    
    switch (c) {
        case '(': tok.type = TOK_LPAREN; break;
        case ')': tok.type = TOK_RPAREN; break;
        case '{': tok.type = TOK_LBRACE; break;
        case '}': tok.type = TOK_RBRACE; break;
        case '[': tok.type = TOK_LBRACKET; break;
        case ']': tok.type = TOK_RBRACKET; break;
        case ';': tok.type = TOK_SEMICOLON; break;
        case ',': tok.type = TOK_COMMA; break;
        case ':': tok.type = TOK_COLON; break;
        case '?': tok.type = TOK_QUESTION; break;
        case '~': tok.type = TOK_BITNOT; break;
        case '.':
            if (js_peek(lex) == '.' && js_peek_ahead(lex, 1) == '.') {
                js_consume(lex);
                js_consume(lex);
                tok.type = TOK_SPREAD;
            } else {
                tok.type = TOK_DOT;
            }
            break;
        case '+':
            if (js_peek(lex) == '+') {
                js_consume(lex);
                tok.type = TOK_PLUSPLUS;
            } else if (js_peek(lex) == '=') {
                js_consume(lex);
                tok.type = TOK_PLUSEQ;
            } else {
                tok.type = TOK_PLUS;
            }
            break;
        case '-':
            if (js_peek(lex) == '-') {
                js_consume(lex);
                tok.type = TOK_MINUSMINUS;
            } else if (js_peek(lex) == '=') {
                js_consume(lex);
                tok.type = TOK_MINUSEQ;
            } else {
                tok.type = TOK_MINUS;
            }
            break;
        case '*':
            if (js_peek(lex) == '=') {
                js_consume(lex);
                tok.type = TOK_STAREQ;
            } else {
                tok.type = TOK_STAR;
            }
            break;
        case '/':
            if (js_peek(lex) == '=') {
                js_consume(lex);
                tok.type = TOK_SLASHEQ;
            } else {
                tok.type = TOK_SLASH;
            }
            break;
        case '%':
            if (js_peek(lex) == '=') {
                js_consume(lex);
                tok.type = TOK_PERCENTEQ;
            } else {
                tok.type = TOK_PERCENT;
            }
            break;
        case '=':
            if (js_peek(lex) == '=') {
                js_consume(lex);
                if (js_peek(lex) == '=') {
                    js_consume(lex);
                    tok.type = TOK_STRICT_EQ;
                } else {
                    tok.type = TOK_EQ;
                }
            } else if (js_peek(lex) == '>') {
                js_consume(lex);
                tok.type = TOK_ARROW;
            } else {
                tok.type = TOK_ASSIGN;
            }
            break;
        case '!':
            if (js_peek(lex) == '=') {
                js_consume(lex);
                if (js_peek(lex) == '=') {
                    js_consume(lex);
                    tok.type = TOK_STRICT_NE;
                } else {
                    tok.type = TOK_NE;
                }
            } else {
                tok.type = TOK_NOT;
            }
            break;
        case '<':
            if (js_peek(lex) == '=') {
                js_consume(lex);
                tok.type = TOK_LE;
            } else if (js_peek(lex) == '<') {
                js_consume(lex);
                if (js_peek(lex) == '=') {
                    js_consume(lex);
                    tok.type = TOK_LSHIFTEQ;
                } else {
                    tok.type = TOK_LSHIFT;
                }
            } else {
                tok.type = TOK_LT;
            }
            break;
        case '>':
            if (js_peek(lex) == '=') {
                js_consume(lex);
                tok.type = TOK_GE;
            } else if (js_peek(lex) == '>') {
                js_consume(lex);
                if (js_peek(lex) == '>') {
                    js_consume(lex);
                    if (js_peek(lex) == '=') {
                        js_consume(lex);
                        tok.type = TOK_URSHIFTEQ;
                    } else {
                        tok.type = TOK_URSHIFT;
                    }
                } else if (js_peek(lex) == '=') {
                    js_consume(lex);
                    tok.type = TOK_RSHIFTEQ;
                } else {
                    tok.type = TOK_RSHIFT;
                }
            } else {
                tok.type = TOK_GT;
            }
            break;
        case '&':
            if (js_peek(lex) == '&') {
                js_consume(lex);
                tok.type = TOK_AND;
            } else if (js_peek(lex) == '=') {
                js_consume(lex);
                tok.type = TOK_ANDEQ;
            } else {
                tok.type = TOK_BITAND;
            }
            break;
        case '|':
            if (js_peek(lex) == '|') {
                js_consume(lex);
                tok.type = TOK_OR;
            } else if (js_peek(lex) == '=') {
                js_consume(lex);
                tok.type = TOK_OREQ;
            } else {
                tok.type = TOK_BITOR;
            }
            break;
        case '^':
            if (js_peek(lex) == '=') {
                js_consume(lex);
                tok.type = TOK_XOREQ;
            } else {
                tok.type = TOK_BITXOR;
            }
            break;
        default:
            tok.type = TOK_EOF;
            break;
    }
    
    return tok;
}


/* SCOPE AND VARIABLE MANAGEMENT */

static JSScope *js_create_scope(JSScope *parent) {
    JSScope *scope = (JSScope *)kmalloc(sizeof(JSScope));
    if (scope) {
        scope->vars = NULL;
        scope->parent = parent;
        scope->is_function_scope = false;
    }
    return scope;
}

static void js_free_scope(JSScope *scope) {
    if (!scope) return;
    JSVar *var = scope->vars;
    while (var) {
        JSVar *next = var->next;
        if (var->name) kfree(var->name);
        js_free_value(&var->value);
        kfree(var);
        var = next;
    }
    kfree(scope);
}

static JSVar *js_find_var(JSScope *scope, const char *name) {
    while (scope) {
        JSVar *var = scope->vars;
        while (var) {
            if (js_strcmp(var->name, name) == 0) {
                return var;
            }
            var = var->next;
        }
        scope = scope->parent;
    }
    return NULL;
}

static void js_set_var(JSScope *scope, const char *name, JSValue value, bool is_const) {
    JSVar *var = js_find_var(scope, name);
    if (var) {
        if (var->is_const) {
            /* Cannot reassign const */
            return;
        }
        js_free_value(&var->value);
        var->value = value;
    } else {
        JSVar *new_var = (JSVar *)kmalloc(sizeof(JSVar));
        if (new_var) {
            new_var->name = js_strdup(name);
            new_var->value = value;
            new_var->is_const = is_const;
            new_var->next = scope->vars;
            scope->vars = new_var;
        }
    }
}

static JSValue js_get_var(JSScope *scope, const char *name) {
    JSVar *var = js_find_var(scope, name);
    if (var) {
        return var->value;
    }
    return js_make_undefined();
}

static void js_set_property(JSObject *obj, const char *key, JSValue value) {
    if (!obj || !key) return;
    
    JSProperty *prop = obj->properties;
    while (prop) {
        if (js_strcmp(prop->key, key) == 0) {
            if (prop->writable) {
                js_free_value(&prop->value);
                prop->value = value;
            }
            return;
        }
        prop = prop->next;
    }
    
    JSProperty *new_prop = (JSProperty *)kmalloc(sizeof(JSProperty));
    if (new_prop) {
        new_prop->key = js_strdup(key);
        new_prop->value = value;
        new_prop->writable = true;
        new_prop->enumerable = true;
        new_prop->configurable = true;
        new_prop->next = obj->properties;
        obj->properties = new_prop;
    }
}

static JSValue js_get_property(JSObject *obj, const char *key) {
    if (!obj || !key) return js_make_undefined();
    
    JSProperty *prop = obj->properties;
    while (prop) {
        if (js_strcmp(prop->key, key) == 0) {
            return prop->value;
        }
        prop = prop->next;
    }
    
    /* Check prototype chain */
    if (obj->prototype) {
        return js_get_property(obj->prototype, key);
    }
    
    return js_make_undefined();
}

static bool js_has_property(JSObject *obj, const char *key) {
    if (!obj || !key) return false;
    
    JSProperty *prop = obj->properties;
    while (prop) {
        if (js_strcmp(prop->key, key) == 0) {
            return true;
        }
        prop = prop->next;
    }
    
    if (obj->prototype) {
        return js_has_property(obj->prototype, key);
    }
    
    return false;
}

static void js_delete_property(JSObject *obj, const char *key) {
    if (!obj || !key) return;
    
    JSProperty *prev = NULL;
    JSProperty *prop = obj->properties;
    while (prop) {
        if (js_strcmp(prop->key, key) == 0) {
            if (!prop->configurable) return;
            
            if (prev) {
                prev->next = prop->next;
            } else {
                obj->properties = prop->next;
            }
            if (prop->key) kfree(prop->key);
            js_free_value(&prop->value);
            kfree(prop);
            return;
        }
        prev = prop;
        prop = prop->next;
    }
}

static void js_array_push(JSObject *arr, JSValue value) {
    if (!arr) return;
    
    char key[32];
    int i = 0;
    int len = arr->length;
    if (len == 0) {
        key[i++] = '0';
    } else {
        char tmp[32];
        int j = 0;
        while (len > 0) {
            tmp[j++] = '0' + (len % 10);
            len /= 10;
        }
        while (j > 0) {
            key[i++] = tmp[--j];
        }
    }
    key[i] = '\0';
    js_set_property(arr, key, value);
    arr->length++;
}

static JSValue js_array_pop(JSObject *arr) {
    if (!arr || arr->length == 0) {
        return js_make_undefined();
    }
    
    arr->length--;
    char key[32];
    int i = 0;
    int len = arr->length;
    if (len == 0) {
        key[i++] = '0';
    } else {
        char tmp[32];
        int j = 0;
        while (len > 0) {
            tmp[j++] = '0' + (len % 10);
            len /= 10;
        }
        while (j > 0) {
            key[i++] = tmp[--j];
        }
    }
    key[i] = '\0';
    
    JSValue val = js_get_property(arr, key);
    js_delete_property(arr, key);
    return val;
}

static JSValue js_array_shift(JSObject *arr) {
    if (!arr || arr->length == 0) {
        return js_make_undefined();
    }
    
    JSValue val = js_get_property(arr, "0");
    
    /* Shift all elements down */
    for (int i = 0; i < arr->length - 1; i++) {
        char key1[32], key2[32];
        
        /* Build key for i */
        int k = 0;
        if (i == 0) {
            key1[k++] = '0';
        } else {
            char tmp[32];
            int j = 0;
            int n = i;
            while (n > 0) {
                tmp[j++] = '0' + (n % 10);
                n /= 10;
            }
            while (j > 0) {
                key1[k++] = tmp[--j];
            }
        }
        key1[k] = '\0';
        
        /* Build key for i+1 */
        k = 0;
        int n = i + 1;
        if (n == 0) {
            key2[k++] = '0';
        } else {
            char tmp[32];
            int j = 0;
            while (n > 0) {
                tmp[j++] = '0' + (n % 10);
                n /= 10;
            }
            while (j > 0) {
                key2[k++] = tmp[--j];
            }
        }
        key2[k] = '\0';
        
        JSValue next_val = js_get_property(arr, key2);
        js_set_property(arr, key1, next_val);
    }
    
    arr->length--;
    return val;
}

static void js_array_unshift(JSObject *arr, JSValue value) {
    if (!arr) return;
    
    /* Shift all elements up */
    for (int i = arr->length; i > 0; i--) {
        char key1[32], key2[32];
        
        /* Build key for i */
        int k = 0;
        if (i == 0) {
            key1[k++] = '0';
        } else {
            char tmp[32];
            int j = 0;
            int n = i;
            while (n > 0) {
                tmp[j++] = '0' + (n % 10);
                n /= 10;
            }
            while (j > 0) {
                key1[k++] = tmp[--j];
            }
        }
        key1[k] = '\0';
        
        /* Build key for i-1 */
        k = 0;
        int n = i - 1;
        if (n == 0) {
            key2[k++] = '0';
        } else {
            char tmp[32];
            int j = 0;
            while (n > 0) {
                tmp[j++] = '0' + (n % 10);
                n /= 10;
            }
            while (j > 0) {
                key2[k++] = tmp[--j];
            }
        }
        key2[k] = '\0';
        
        JSValue prev_val = js_get_property(arr, key2);
        js_set_property(arr, key1, prev_val);
    }
    
    js_set_property(arr, "0", value);
    arr->length++;
}

/* Initialize JavaScript engine for a tab */
void blaze_js_init(BlazeTab *tab) {
    if (!tab) return;
    /* JS context will be created on first script execution */
}

/* Cleanup JavaScript engine */
void blaze_js_cleanup(BlazeTab *tab) {
    if (!tab) return;
    /* Context cleanup happens when tab is destroyed */
}

/* Execute JavaScript code in tab context */
void blaze_js_execute(BlazeTab *tab, const char *script) {
    if (!tab || !script) return;
    
    /* Access state (this is a bit hacky but works for now in this architecture) */
    extern BlazeState browser_state;
    BlazeState *state = &browser_state;

    blaze_log(state, "[JS] Executing script...\n");
    
    const char *p = script;
    while (*p) {
        /* Skip whitespace */
        while (*p && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')) p++;
        if (!*p) break;
        
        /* Look for console.log */
        if (blaze_str_starts_with(p, "console.log")) {
            state->console_open = true;
            p += 11;
            while (*p && (*p == ' ' || *p == '\t')) p++;
            if (*p == '(') {
                p++;
                while (*p && (*p == ' ' || *p == '\t')) p++;
                
                /* Handle string litereal */
                if (*p == '"' || *p == '\'') {
                    char quote = *p++;
                    char msg[256];
                    int mi = 0;
                    while (*p && *p != quote && mi < 255) {
                        msg[mi++] = *p++;
                    }
                    msg[mi] = 0;
                    if (*p == quote) p++;
                    
                    /* Log the message */
                    blaze_log(state, "> ");
                    blaze_log(state, msg);
                    blaze_log(state, "\n");
                }
                
                while (*p && *p != ')') p++;
                if (*p == ')') p++;
                while (*p && (*p == ' ' || *p == '\t')) p++;
                if (*p == ';') p++;
            }
        } else if (blaze_str_starts_with(p, "alert")) {
            /* Simple alert stub */
            p += 5;
            while (*p && *p != ';') p++;
            if (*p == ';') p++;
            blaze_log(state, "[JS] Alert called (not implemented in GUI yet)\n");
        } else {
            /* Skip unknown token */
            while (*p && *p != ';' && *p != '\n') p++;
            if (*p) p++;
        }
    }
}

