# Types and Variables

## Types

Every value in Hokkaido has a type, specified with a type annotation using colon syntax:
`name: type`.

### Primitive types

| Type      | Description                          | Size      |
|-----------|--------------------------------------|-----------|
| `int8`    | 8-bit signed integer                 | 1 byte    |
| `int32`   | 32-bit signed integer                | 4 bytes   |
| `int64`   | 64-bit signed integer                | 8 bytes   |
| `int`     | Shorthand for `int64`                | 8 bytes   |
| `float16` | 16-bit floating point (half-precision) | 2 bytes |
| `float32` | 32-bit floating point                | 4 bytes   |
| `float64` | 64-bit floating point (double)       | 8 bytes   |
| `float`   | Shorthand for `float64`              | 8 bytes   |
| `bool`    | Boolean — `true` or `false`          | 1 byte    |
| `string`  | Opaque string type (internally a pointer) | 8 bytes |
| `void`    | No value (function returns only)     | 0 bytes   |
| `cubical` | Compile-time cubical expression (see [Cubical](syntax-ffi-cubical.md#cubical)) | 8 bytes |

```
let a: int8 = -128
let b: int32 = 2000000
let c: int64 = 9000000000000000000
let d: int = 42                    // same as int64
let e: float16 = 1.0              // half-precision
let f: float32 = 2.0              // single-precision
let g: float64 = 3.14159265358979
let h: float = 2.71828            // same as float64
let flag: bool = true
let msg: string = "hello"
```

### Struct and enum types

A user-defined [struct](syntax-data-structures.md#structs) or [enum](syntax-data-structures.md#enums) name is
also a valid type:

```
struct Point { x: int, y: int }
let p: Point = Point { x: 10, y: 20 }

enum Option { Some { value: int }, None }
let v: Option = Some { value: 42 }
```

### Pointer types

A pointer type is written by appending `*` to the element type — one `*` per level of indirection:

```
int8*      Pointer to int8
int64*     Pointer to int64
int64**    Pointer to pointer to int64
Point*     Pointer to a Point struct
```

Examples:

```
let x: int = 42
let p: int* = &x           // pointer to x
let pp: int** = &p         // pointer to pointer
let val: int = *p          // dereference → 42
```

See [Pointers](syntax-expressions.md#pointers) for more detail.

### Array types

An array type is written by appending `[size]` to the element type:

```
int[5]     Array of 5 int64 values
int8[256]  Array of 256 int8 values
Point[10]  Array of 10 Point structs
```

Array size must be a literal integer (compile-time constant). See [Arrays](syntax-data-structures.md#arrays).

### Type equivalence

Two types are considered the same only when they have the same kind, the same pointer depth,
the same array size (or neither is an array), and (for `Struct`/`Enum`) the same name.
There are no implicit conversions except:

- Integer literals coerce to the expected type when the target type is unambiguous.
- `bool` (`i1`) is zero-extended to wider integer types when needed (e.g. in arithmetic).
- `int64` values are silently truncated to `int32` when assigned to an `int32` variable.

## Variables

### Declaration

Variables are declared with `let`, which always requires a type annotation and an initializer:

```
let name: type = expression
```

```
let count: int = 0
let label: string = "hello"
let ptr: int* = &count
let flag: bool = true
let arr: int[3] = [10, 20, 30]
let p: Point = Point { x: 1, y: 2 }
```

### Mutability

Variables are mutable by default. Reassign using `=`:

```
let x: int = 10
x = 20               // OK — x is now 20
```

### Scopes

`let` can appear at the top level of a file (outside any function) or inside a function body
or block `{ }`.

- **Top-level** `let`s are initialized once, before `main` runs. They are visible to all
  functions in the file and across `include` boundaries.
- **Local** `let`s inside a function or block are scoped to that block and are destroyed
  when execution leaves the block.

```
let top_level: int = 100          // top-level, visible everywhere

fn main() -> int {
    let local: int = top_level    // local, scoped to main
    {
        let inner: int = 99       // scoped to this block
        local = inner             // OK — both in scope
    }
    // inner is out of scope here
    return local
}
```

### Shadowing

An inner scope may declare a variable with the same name as one in an outer scope, temporarily
hiding the outer one:

```
let x: int = 1
{
    let x: int = 2        // shadows the outer x
    // x is 2 here
}
// x is 1 again here
```

### Restrictions

- A variable cannot have type `void`.
- Every variable must be initialized at the point of declaration. There is no default
  zero-initialization for local variables (unlike top-level `let`s, which are zeroed if the
  initializer evaluates to zero or is omitted — but omission is not valid syntax; the
  initializer is always required syntactically).
