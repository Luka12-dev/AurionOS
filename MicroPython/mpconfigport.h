#ifndef MICROPY_INCLUDED_AURIONOS_MPCONFIGPORT_H
#define MICROPY_INCLUDED_AURIONOS_MPCONFIGPORT_H

#include <stdint.h>
#include <stddef.h>
#include <limits.h>

/* Freestanding environment - define missing macros */
#ifndef offsetof
#define offsetof(type, member) __builtin_offsetof(type, member)
#endif

#ifndef UINT8_MAX
#define UINT8_MAX 255
#endif
#ifndef UINT16_MAX
#define UINT16_MAX 65535
#endif
#ifndef UINT32_MAX
#define UINT32_MAX 4294967295U
#endif
#ifndef SSIZE_MAX
#define SSIZE_MAX 2147483647
#endif

/* i386 platform: int and long are both 32-bit, which confuses MicroPython's auto-detection.
   We use MP_INT_TYPE_OTHER to provide custom definitions. */

/* Tell MicroPython we're providing custom integer types */
#define MP_INT_TYPE (2)  /* MP_INT_TYPE_OTHER */

/* Integer types */
typedef int32_t mp_int_t;
typedef uint32_t mp_uint_t;
typedef long mp_off_t;

/* Define limits */
#define MP_INT_MAX INT32_MAX
#define MP_INT_MIN INT32_MIN
#define MP_UINT_MAX UINT32_MAX

/* Format strings for mp_printf (bypass validation in mpconfig.h) */
#define INT_FMT "%d"
#define UINT_FMT "%u"
#define HEX_FMT "%x"
#define SIZE_FMT "%u"

/* Object representation */
#define MICROPY_OBJ_REPR (MICROPY_OBJ_REPR_A)

/* AurionOS is a freestanding 32-bit i386 environment */
#define MICROPY_CONFIG_ROM_LEVEL (MICROPY_CONFIG_ROM_LEVEL_MINIMUM)

/* Compiler and REPL */
#define MICROPY_ENABLE_COMPILER     (1)
#define MICROPY_HELPER_REPL         (1)
#define MICROPY_REPL_AUTO_INDENT    (0)

/* Memory management */
#define MICROPY_ENABLE_GC           (1)
#define MICROPY_HEAP_SIZE           (32 * 1024)
#define MICROPY_STACK_CHECK         (0)

/* Python features */
#define MICROPY_PY_BUILTINS_HELP    (0)
#define MICROPY_PY_BUILTINS_INPUT   (1)
#define MICROPY_PY_BUILTINS_STR_UNICODE (0)
#define MICROPY_PY_BUILTINS_BYTEARRAY (0)
#define MICROPY_PY_BUILTINS_MEMORYVIEW (0)
#define MICROPY_PY_BUILTINS_SET     (0)
#define MICROPY_PY_BUILTINS_FROZENSET (0)
#define MICROPY_PY_BUILTINS_SLICE   (1)
#define MICROPY_PY_BUILTINS_PROPERTY (0)
#define MICROPY_PY_BUILTINS_MIN_MAX (1)
#define MICROPY_PY___FILE__         (0)
#define MICROPY_PY_MICROPYTHON_MEM_INFO (0)
#define MICROPY_PY_BUILTINS_OPEN    (0)

/* Built-in modules */
#define MICROPY_PY_ARRAY            (1)
#define MICROPY_PY_COLLECTIONS      (0)
#define MICROPY_PY_MATH             (1)
#define MICROPY_PY_CMATH            (0)
#define MICROPY_PY_IO               (0)
#define MICROPY_PY_STRUCT           (0)
#define MICROPY_PY_SYS              (1)
#define MICROPY_PY_SYS_MAXSIZE      (1)
#define MICROPY_PY_SYS_EXIT         (1)
#define MICROPY_PY_SYS_PLATFORM     "aurionos"

/* Optimizations */
#define MICROPY_OPT_COMPUTED_GOTO   (0)
#define MICROPY_ALLOC_PATH_MAX      (128)
#define MICROPY_ALLOC_PARSE_CHUNK_INIT (16)

/* Error handling */
#define MICROPY_ERROR_REPORTING     (MICROPY_ERROR_REPORTING_TERSE)
#define MICROPY_WARNINGS            (0)

/* Float support */
#define MICROPY_FLOAT_IMPL          (MICROPY_FLOAT_IMPL_FLOAT)

/* Use MicroPython's built-in libm for freestanding environment */
#define MICROPY_PY_MATH_SPECIAL_FUNCTIONS (0)
#define MICROPY_PY_BUILTINS_FLOAT   (1)
#define MICROPY_PY_BUILTINS_COMPLEX (0)

/* Long int implementation */
#define MICROPY_LONGINT_IMPL        (MICROPY_LONGINT_IMPL_MPZ)

/* We need to provide a declaration/definition of alloca() */
#define alloca __builtin_alloca

#define MICROPY_HW_BOARD_NAME "AurionOS"
#define MICROPY_HW_MCU_NAME "i386"

#define MP_STATE_PORT MP_STATE_VM

/* No extra built-in names since we disabled file I/O */
#define MICROPY_PORT_BUILTINS

extern const struct _mp_print_t mp_plat_print;

#endif
