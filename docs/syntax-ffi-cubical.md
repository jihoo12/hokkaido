# C FFI, Freestanding, Inline Assembly, and Cubical

## C FFI

Hokkaido can call C functions and be called from C code. FFI declarations use the
`extern` keyword to indicate that the function is defined externally (linked from a
C library or object file).

### Extern function declarations

```
extern fn name(param1: type1, param2: type2, ...) -> return_type
```

The function body is omitted — the implementation comes from the linker.

```
extern fn putchar(c: int8) -> int32
extern fn printf(fmt: string, ...) -> int32
extern fn sqrt(x: float64) -> float64
```

### Variadic parameters

Extern functions can be declared variadic with `...` after the required parameters.
There is no type checking on variadic arguments — the compiler passes them through
according to the platform calling convention.

```
extern fn printf(fmt: string, ...) -> int32
```

### Calling extern functions

Extern functions are called like regular functions:

```
fn main() -> int {
    printf::<string>("hello\n")    // prints "hello"
    return 0
}
```

(Note: generic C functions are not supported — the turbofish `::<string>` here is
for example only; actual `printf` usage requires matching the format string to
argument types manually.)

### String / char* handling

Hokkaido's `string` type maps to a C `char*` when passed to an extern function.
When a C function returns `char*`, Hokkaido treats it as a raw pointer (`int8*`).

### Linking

All object files are linked with `cc` (the system C compiler) by default. To link
against a specific library, add `-l<libname>` to the linker flags:

```
// Compile: hokkaido file.hk && cc file.o -lm -o file
```

### Calling convention

Extern functions use the C calling convention (`ccall`). Regular (non-extern)
Hokkaido functions do not use a stable ABI — they may use the C calling convention
internally, but this is not guaranteed.

## Freestanding mode

Freestanding mode produces code with no dependency on the C runtime. The program
must provide its own entry point and is linked directly with the system linker (or
with a custom linker script).

### Entry point

In freestanding mode, `main` is compiled to use the C calling convention and can
serve as the program entry point when linked appropriately. The return value of
`main` is passed to the exit system call via the LLVM-generated `_start` stub or
a user-provided entry point.

### Syscalls

System calls are made via inline assembly (see below) or by calling extern functions
from a static library that implements the raw syscall interface.

### No CRT dependency

Freestanding mode avoids linking against `crt0`, `libc`, and other C runtime objects.
The resulting binary is typically smaller and has no startup overhead.

## Inline assembly

Raw machine instructions can be embedded using assembly blocks. Inline assembly is
parsed as a string by the compiler and passed directly to the LLVM backend.

### Basic syntax

```
asm("instruction_template" : output_operands : input_operands : clobbers)
```

Each operand is written as `"constraint" (expression)`. The template uses `$0`, `$1`,
etc. to refer to operands by position.

```
// x86-64: issue a write syscall
let msg: string = "hello\n"
let len: int = 6
let ret: int = 0
asm(
    "syscall"
    : "={rax}" (ret)
    : "{rax}" (1), "{rdi}" (1), "{rsi}" (msg), "{rdx}" (len)
    : "rcx", "r11"
)
```

### Constraints

- `={reg}` — output operand, assigned from the named register after the asm executes.
- `{reg}` — input operand, placed into the named register before the asm executes.

Common registers: `{rax}`, `{rbx}`, `{rcx}`, `{rdx}`, `{rsi}`, `{rdi}`, `{r8}`...`{r15}`,
`{xmm0}`...`{xmm15}`.

### Clobbers

Registers that are modified by the inline assembly but not listed as outputs must be
listed in the clobber list. The compiler will save and restore them around the asm block.
Current clobber values are passed as a comma-separated list of register names in the
template string after `:`, e.g. `: "rcx", "r11"`.

### Volatile

Inline assembly is treated as volatile by default — the compiler will not remove,
move, or optimize the asm block.

## Cubical

Cubical is a compile-time evaluation subsystem for Hokkaido. It allows running code
during compilation to generate types, compute constants, or perform metaprogramming.

### Cubical type

The `cubical` type represents a compile-time cubical expression. Variables of type
`cubical` are evaluated during compilation and can be used where compile-time
constants are required.

```
let compile_time_value: cubical = 42
```

### Cubical functions

A function can accept `cubical` parameters to receive compile-time values:

```
fn compile_time_add(a: cubical, b: cubical) -> cubical {
    return a + b
}
```

### Usage

Cubical expressions are evaluated by the cubical library during compilation.
The exact feature set is determined by the cubical implementation (currently
provided as a Rust cubical library that Hokkaido embeds).

### Relation to generics

Cubical is separate from generics. While generics provide type-level polymorphism
at compile time with runtime code generation (monomorphization), cubical provides
arbitrary compile-time computation that can produce both types and values.
