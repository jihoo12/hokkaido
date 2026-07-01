# Expressions

## Operator precedence

Operators are listed below in descending precedence (tighter binds first). Operators on
the same line have the same precedence and associate left-to-right.

| Precedence | Operators                          | Category              |
|------------|------------------------------------|-----------------------|
| 1          | `()` `[]` `::<>` `.`               | Call / index / turbofish / field access |
| 2          | `*` (deref) `&` (addr) `-` (neg) `~` (bitnot) `!` (not) | Unary prefix     |
| 3          | `*` `/` `%`                        | Multiplicative        |
| 4          | `+` `-`                            | Additive              |
| 5          | `<<` `>>`                          | Shift                 |
| 6          | `&`                                | Bitwise AND           |
| 7          | `^`                                | Bitwise XOR           |
| 8          | `\|`                               | Bitwise OR            |
| 9          | `==` `!=` `<` `>` `<=` `>=`        | Comparison            |
| 10         | `&&`                               | Logical AND           |
| 11         | `\|\|`                             | Logical OR            |
| 12         | `=` `+=` `-=` `*=` `/=` `&=` `\|=` `^=` `<<=` `>>=` | Assignment / compound |

## Comparison operators

Return `bool` (`true` or `false`). Available on all integer and float types.
Pointers can be compared with `==` and `!=`.

```
==    Equal
!=    Not equal
<     Less than
>     Greater than
<=    Less than or equal
>=    Greater than or equal
```

```
let a: int = 10
let b: int = 20
let eq: bool = a == b      // false
let lt: bool = a < b       // true
let ne: bool = a != b      // true
```

Comparison operators are at precedence level 9, lower than bitwise and shift,
so `x & 3 == 0` parses as `x & (3 == 0)`. Use parentheses for the intended meaning:
`(x & 3) == 0`.

## Logical operators

Short-circuiting boolean operators. Operands are coerced to `bool` (non-zero is truthy).
Return `bool`.

```
&&    Logical AND — evaluates RHS only if LHS is truthy
||    Logical OR — evaluates RHS only if LHS is falsy
!     Logical NOT (unary prefix)
```

```
let a: bool = true
let b: bool = false
let c: bool = a && b        // false
let d: bool = a || b        // true
let e: bool = !a            // false
```

`!` is a unary prefix operator at precedence level 2. `&&` is level 10, `||` is level 11.

## Bitwise operators

Operate on integer types. Not available on floats or pointers.

```
&     Bitwise AND
|     Bitwise OR
^     Bitwise XOR
~     Bitwise NOT (unary prefix)
```

```
let a: int = 0xFF00
let b: int = 0x0FF0
let c: int = a & b          // 0x0F00
let d: int = a | b          // 0xFFF0
let e: int = a ^ b          // 0xF0F0
let f: int = ~a             // 0xFFFFFFFFFFFF00FF
```

Bitwise AND `&` is at level 6, XOR `^` at level 7, OR `|` at level 8. Bitwise NOT `~`
is a unary prefix at level 2.

Note: `&` as a binary operator is bitwise AND. The address-of operator is a unary prefix `&`,
distinguished by context.

## Arithmetic operators

Available on all integer and float types. Integer overflow is two's complement wraparound
(as in LLVM, not UB — but this may change to poison in the future).

```
+     Addition
-     Subtraction (binary) / Negation (unary prefix)
*     Multiplication
/     Division
%     Modulo (remainder)
```

```
let x: int = 10 + 20        // 30
let y: int = x - 5          // 25
let z: int = x * y          // 750
let q: int = z / 10         // 75
let r: int = q % 7          // 75 % 7 = 5
let n: int = -q             // -75
```

`*`, `/`, and `%` are level 3 (multiplicative). `+` and `-` (binary) are level 4.
Unary `-` (negation) is level 2.

## Shift operators

Shift left and shift right on integer types. Shift amount must be non-negative. The
behavior for shift amounts equal to or greater than the bit width is poison (LLVM
poison value).

```
<<    Shift left (zero-fill)
>>    Shift right (arithmetic — sign-extending)
```

```
let x: int = 1
let y: int = x << 3         // 8
let z: int = y >> 2         // 2
```

Shift operators are at precedence level 5.

## Assignment

The assignment operator `=` evaluates its right-hand side and assigns the value to the
left-hand side (which must be a place — a variable, a pointer dereference, or an array
subscript). Assignment is an expression: it evaluates to the assigned value.

```
let x: int = 10
let y: int = (x = 20)       // assigns 20 to x, y is also 20
```

### Compound assignment operators

Compound assignment applies an operation and an assignment in one step:

```
+=    Add and assign
-=    Subtract and assign
*=    Multiply and assign
/=    Divide and assign
%=    Modulo and assign
&=    Bitwise AND and assign
|=    Bitwise OR and assign
^=    Bitwise XOR and assign
<<=   Shift left and assign
>>=   Shift right and assign
```

```
let x: int = 10
x += 5                       // x = 15
x *= 2                       // x = 30
x &= 0xFF                    // x = 30
```

Compound assignment operators all have the same precedence as `=` (level 12), the lowest.

### Target evaluation

For assignment and compound assignment, the left-hand side (the "lvalue") is evaluated
first to obtain a pointer, then the right-hand side is evaluated, and finally the store
(or load-modify-store for compound) is performed. The lvalue expression is evaluated
exactly once — there is no double evaluation.

## Pointers

Pointers are typed references to memory locations.

### Address-of

The unary `&` operator takes any lvalue (variable, array element, struct field,
dereference) and produces a pointer to it.

```
let x: int = 42
let p: int* = &x
```

### Dereference

The unary `*` operator reads or writes through a pointer.

```
let val: int = *p            // read: val = 42
*p = 99                      // write: x is now 99
```

Dereference can be the target of assignment, including compound assignment:

```
*p = 100
*p += 5                      // x is now 105
```

### Null pointers

`null` is a keyword that evaluates to a null pointer of any pointer type. Dereferencing
a null pointer is undefined behavior (typically a segfault).

```
let p: int* = null
```

### Pointer arithmetic

Pointer arithmetic is performed by integer addition/subtraction on pointer-typed
expressions. The stride is always 1 element — no implicit scaling by element size.
(Note: this is the reverse of C — `p + n` advances by `n` bytes, not `n * sizeof(T)`.
This design choice simplifies low-level memory manipulation.)

```
let arr: int[4] = [10, 20, 30, 40]
let p: int* = &arr[0]
let a: int = *p              // 10
let b: int = *(p + 8)        // 20 — advances 8 bytes (one int64)
let c: int = *(p + 16)       // 30
```

## Function calls

A function call evaluates the argument expressions left to right, then transfers control
to the function.

```
result = name(arg1, arg2, arg3)
```

### Turbofish type arguments

When calling a [generic function](syntax-functions.md#generic-functions), provide explicit
type arguments with the `::< >` syntax:

```
let x: int = identity::<int>(42)
let y: float64 = pair::<int, float64>(42, 3.14)
let z: int = util::wrap::<int>(99)    // namespaced generic call
```

The turbofish sequence `::<` is parsed as a single token by the parser and never
conflicts with the less-than operator.

### Method-call syntax

There is no method-call syntax. All functions are called with the function name first.
Struct or enum "methods" use a flat function or are placed in a namespace:

```
namespace point {
    fn distance(p: Point, q: Point) -> float64 { /* ... */ }
}
let d = point::distance(p, q)
```
