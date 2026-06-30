# Hokkaido Language Syntax

## Comments

```
// Line comment (everything after // is ignored)
```

## Types

```
int8        8-bit signed integer
int32       32-bit signed integer
int64       64-bit signed integer
int         Shorthand for int64
float       64-bit floating point
string      String (internal)
void        No return value (functions only)
bool        true or false
```

Pointer types are written with `*`:

```
int8*       Pointer to int8
int32*      Pointer to int32
int64*      Pointer to int64
int**       Pointer to pointer to int64
```

## Variables

```
let a: int8 = 1             8-bit integer variable
let b: int32 = 2            32-bit integer variable
let c: int64 = 42           64-bit integer variable
let d: int = 42             Shorthand for int64
let y: float = 3.14         Float variable
let s: string = "hello"     String variable
let p: int64* = &c          Pointer variable (address of c)
let b: bool = true          true
```

Variables are mutable; assignment uses `=`.

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

A `fn main() -> int` entry point is required.

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

## For loop

```
for let i: int = 0, i < 10, i = i + 1 {
  // body
}
```

Three comma-separated parts: init, condition, update. Any part can be omitted:

```
for , condition, { }       // while-like
for , , { }                // infinite loop
```

## Assignment

```
x = expr        Assign to an existing variable
```

Assignment is an expression (returns the assigned value). Write through a pointer:

```
*p = 42      Assign 42 through pointer p
```

## Pointers

```
&x              Address of variable x
*p              Dereference pointer p
null            Null pointer literal
```

Pointers are typed: `int64*` is a pointer to `int64`, `float**` is a pointer to pointer to `float`.
Pointer depth is part of the type and checked at compile time for assignment targets.

## Comparison operators

```
==      Equal
!=      Not equal
<       Less than
>       Greater than
<=      Less than or equal
>=      Greater than or equal
```

Comparison operators have lower precedence than `+` and `-`.

## Arithmetic operators

```
+       Addition
-       Subtraction
*       Multiplication (binary) / Dereference (unary)
/       Division
```

Standard precedence: `*` `/` bind tighter than `+` `-`.
Unary `-`, `!`, and `*` bind tighter than all binary operators.

## Function calls

```
let z: int = add(1, 2)          Function call
```

## array

```
let arr: int[5] = [10,20,30,40,50]          list definition
arr[0] = 0                                  use array
```

## struct

```
struct Point {                              struct definition
  x: int
  y: int
}

let p: Point = Point                        use struct
p.x = 10
p.y = 20
``` 

## Inline assembly

```
asm("nop")                      Inline assembly (void expression)
```

## cubical

```
let n: cubical = "cubicalsource.cub"
```

typecheking and evaluation at compile time  
see the syntax document  
[cubical surface language](cubical_surface_language.md)