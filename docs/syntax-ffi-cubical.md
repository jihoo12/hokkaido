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

### String / char* handling

Hokkaido's `string` type maps to a C `char*` when passed to an extern function.
When a C function returns `char*`, Hokkaido treats it as a raw pointer (`int8*`).

### Linking

All object files are linked with `cc` (the system C compiler) by default. To link
against a specific library, add `-l<libname>` to the linker flags:

```
// Compile: hokkaido file.hk && clang file.o -lm -o file
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

Cubical is a compile-time evaluation subsystem for Hokkaido. It embeds a
Rust cubical language backend that type-checks
and evaluates cubical source files during compilation, embedding the result as a
constant in the generated code.  
for reference check [cubical_surface_language](cubical_surface_language.md)

### Cubical type

The `cubical` type is a compile-time placeholder. After evaluation it resolves to
either `int64` (when the cubical file evaluates to a natural number) or `string`
(when it evaluates to another type of value).

### Usage

Declare a cubical value by binding a string literal (a path to a `.cub` file) to the
`cubical` type:

```
let n: cubical = "path/to/example.cub"
```

At compile time, the compiler loads the file, sends it to the Rust cubical backend
for parsing, type-checking, and evaluation (via normalization-by-evaluation), then
embeds the resulting constant directly into the LLVM IR.

```
// --- test/example.cub ---
// (contains a cubical expression that evaluates to a Nat)

// --- main.hk ---
let n: cubical = "../test/example.cub"

fn main() -> int {
    return n       // n is already an int64 constant
}
```

### Result resolution

The cubical backend always returns a **natural number** (Nat) encoded as either a
decimal literal or a chain of `suc(suc(...zero))`. The compiler parses this result:

- If it parses as a decimal integer → the variable becomes an `int64` constant.
- If it cannot be parsed as a Nat → the variable becomes a `string` constant
  containing the cubical backend's text output.
  
There is no runtime cubical evaluation — everything happens during compilation.