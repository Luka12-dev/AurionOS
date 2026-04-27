# MicroPython port for AurionOS

# Path to MicroPython source
MICROPYTHON_SRC = src

# Include MicroPython build environment
include $(MICROPYTHON_SRC)/py/mkenv.mk

# Port-specific settings
QSTR_DEFS = qstrdefsport.h
MICROPY_ROM_TEXT_COMPRESSION = 0

# Include MicroPython core
include $(MICROPYTHON_SRC)/py/py.mk

# Compiler settings for AurionOS (freestanding i386)
CC = gcc
LD = ld
AR = ar
OBJCOPY = objcopy

# Compiler flags - match AurionOS build (32-bit)
CFLAGS = -m32 -ffreestanding -fno-pie -fno-stack-protector
CFLAGS += -Wall -Wextra -std=c99
CFLAGS += -Os -DNDEBUG
CFLAGS += -fdata-sections -ffunction-sections
CFLAGS += -D__i386__ -D__ILP32__
CFLAGS += -Wno-error
CFLAGS += -DMICROPY_FREESTANDING=1
CFLAGS += -include aurionos_compat.h

# Include paths - our libm.h comes first to override system math.h
INC += -I.
INC += -I$(MICROPYTHON_SRC)
INC += -I$(BUILD)
INC += -I../include

CFLAGS += $(INC)

# Source files
SRC_C = \
	main.c \
	mphalport.c \
	shared/libc/string0.c \
	shared/libc/printf.c \
	shared/readline/readline.c \
	shared/runtime/pyexec.c \
	shared/runtime/stdout_helpers.c \
	lib/libm/math.c \
	lib/libm/acoshf.c \
	lib/libm/asinfacosf.c \
	lib/libm/asinhf.c \
	lib/libm/atan2f.c \
	lib/libm/atanf.c \
	lib/libm/atanhf.c \
	lib/libm/ef_rem_pio2.c \
	lib/libm/ef_sqrt.c \
	lib/libm/erf_lgamma.c \
	lib/libm/fmodf.c \
	lib/libm/kf_cos.c \
	lib/libm/kf_rem_pio2.c \
	lib/libm/kf_sin.c \
	lib/libm/kf_tan.c \
	lib/libm/log1pf.c \
	lib/libm/nearbyintf.c \
	lib/libm/roundf.c \
	lib/libm/sf_cos.c \
	lib/libm/sf_erf.c \
	lib/libm/sf_frexp.c \
	lib/libm/sf_ldexp.c \
	lib/libm/sf_modf.c \
	lib/libm/sf_sin.c \
	lib/libm/sf_tan.c \
	lib/libm/wf_lgamma.c \
	lib/libm/wf_tgamma.c \

SRC_QSTR += shared/readline/readline.c shared/runtime/pyexec.c

# Object files
OBJ = $(PY_CORE_O)
OBJ += $(addprefix $(BUILD)/, $(SRC_C:.c=.o))

# Build directory
BUILD = build

# Default target
all: $(BUILD)/libmicropython.a

# Create build directory
$(BUILD):
	mkdir -p $(BUILD)
	mkdir -p $(BUILD)/shared/libc
	mkdir -p $(BUILD)/shared/readline
	mkdir -p $(BUILD)/shared/runtime

# Build static library
$(BUILD)/libmicropython.a: $(OBJ) | $(BUILD)
	$(ECHO) "AR $@"
	$(Q)$(AR) rcs $@ $^

# Clean
clean:
	rm -rf $(BUILD)

.PHONY: all clean

# Include MicroPython build rules
include $(MICROPYTHON_SRC)/py/mkrules.mk
