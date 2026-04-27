# MicroPython for AurionOS

This directory contains the complete MicroPython implementation ported to AurionOS.

## What is MicroPython?

MicroPython is a lean and efficient implementation of Python 3, optimized to run on microcontrollers and in constrained environments. It implements the entire Python 3.4 syntax and includes select features from later versions.

**This is the MicroPython** - the same codebase that powers:
- ESP32 and ESP8266 boards
- Raspberry Pi Pico (RP2040/RP2350)
- PyBoard (official MicroPython hardware)
- STM32 microcontrollers
- Hundreds of other embedded systems

Source: https://github.com/micropython/micropython

## Directory Structure

```
MicroPython/
├── src/                    # Official MicroPython source code
│   ├── py/                 # Core Python VM and runtime
│   ├── extmod/             # Extension modules (C implementations)
│   ├── lib/                # Third-party libraries
│   ├── shared/             # Shared utilities
│   ├── ports/              # Platform-specific ports
│   ├── mpy-cross/          # Cross-compiler for .mpy files
│   └── tools/              # Build tools
│
├── mpconfigport.h          # AurionOS port configuration
├── mphalport.h             # Hardware abstraction layer (header)
├── mphalport.c             # HAL implementation
├── main.c                  # MicroPython initialization
├── qstrdefsport.h          # Port-specific strings
├── Makefile                # Build system
└── README.md               # This file
```

## Features

### Full Python 3 Language Support

- **Syntax**: Complete Python 3.4+ syntax including:
  - Classes and inheritance
  - Generators and `yield`
  - List/dict/set comprehensions
  - `async`/`await` keywords
  - Exception handling
  - Decorators
  - Context managers (`with` statement)

- **Data Types**:
  - `int` (arbitrary precision with MPZ)
  - `float` (32-bit IEEE 754)
  - `str` (with Unicode support)
  - `bytes`, `bytearray`
  - `list`, `tuple`, `dict`, `set`, `frozenset`
  - `array.array`
  - `collections.namedtuple`

### Built-in Modules

- `sys` - System-specific parameters and functions
- `math` - Mathematical functions (sin, cos, sqrt, etc.)
- `struct` - Binary data packing/unpacking
- `array` - Efficient numeric arrays
- `collections` - Container datatypes
- `gc` - Garbage collector interface
- `micropython` - MicroPython-specific functions

### Memory Management

- **Automatic Garbage Collection**: Mark-and-sweep GC
- **Heap Size**: 64KB (configurable)
- **Stack Checking**: Prevents stack overflow
- **Memory Info**: `micropython.mem_info()` for debugging

### Performance

- **Bytecode Compilation**: Python source → bytecode
- **Optimized VM**: Fast bytecode interpreter
- **Native Code**: Support for inline assembler (ARM/x86)
- **Frozen Modules**: Pre-compiled bytecode in flash

## Building

MicroPython is automatically built as part of AurionOS:

```bash
wsl make all
```

The build process:
1. Builds `mpy-cross` (cross-compiler)
2. Compiles MicroPython core to `libmicropython.a`
3. Links library into AurionOS kernel

## Usage

### Interactive REPL

Start the MicroPython REPL (DOS mode only):

```
PYTHON
```

Features:
- Multi-line editing
- Auto-indentation
- Tab completion
- Command history
- Help system (`help()`)

### Run Scripts

Execute a Python file:

```
PYTHON script.py
```

### Example Code

```python
# Variables and types
x = 42
name = "AurionOS"
pi = 3.14159

# Lists and iteration
numbers = [1, 2, 3, 4, 5]
for n in numbers:
    print(n * 2)

# List comprehensions
squares = [x**2 for x in range(10)]

# Functions
def fibonacci(n):
    a, b = 0, 1
    for _ in range(n):
        yield a
        a, b = b, a + b

# Generators
for fib in fibonacci(10):
    print(fib)

# Classes
class Vector:
    def __init__(self, x, y):
        self.x = x
        self.y = y
    
    def __add__(self, other):
        return Vector(self.x + other.x, self.y + other.y)
    
    def magnitude(self):
        return (self.x**2 + self.y**2)**0.5

v1 = Vector(3, 4)
v2 = Vector(1, 2)
v3 = v1 + v2
print(v3.magnitude())

# Exception handling
try:
    result = 10 / 0
except ZeroDivisionError as e:
    print("Error:", e)
finally:
    print("Cleanup")

# Context managers
class Resource:
    def __enter__(self):
        print("Acquiring resource")
        return self
    
    def __exit__(self, exc_type, exc_val, exc_tb):
        print("Releasing resource")

with Resource() as r:
    print("Using resource")

# Decorators
def timer(func):
    def wrapper(*args, **kwargs):
        print(f"Calling {func.__name__}")
        return func(*args, **kwargs)
    return wrapper

@timer
def greet(name):
    print(f"Hello, {name}!")

greet("World")
```

## Configuration

Edit `mpconfigport.h` to customize the port:

### Memory Settings

```c
#define MICROPY_HEAP_SIZE (64 * 1024)  // 64KB heap
#define MICROPY_STACK_CHECK (1)         // Enable stack checking
```

### Language Features

