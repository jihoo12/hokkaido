# Hokkaido Language Syntax

## Comments

```
// Line comment (everything after // is ignored)
```

## Types

```
int         64-bit signed integer
float       64-bit floating point
string      String (internal)
void        No return value (functions only)
```

## Variables

```
let x: int = 42             Integer variable
let y: float = 3.14         Float variable
let s: string = "hello"     String variable
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

Assignment is an expression (returns the assigned value).

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
*       Multiplication
/       Division
```

Standard precedence: `*` `/` bind tighter than `+` `-`.
Unary `-` and `!` bind tighter than all binary operators.

## Function calls

```
let z: int = add(1, 2)          Function call
```

## Inline assembly

```
asm("nop")                      Inline assembly (void expression)
```
