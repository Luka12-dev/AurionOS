/* Minimal libm stubs for freestanding MicroPython */

#include <stdint.h>

/* errno stub */
int errno = 0;

/* Stack protector stubs */
void __stack_chk_fail_local(void) {
    while(1);
}

void __stack_chk_fail(void) {
    while(1);
}

/* CPU features stub */
struct {
    unsigned int cpuid[4];
} _dl_x86_cpu_features;

/* NaN parsing stub */
float __strtof_nan(const char *s) {
    (void)s;
    return 0.0f / 0.0f;
}

/* Missing math functions for MicroPython libm */
float fabsf(float x) {
    union { float f; uint32_t i; } u;
    u.f = x;
    u.i &= 0x7fffffff;
    return u.f;
}

int isnanf(float x) {
    union { float f; uint32_t i; } u;
    u.f = x;
    return (u.i & 0x7fffffff) > 0x7f800000;
}

int isfinite(double x) {
    union { double f; uint64_t i; } u;
    u.f = x;
    return (u.i & 0x7fffffffffffffffULL) < 0x7ff0000000000000ULL;
}

int finitef(float x) {
    union { float f; uint32_t i; } u;
    u.f = x;
    return (u.i & 0x7fffffff) < 0x7f800000;
}

int isinff(float x) {
    union { float f; uint32_t i; } u;
    u.f = x;
    return (u.i & 0x7fffffff) == 0x7f800000;
}

float nanf(const char *tagp) {
    (void)tagp;
    union { float f; uint32_t i; } u;
    u.i = 0x7fc00000;
    return u.f;
}

float copysignf(float x, float y) {
    union { float f; uint32_t i; } ux = {x}, uy = {y};
    ux.i = (ux.i & 0x7fffffff) | (uy.i & 0x80000000);
    return ux.f;
}

int signbit(double x) {
    union { double f; uint64_t i; } u;
    u.f = x;
    return u.i >> 63;
}