```c
#define MICROPY_PY_BUILTINS_SET (1)        // Enable set type
#define MICROPY_PY_BUILTINS_FROZENSET (1)  // Enable frozenset
#define MICROPY_PY_BUILTINS_SLICE (1)      // Enable slice objects
```

### Modules

```c
#define MICROPY_PY_MATH (1)         // Enable math module
#define MICROPY_PY_STRUCT (1)       // Enable struct module
#define MICROPY_PY_ARRAY (1)        // Enable array module
#define MICROPY_PY_COLLECTIONS (1)  // Enable collections module
```

### Float Support

```c
#define MICROPY_FLOAT_IMPL (MICROPY_FLOAT_IMPL_FLOAT)  // 32-bit float
// or
#define MICROPY_FLOAT_IMPL (MICROPY_FLOAT_IMPL_DOUBLE) // 64-bit double
// or
#define MICROPY_FLOAT_IMPL (MICROPY_FLOAT_IMPL_NONE)   // No float support
```

## Integration with AurionOS

### Console I/O

MicroPython uses AurionOS console functions:
- `c_putc(char)` - Output character
- `c_getkey()` - Input character

### Filesystem

Scripts are loaded using:
- `load_file_content(filename, buffer, size)` - Read file from AurionOS FS

### Boot Mode Detection

- GUI mode: REPL disabled (scripts only)
- DOS mode: Full REPL available

## Advanced Features

### Inline Assembler

```python
@micropython.asm_thumb
def add(r0, r1):
    add(r0, r0, r1)
```

### Native Code Emission

```python
@micropython.native
def fast_function(x):
    return x * x
```

### Viper Code Generator

```python
@micropython.viper
def very_fast(x: int) -> int:
    return x * x
```

### Memory Profiling

```python
import gc
import micropython

# Show memory info
micropython.mem_info()

# Detailed allocation info
micropython.mem_info(1)

# Force garbage collection
gc.collect()

# Get free memory
print("Free:", gc.mem_free())
print("Allocated:", gc.mem_alloc())
```

## Limitations

Current limitations of the AurionOS port:

1. **No File I/O Module** - Use AurionOS filesystem commands
2. **No Socket Module** - Use AurionOS network commands
3. **No Threading** - Single-threaded execution
4. **REPL in DOS Mode Only** - GUI mode supports scripts only
5. **Limited Standard Library** - Embedded-focused subset

## Future Enhancements

Planned additions:

### AurionOS-Specific Modules

```python
import aurion

# Graphics
aurion.graphics.pixel(x, y, color)
aurion.graphics.line(x1, y1, x2, y2, color)
aurion.graphics.rect(x, y, w, h, color)

# GUI
window = aurion.gui.Window("My App", 640, 480)
button = aurion.gui.Button(window, "Click Me", 10, 10)

# Filesystem
with aurion.fs.open("file.txt", "r") as f:
    data = f.read()

# Network
sock = aurion.net.socket()
sock.connect("example.com", 80)
sock.send(b"GET / HTTP/1.0\r\n\r\n")
```

### Standard Library Additions

- `json` - JSON encoding/decoding
- `re` - Regular expressions
- `hashlib` - Cryptographic hashes
- `binascii` - Binary/ASCII conversions
- `time` - Time functions

## Technical Details

### Compilation Process

1. **Source Code** → Lexer → Tokens
2. **Tokens** → Parser → Parse Tree
3. **Parse Tree** → Compiler → Bytecode
4. **Bytecode** → VM → Execution

### Bytecode Format

MicroPython uses a compact bytecode format:
- Variable-length instructions
- Inline caching for performance
- Optimized for small code size

### Garbage Collection

Mark-and-sweep algorithm:
1. Mark phase: Traverse all reachable objects
2. Sweep phase: Free unmarked objects
3. Compact phase: Optional memory defragmentation

### Object Model

All Python objects are represented as `mp_obj_t`:
- Small integers: Immediate values (no allocation)
- Other types: Pointer to heap-allocated structure

## Debugging

### Enable Debug Output

```c
#define MICROPY_DEBUG_PRINTERS (1)
```

### Stack Trace

```python
import sys

try:
    # Code that might fail
    pass
except Exception as e:
    sys.print_exception(e)
```

### Memory Debugging

```python
import gc
import micropython

# Allocation tracking
micropython.mem_info(1)

# Show all objects
gc.collect()
micropython.mem_info()
```

## Performance Tips

1. **Use Local Variables** - Faster than globals
2. **Avoid String Concatenation** - Use `''.join()` instead
3. **Use Generators** - Memory efficient for large sequences
4. **Pre-allocate Lists** - `[None] * n` is faster than appending
5. **Use `@micropython.native`** - 2-5x speedup for numeric code
6. **Use `@micropython.viper`** - 10-20x speedup with type hints

## Resources

- Official Documentation: https://docs.micropython.org/
- GitHub Repository: https://github.com/micropython/micropython
- Forum: https://forum.micropython.org/
- Discord: https://discord.gg/RB8HZSAExQ

## Credits

MicroPython is developed by Damien George and the MicroPython community.

AurionOS port by the AurionOS development team.

## License

MicroPython is licensed under the MIT License.
See `src/LICENSE` for details.
