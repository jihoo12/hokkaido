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
- [Arithmetic operators](#arithmetic-operators)
- [Function calls](#function-calls)
- [Arrays](#arrays)
- [Structs](#structs)
- [Namespaces](#namespaces)
- [Include](#include)
- [C FFI](#c-ffi)
- [Inline assembly](#inline-assembly)
- [Cubical](#cubical)

## Comments

```
// Line comment (everything after // is ignored)
```

There is no block comment syntax.

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

A struct name is also a valid type (see [Structs](#structs)).

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
```

A `fn main() -> int` entry point is required at the top level of the program (i.e. not inside a
`namespace`). `main` must return `int`. If a function falls off the end of its body without an
explicit `return`, it returns a zero value of its declared type.

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
```

`condition` is any expression; nonzero is treated as true. There is no `else if` keyword — chain
with a nested `if` inside the `else` block:

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

## Comparison operators

```
==      Equal
!=      Not equal
<       Less than
>       Greater than
<=      Less than or equal
>=      Greater than or equal
```

Comparison operators have lower precedence than `+` and `-`, and produce an `int` (`1` for true,
`0` for false) rather than a distinct boolean value.

## Arithmetic operators

```
+       Addition
-       Subtraction
*       Multiplication (binary) / Dereference (unary)
/       Division
```

Standard precedence: `*` `/` bind tighter than `+` `-`. Unary `-` and `*` bind tighter than all
binary operators.

## Function calls

```
let z: int = add(1, 2)          Function call
```

Arguments are evaluated left to right and coerced to each parameter's declared type.

## Arrays

```
let arr: int[5] = [10, 20, 30, 40, 50]      array literal
arr[0] = 0                                  assign an element
let first: int = arr[0]                     read an element
```

Array elements in a literal must currently be compile-time constants (e.g. number literals); a
non-constant element is filled with zero.

## Structs

```
struct Point {
  x: int
  y: int
}

let p: Point = Point
p.x = 10
p.y = 20
```

Struct fields are declared one per line (no commas, no trailing separator). A struct variable is
currently always zero-initialized at declaration — the expression on the right of `=` is parsed
but not evaluated, so any placeholder identifier (conventionally the struct's own name) is
accepted there. Field access and assignment (`p.x`, `p.x = 10`) work as normal once the variable
exists.

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
resolved by the linker against the named symbol (the C standard library is linked by default).

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
real C symbol being linked against.

To link against an additional C library beyond libc, pass linker flags after `-o` on the command
line; they're forwarded straight to the underlying `clang` link step:

```
hokkaido prog.hk -o prog -lm -lcurl -L/usr/local/lib
```

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