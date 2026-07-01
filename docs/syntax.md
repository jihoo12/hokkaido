# Hokkaido Language Syntax

## Contents

- [Comments](#comments)
- [Types](#types)
- [Variables](#variables)
- [Functions](#functions)
- [Return](#return)
- [If / Else](#if--else)
- [For loop](#for-loop)
- [Assignment](#assignment)
- [Pointers](#pointers)
- [Comparison operators](#comparison-operators)
- [Logical operators](#logical-operators)
- [Arithmetic operators](#arithmetic-operators)
- [Shift operators](#shift-operators)
- [Function calls](#function-calls)
- [Match](#match)
- [Arrays](#arrays)
- [Structs](#structs)
- [Enums](#enums)
- [Namespaces](#namespaces)
- [Include](#include)
- [C FFI](#c-ffi)
- [Freestanding mode](#freestanding-mode)
- [Inline assembly](#inline-assembly)
- [Cubical](#cubical)

## Comments

```
// Line comment (everything after // is ignored)

/* Block comment — can span multiple lines,
   does not nest (the first */ ends the comment) */
```

## Types

```
int8        8-bit signed integer
int32       32-bit signed integer
int64       64-bit signed integer
int         Shorthand for int64
float16     16-bit floating point (half)
float32     32-bit floating point
float64     64-bit floating point
float       Shorthand for float64
bool        true or false
string      String (internal)
void        No return value (functions only)
cubical     Compile-time evaluated cubical expression (see Cubical)
```

A struct name is also a valid type (see [Structs](#structs)), and an enum name is a valid type (see [Enums](#enums)).

Pointer types are written with one `*` per level of indirection:

```
int8*       Pointer to int8
int32*      Pointer to int32
int64*      Pointer to int64
int**       Pointer to pointer to int64
```

Array types are written with a fixed size in brackets:

```
int[5]      Array of 5 int64 values
```

## Variables

```
let a: int8 = 1             8-bit integer variable
let b: int32 = 2            32-bit integer variable
let c: int64 = 42           64-bit integer variable
let d: int = 42             Shorthand for int64
let e: float16 = 1.0        16-bit float variable
let f: float32 = 2.0        32-bit float variable
let g: float64 = 3.14       64-bit float variable
let h: float = 3.14         Shorthand for float64
let s: string = "hello"     String variable
let p: int64* = &c          Pointer variable (address of c)
let b: bool = true          true
```

Variables are mutable; assignment uses `=`. A variable cannot have type `void`.

`let` can appear at the top level of a file (outside any function) or inside a function body or
block. Top-level `let`s are initialized once, before `main` runs.

## Functions

```
fn add(a: int, b: int) -> int {
  return a + b
}

fn main() -> int {
  return add(1, 2)
}

fn log(msg: string) -> void {
  asm("nop")
}

fn main(argc: int, argv: int8**) -> int {
  return argc
}
```

A `fn main() -> int` entry point is required at the top level of the program (i.e. not inside a
`namespace`). `main` must return `int`. `main` may optionally take two parameters:

- `argc: int` — argument count (automatically sign-extended from `i32` to `int64`)
- `argv: int8**` — argument vector (`char **` from the C runtime)

```
fn main() -> int { ... }                       // no arguments
fn main(argc: int, argv: int8**) -> int { ... } // argc / argv
```

If a function falls off the end of its body without an explicit `return`, it returns a zero value
of its declared type.

## Return

```
return expr         Return a value from a function
return              Return from a void function
```

## If / Else

```
if condition {
  // then branch
}

if condition {
  // then branch
} else {
  // else branch
}

if condition {
  // then branch
} else if other_condition {
  // else-if branch
} else {
  // else branch
}
```

`condition` is any expression; nonzero is treated as true. `else if` chains as far as you
like, ending in an optional final `else`.

```
if a {
  // ...
} else {
  if b {
    // ...
  }
}
```

## For loop

```
for let i: int = 0; i < 10; i = i + 1 {
  // body
}
```

Three semicolon-separated parts: init, condition, update. Any part can be omitted:

```
for ; condition; { }       // while-like
for ;; { }                 // infinite loop
```

The init clause can be a `let` (scoped to the loop) or any expression statement.

## Assignment

```
x = expr        Assign to an existing variable
```

Assignment is an expression (it evaluates to the assigned value), so it can be used inline, e.g.
as the update clause of a `for` loop. Valid assignment targets are: a plain variable, a pointer
dereference, an array subscript, or a struct field access.

```
*p = 42          Assign 42 through pointer p
arr[0] = 1       Assign to an array element
point.x = 10     Assign to a struct field
```

## Pointers

```
&x              Address of variable x
*p              Dereference pointer p
null            Null pointer literal
```

Pointers are typed: `int64*` is a pointer to `int64`, `float**` is a pointer to pointer to
`float`. Pointer depth is part of the type and is checked at compile time for assignment targets.

Pointer arithmetic is supported through standard `+` and `-` operators. Adding an integer to
a pointer (or a pointer to an integer) produces a pointer offset by that many elements; the
stride is determined by the pointee type's size:

```
let p: int* = arr
let q: int* = p + 5      // advance 5 * sizeof(int) bytes
let r: int* = 3 + q      // pointer + integer works both ways
p = p - 1                // subtract from pointer
```

## Comparison operators

```
==      Equal
!=      Not equal
<       Less than
>       Greater than
<=      Less than or equal
>=      Greater than or equal
```

Comparison operators have lower precedence than `<<` and `>>`, which in turn bind lower
than `+` and `-` — the same precedence hierarchy as C. They return `bool` (`true` / `false`).

## Logical operators

```
&&      Logical AND
||      Logical OR
```

`&&` and `||` short-circuit and return `bool` (`true` / `false`). Each operand is
treated as truthy when non-zero (the same "nonzero is true" rule as `if` conditions).

## Arithmetic operators

```
+       Addition
-       Subtraction
*       Multiplication (binary) / Dereference (unary)
/       Division
```

Standard precedence: `*` `/` bind tighter than `+` `-`. Unary `-` and `*` bind tighter than all
binary operators.

## Shift operators

```
>>      Right shift (arithmetic — sign-extending)
<<      Left shift (zero-filling)
```

`a >> b` shifts `a` right by `b` bits, sign-extending (the sign bit is preserved for
negative values), matching C's implementation-defined signed right shift on GCC/Clang.
`a << b` shifts `a` left by `b` bits, zero-filling — the same as C.

Precedence matches C: `<<` and `>>` bind tighter than comparison operators but looser
than `+` and `-`, so `a + 1 >> b - 1` parses as `(a + 1) >> (b - 1)`.

## Function calls

```
let z: int = add(1, 2)          Function call
```

Arguments are evaluated left to right and coerced to each parameter's declared type.

## Match

```
match expr {
    pattern => expr,
    pattern => expr,
    ...
}
```

`match` evaluates the subject expression and tries each arm's pattern in order. The first
matching arm's expression is evaluated and becomes the value of the whole match. If no arm
matches, the result is zero.

### Patterns

```
_                           Wildcard — matches anything, binds nothing
123                         Literal — matches an exact integer/float/string/null
name                        Variable — matches anything, binds the value to name
Point { x: 0, y: _ }        Struct — matches fields against sub-patterns
Point { x, y }              Struct — shorthand for `Point { x: x, y: y }`
Some { value }              Enum variant — matches a variant with fields (shorthand)
None                        Enum unit variant — matches a unit variant by name
```

`match` works on any type, including integers, structs, and enums. When matching an enum,
the compiler checks the variant tag (discriminant) at runtime and branches to the matching
arm. Enum variant patterns use the same syntax as struct patterns.

### Examples

```
fn classify(p: Point) -> int {
  return match p {
    Point { x: 0, y: 0 } => 1,     // origin
    Point { x: 0, y } => 2,        // on y-axis, bind y
    Point { x, y: 0 } => 3,        // on x-axis, bind x
    Point { x, y } => x + y,       // somewhere else
  }
}
```

`match` is an expression and can appear wherever any other expression is valid — in a return,
an assignment, a let initializer, or nested inside another match.

## Arrays

```
let arr: int[5] = [10, 20, 30, 40, 50]      array literal
arr[0] = 0                                  assign an element
let first: int = arr[0]                     read an element
```

Array literal elements can be any expression, not just compile-time constants:

```
let a: int[3] = [x, x + 1, x + 2]
```

Arrays decay to a pointer to their first element when assigned to a pointer-typed variable:

```
let ptr: int* = arr                              array-to-pointer decay
ptr[0] = 100                                     pointer-based subscript (read/write)
let v: int = *(ptr + 2)                          pointer arithmetic
```

Once decayed, the pointer supports subscript (`ptr[i]`) and pointer arithmetic (`ptr + n`,
`n + ptr`, `ptr - n`) with stride based on the element size.

## Structs

```
struct Point {
  x: int
  y: int
}

let p: Point = make_point(5)     evaluated initializer
p.x = 10
p.y = 20
```

Struct fields are declared one per line (separated by commas or newlines). A struct literal is
written with the struct name and a brace-enclosed list of `field: value` pairs, separated by commas:

```
let p: Point = Point { x: 10, y: 20 }
let q: Point = q                              // placeholder: zero-initialized
```

Struct literals can also be created by any expression returning the struct type.

Field access works on any expression that evaluates to a struct, including chained access,
dereference, and array subscript:

```
a.b.c                         chained field access
(*ptr).field                  dereference then field access
arr[i].field                  array element then field access
```

Structs are passed and returned by value (like C). Passing a struct to a function copies the
entire struct; returning a struct returns a copy.

## Enums

```
enum Option {
  Some { value: int },
  None,
}

let x: Option = Some { value: 42 }
```

Enums are algebraic data types with one or more named variants. Each variant can have named
fields (struct-like) or be a unit variant (no fields). The enum name becomes a type; variant
names serve as constructors.

### Enum constructors

```
let x: Option = Some { value: 42 }
let y: Option = None
```

Constructor expressions use the variant name followed by `{ field: expr, ... }` for variants
with fields, or just the variant name for unit variants. Field order is not significant at
the value level (the compiler stores them in declaration order).

### Matching on enums

```
return match x {
  Some { value } => value,
  None => 0,
}
```

Match arms can destructure enum variants using the same syntax as struct patterns. The
compiler emits a tag check (discriminant) at runtime, branching to the appropriate arm.
Shorthand field patterns (`Some { value }` binds the field named `value`) work the same way
as in struct patterns.

### Nested types

Enums can contain structs, other enums, or any other type as field types. Structs, arrays,
and pointers can all appear as enum variant fields:

```
enum Container {
  HasPoint { pt: Pair, label: int },
  Empty,
}
```

## Namespaces

```
namespace math {
  fn square(x: int) -> int {
    return x * x
  }

  struct Point {
    x: int
    y: int
  }

  namespace trig {
    fn double_it(x: int) -> int {
      return x + x
    }
  }
}

let a: int = math::square(5)
let b: int = math::trig::double_it(3)
let p: math::Point = p
```

`namespace name { ... }` groups `let`, `fn`, `struct`, `include`, and nested `namespace`
declarations under a qualified name. Inside, declare things exactly as you would at the top
level; from outside, refer to them with `::`, chaining one `::segment` per level of nesting
(`a::b::name`). There is no `using namespace` — names must always be fully qualified at the use
site.

Namespaces are resolved at compile time by qualifying each declaration's name, so a namespaced
`fn main()` (e.g. `app::main`) is **not** picked up as the program's entry point; the entry point
must be an unqualified top-level `main`. Top-level `let`s declared inside a namespace are parsed
and accepted, but are not currently wired into program startup the way unqualified top-level
`let`s are.

## Include

```
include "shapes.hk"
```

`include "path"` pulls in another `.hk` file's top-level declarations (`let`, `fn`, `struct`,
nested `include`, and `namespace`) as if they were written at the `include` site. Relative paths
are resolved relative to the directory of the file containing the `include`. A file is only ever
included once, even if reached by multiple paths (diamond includes) or in a cycle — repeated or
circular `include`s are silently skipped rather than causing an error or infinite loop.

`include` can appear at the top level or inside a `namespace` block, in which case the included
file's declarations are namespaced along with everything else in that block:

```
namespace geo {
  include "vec2.hk"     // declarations from vec2.hk become geo::Vec2, geo::add, etc.
}
```

## C FFI

```
extern fn puts(s: string) -> int
extern fn printf(fmt: string, ...) -> int
extern fn malloc(size: int) -> int8*
extern fn free(ptr: int8*) -> void

fn main() -> int {
  puts("hello from ffi")
  printf("x = %d, y = %d\n", 1, 2)
  return 0
}
```

`extern fn name(params...) -> T` declares a foreign function — typically from a C library — to
link against. It has no body: a `{ }` block is not written, and the declaration ends after the
return type. Codegen emits this as a function *declaration* only; the actual definition is
resolved by whatever linker you run afterwards against the named symbol.

The parameter list may end in a bare `...` to mark the function variadic (C-style varargs, as
used by `printf`-family functions):

```
extern fn printf(fmt: string, ...) -> int
```

`...` must be the last thing in the parameter list. At a call site, arguments matching declared
parameters are coerced to those parameters' types as usual; any extra arguments past that (the
`...` portion) have no declared type to coerce to, so a type is inferred from the argument itself
— string literals become a pointer, anything else becomes `int64`. This does **not** replicate C's
variadic argument promotion rules (`float` → `double`, small integers → `int`); pass `float`/
`float64` values explicitly rather than relying on automatic promotion.

`string` parameters and returns map to a raw `i8*`-equivalent pointer, matching C's `char*`/
`const char*`, so functions like `puts(const char *s)` line up directly with `puts(s: string)`.

`extern fn` is currently only supported at the top level — it cannot appear inside a `namespace`
block, since namespacing would qualify (mangle) the declared name, which would no longer match the
real C symbol being linked against. It is also rejected entirely in `--freestanding` mode (see
[Freestanding mode](#freestanding-mode)), since there's no libc linked for it to resolve against.

`hokkaido prog.hk -o prog` only emits `prog.o` — it does not link, so any `extern fn` symbols
(including ordinary libc ones like `puts` or `malloc`) stay unresolved until you link the object
file yourself. The simplest way is to hand it to `clang`, which already knows how to find libc and
the C runtime startup objects:

```
clang prog.o -o prog
```

To link against an additional C library beyond libc, add the usual linker flags:

```
clang prog.o -o prog -lm -lcurl -L/usr/local/lib
```

Any extra arguments you pass to `hokkaido` after `-o prog` aren't used for linking — `hokkaido`
doesn't invoke a linker at all. They're only echoed back as part of a suggested link command it
prints once `prog.o` is written (using `ld.lld` directly if it can locate the CRT startup objects
and dynamic linker, or `clang` as a simpler fallback otherwise), so you can copy that line or
write your own.

## Freestanding mode

```
hokkaido prog.hk -o prog --freestanding
```

`--freestanding` compiles without any dependency on the C runtime or libc. Two things change:

- `main` is generated as the program's raw ELF entry point rather than a normal C-ABI `main` that
  a CRT calls into. Since nothing will be there to receive its return value, falling off the end
  (or an explicit `return`) is compiled to a direct `exit` syscall instead of a `ret` instruction.
  `return n` in `main` still does what you'd expect — it's just implemented without libc's
  `exit()`.
- `extern fn` declarations are rejected at compile time, since there's no libc linked for them to
  resolve against. Anything you need at the syscall level has to go through `asm(...)` instead.

Because there's no CRT to provide `_start`, the suggested link command points the linker at
`main` directly as the entry point and skips `-lc`/CRT objects entirely:

```
ld.lld --entry=main -o prog prog.o
```

(or, if `ld.lld` isn't on `PATH`, a `clang -nostdlib -static -Wl,--entry=main` invocation that
asks clang to skip CRT/libc the same way.)

This trades away everything libc normally provides for free — buffered I/O, `malloc`, `printf`,
etc. — in exchange for a binary with no runtime dependency on libc being present on the system
that runs it. It's intended for small, syscall-only programs (or as a base to build a custom
runtime on top of), not as a drop-in replacement for normal compilation.

## Inline assembly

```
asm("nop")                      Inline assembly (void expression)
```

`asm(...)` takes a single string literal of raw assembly and emits it inline. It evaluates as an
expression but produces no usable value.

## Cubical

```
let n: cubical = "cubicalsource.cub"
```

A `cubical` variable triggers compile-time type-checking and evaluation of a cubical
expression — either inline cubical source, or, if the string ends in `.cub`, a path to a `.cub`
file read at compile time. If the result is a natural number it is embedded as an integer
constant; otherwise the textual result is embedded as a string constant.

See the [cubical surface language](cubical_surface_language.md) syntax document for the cubical
language itself.