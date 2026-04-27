/* AurionOS compatibility header for freestanding MicroPython build */
#ifndef AURIONOS_COMPAT_H
#define AURIONOS_COMPAT_H

#ifdef MICROPY_FREESTANDING

/* Provide minimal standard library declarations for freestanding build */
#include <stddef.h>
#include <stdint.h>

typedef uint8_t  __uint8_t;
typedef int8_t   __int8_t;
typedef uint16_t __uint16_t;
typedef int16_t  __int16_t;
typedef uint32_t __uint32_t;
typedef int32_t  __int32_t;

/* stdlib.h functions */
void *malloc(size_t size);
void free(void *ptr);
void *realloc(void *ptr, size_t size);
void *calloc(size_t nmemb, size_t size);
void abort(void);
int atoi(const char *nptr);
long atol(const char *nptr);
double atof(const char *nptr);

/* stdio.h functions */
int printf(const char *format, ...);
int sprintf(char *str, const char *format, ...);
int snprintf(char *str, size_t size, const char *format, ...);
int vsnprintf(char *str, size_t size, const char *format, __builtin_va_list ap);
int puts(const char *s);
int putchar(int c);

/* stdio.h constants */
#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2
/* string.h functions */
void *memset(void *s, int c, size_t n);
void *memcpy(void *dest, const void *src, size_t n);
void *memmove(void *dest, const void *src, size_t n);
int memcmp(const void *s1, const void *s2, size_t n);
size_t strlen(const char *s);
char *strcpy(char *dest, const char *src);
char *strncpy(char *dest, const char *src, size_t n);
int strcmp(const char *s1, const char *s2);
int strncmp(const char *s1, const char *s2, size_t n);
char *strcat(char *dest, const char *src);
char *strchr(const char *s, int c);
char *strrchr(const char *s, int c);
char *strstr(const char *haystack, const char *needle);

/* math.h functions and constants */
#define INFINITY (__builtin_inff())
#define NAN      (__builtin_nanf(""))
#define HUGE_VALF (__builtin_inff())

/* Math function declarations - provided by MicroPython's libm */
float logf(float x);
float expm1f(float x);
float log10f(float x);
float sinf(float x);
float cosf(float x);
float tanf(float x);
float asinf(float x);
float acosf(float x);
float atanf(float x);
float atan2f(float y, float x);
float ceilf(float x);
float floorf(float x);
float fabsf(float x);
float sqrtf(float x);
float powf(float x, float y);
float expf(float x);
float log1pf(float x);
float asinhf(float x);
float acoshf(float x);
float atanhf(float x);
float copysignf(float x, float y);
float fmodf(float x, float y);
float finitef(float x);
float isnanf(float x);
float roundf(float x);
float nearbyintf(float x);
float nanf(const char *tagp);
float truncf(float x);
float scalbnf(float x, int n);
float coshf(float x);
float sinhf(float x);
float tanhf(float x);
int __fpclassifyf(float x);
int __signbitf(float x);
float __expo2f(float x);

#define isnan(x) isnanf(x)
#define isinf(x) (!finitef(x))
#define signbit(x) __signbitf(x)
#define fpclassify(x) __fpclassifyf(x)

/* assert.h */
#ifdef NDEBUG
#define assert(x) ((void)0)
#else
#define assert(x) ((x) ? (void)0 : __assert_fail(#x, __FILE__, __LINE__, __func__))
void __assert_fail(const char *expr, const char *file, int line, const char *func);
#endif

/* Redefine standard includes to use our declarations */
#define _STDLIB_H 1
#define _STDIO_H 1
#define _STRING_H 1
#define _ASSERT_H 1

#endif /* MICROPY_FREESTANDING */

#endif /* AURIONOS_COMPAT_H */
