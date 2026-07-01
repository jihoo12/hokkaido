# Data Structures

## Arrays

An array is a fixed-size, contiguous sequence of elements of the same type. The size
must be a compile-time constant literal integer.

### Array type

Written as `element_type[size]`:

```
int[5]          // 5 int64 values
int8[256]       // 256 bytes
Point[10]       // 10 Point structs
```

### Array literal

Constructed with square brackets containing comma-separated values:

```
let arr: int[4] = [10, 20, 30, 40]
let bytes: int8[3] = [65, 66, 67]
```

### Array access

Index with square brackets. Indexing is zero-based. Out-of-bounds access is undefined
behavior (no bounds checking at runtime by default).

```
let arr: int[4] = [10, 20, 30, 40]
let first: int = arr[0]       // 10
let third: int = arr[2]       // 30
arr[1] = 99                   // write
```

### Array-to-pointer decay

When an array name is used in a context that expects a pointer, it implicitly converts
("decays") to a pointer to its first element. This is used for pointer arithmetic on
arrays.

```
let arr: int[4] = [10, 20, 30, 40]
let p: int* = arr              // same as &arr[0]
let val: int = *(p + 8)        // 20 — see pointer arithmetic
```

Array subscripts can be used as lvalues for assignment:

```
arr[0] = 99
arr[1] += 5
```

### Memory layout

Elements are laid out consecutively in memory with no padding between elements.
Alignment follows the element type's alignment.

```
// int[4] layout (each int = 8 bytes):
// [0]: bytes 0-7
// [1]: bytes 8-15
// [2]: bytes 16-23
// [3]: bytes 24-31
```

## Structs

A struct is a named product type grouping multiple fields of possibly different types.

### Declaration

```
struct Name {
    field1: type1,
    field2: type2,
    // ...
}
```

The trailing comma after the last field is optional.

```
struct Point {
    x: int,
    y: int,
}
struct Person {
    name: string,
    age: int,
}
```

### Construction

Structs are constructed with curly braces using the struct name:

```
let p: Point = Point { x: 10, y: 20 }
let person: Person = Person { name: "Alice", age: 30 }
```

Fields must be specified in declaration order. There are no named field initializers
at the call site — position matters.

### Field access

Fields are accessed with the dot `.` operator:

```
let px: int = p.x
let age: int = person.age
```

Field access can be the target of assignment:

```
p.x = 99
person.age += 1
```

### Structs as values

Structs are passed by value (copied) into functions and returned by value. For large
structs, use a pointer to avoid copying.

```
fn move_x(p: Point, dx: int) -> Point {
    return Point { p.x + dx, p.y }
}
fn move_x_in_place(p: Point*, dx: int) {
    (*p).x = (*p).x + dx
}
```

### Memory layout

Fields are laid out in declaration order with natural alignment padding between fields.
The struct as a whole has the alignment of its most-aligned field.

## Enums

An enum is a tagged union: it holds one of several named variants, each optionally
carrying fields.

### Declaration

```
enum Name {
    Variant1 { field1: type1, field2: type2 },
    Variant2 { field: type },
    Variant3 {},                    // no fields
}
```

The trailing commas are optional. A variant with no fields uses `{}`. At least one
variant is required.

```
enum Option {
    Some { value: int },
    None {},
}
enum Shape {
    Circle { radius: float64 },
    Rect { w: float64, h: float64 },
}
```

### Construction

An enum value is constructed by naming the variant, like calling a function:

```
let v: Option = Some { value: 42 }
let n: Option = None {}
let c: Shape = Circle { radius: 1.0 }
let r: Shape = Rect { w: 3.0, h: 4.0 }
```

Fields must be provided in declaration order, by position.

### Memory layout

The enum stores a tag (a discriminator integer) plus a union of all variant fields.
The tag is at offset zero and occupies one byte. Fields are laid out after the tag,
at the same offset for all variants, sized to the largest variant. Padding ensures
alignment.

For example, `Option` with `Some { value: int }` and `None {}`:
- Offset 0: 1-byte tag (0 = Some, 1 = None)
- Offset 8: value (aligned to int64's 8-byte alignment)

`None {}` contributes 0 bytes of payload. The total struct size is 16 bytes
(1 byte tag + 7 padding + 8 bytes value).

### Pattern matching

Enum values are destructured with [match](syntax-control-flow.md#match):

```
let s: Shape = Circle { radius: 2.0 }
let area: float64 = match s {
    Circle { radius } => {
        return 3.14159 * radius * radius
    }
    Rect { w, h } => {
        return w * h
    }
}
```

The match selects the arm whose tag matches the enum's tag. Fields from that variant
are bound to local variables with the same name as declared in the enum definition.
