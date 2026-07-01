# Modules

## Namespaces

Namespaces group related functions, types, and top-level variables under a common name.

### Declaration

```
namespace name {
    // declarations
}
```

A namespace can contain function declarations, struct/enum/alias definitions, and
top-level `let` bindings.

```
namespace math {
    fn add(a: int, b: int) -> int {
        return a + b
    }
    fn multiply(a: int, b: int) -> int {
        return a * b
    }
    const PI: float64 = 3.141592653589793
}
```

### Qualified access

Items inside a namespace are accessed with `::`:

```
fn main() -> int {
    let sum: int = math::add(10, 20)        // 30
    let product: int = math::multiply(3, 4) // 12
    return 0
}
```

### Nested namespaces

Namespaces can nest arbitrarily. Access with multiple `::`:

```
namespace outer {
    namespace inner {
        fn hello() -> int {
            return 42
        }
    }
}
fn main() -> int {
    return outer::inner::hello()            // 42
}
```

### Namespace restrictions

- `main` cannot be declared inside a namespace — the entry point must be a top-level
  function named `main`.
- There is no `use` or `import` statement; all references must be fully qualified.
- Namespaces are open — multiple `namespace math { ... }` blocks in the same file or
  across includes add to the same namespace.

## Include

The `include` directive brings in declarations from another `.hk` source file. It
acts as textual inclusion — the included file's top-level declarations become visible
at the point of inclusion.

### Syntax

```
include "relative/path/to/file.hk"
```

The path is relative to the directory containing the current source file.

```
// --- src/vec.hk ---
namespace vec {
    struct Vec2 { x: int, y: int }
}

// --- src/main.hk ---
include "vec.hk"

fn main() -> int {
    let v: vec::Vec2 = vec::Vec2 { x: 1, y: 2 }
    return 0
}
```

### Transitive include

If `a.hk` includes `b.hk`, and `b.hk` includes `c.hk`, then compiling `a.hk` makes
`c.hk`'s declarations visible in both `b.hk` and `a.hk`. There is no private/module
isolation.

### Diamond inclusion

If the same file is included multiple times (e.g. `a.hk` includes both `b.hk` and
`c.hk`, and both include `utility.hk`), the declarations from `utility.hk` are
processed only once. The include mechanism tracks which files have already been
processed to prevent duplicate declarations.

### Include and namespaces

Each include is processed in the *current* namespace context. If you include a file
inside a `namespace` block, the included file's declarations are placed inside that
namespace:

```
// --- types.hk ---
struct Point { x: int, y: int }

// --- main.hk ---
namespace myapp {
    include "types.hk"    // Point is now myapp::Point
}
fn main() -> int {
    let p: myapp::Point = myapp::Point { x: 1, y: 2 }
    return 0
}
```
