/* Custom libm.h for AurionOS freestanding MicroPython build */
/* Replaces system math.h to avoid glibc dependencies */

#ifndef AURIONOS_LIBM_H
#define AURIONOS_LIBM_H

#include <stdint.h>

/* Float type definitions */
typedef float float_t;
typedef double double_t;

/* Math constants */
#define INFINITY (__builtin_inff())
#define NAN      (__builtin_nanf(""))
#define HUGE_VALF (__builtin_inff())

#define FP_NAN       0
#define FP_INFINITE  1
#define FP_ZERO      2
#define FP_SUBNORMAL 3
#define FP_NORMAL    4

/* Force evaluation macro (from musl) */
#define FORCE_EVAL(x) do {                        \
	if (sizeof(x) == sizeof(float)) {         \
		volatile float __x;               \
		__x = (x);                        \
                (void)__x;                        \
	} else if (sizeof(x) == sizeof(double)) { \
		volatile double __x;              \
		__x = (x);                        \
                (void)__x;                        \
	} else {                                  \
		volatile long double __x;         \
		__x = (x);                        \
                (void)__x;                        \
	}                                         \
} while(0)

/* Get a 32 bit int from a float */
#define GET_FLOAT_WORD(w,d)                       \
do {                                              \
  union {float f; uint32_t i;} __u;               \
  __u.f = (d);                                    \
  (w) = __u.i;                                    \
} while (0)

/* Set a float from a 32 bit int */
#define SET_FLOAT_WORD(d,w)                       \
do {                                              \
      union {float f; uint32_t i;} __u;           \
      __u.i = (w);                                \
      (d) = __u.f;                                \
} while (0)

/* Math function declarations */
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

/* Additional classification functions */
int isfinite(double x);

/* Macros */
#define isnan(x) isnanf(x)
#define isinf(x) (!finitef(x))
#define signbit(x) __signbitf(x)
#define fpclassify(x) __fpclassifyf(x)

#endif /* AURIONOS_LIBM_H */
