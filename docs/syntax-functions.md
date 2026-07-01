# Functions

## Function declarations

A function is declared with the `fn` keyword, followed by the name, parameter list,
optional return type, and a body block:

```
fn name(param1: type1, param2: type2) -> return_type {
    body
}
fn name(param: type) {
    // implicitly returns void
}
```

### Parameters

Each parameter has an explicit type annotation, just like `let`. Parameters are always
passed by value. There are no default arguments, optional parameters, or variadic
parameters (except in [C FFI](syntax-ffi-cubical.md#c-ffi) declarations).

```
fn greet(name: string, count: int) {
    // ...
}
```

### Return type

When a function returns a value, the return type is written after `->`:

```
fn add(a: int, b: int) -> int {
    return a + b
}
```

If the arrow and type are omitted, the function implicitly returns `void`:

```
fn log(msg: string) {
    // no return needed — implicitly returns void
}
```

### The main function

Every program must have a `main` function. It takes no parameters and returns `int`:

```
fn main() -> int {
    return 0
}
```

The return value of `main` is the process exit code. A value of `0` conventionally
indicates success; any non-zero value indicates an error.

### Calling functions

Functions are called with the standard `name(args)` syntax:

```
let result: int = add(3, 4)
log("hello")
```

See [Function calls](syntax-expressions.md#function-calls) for details on call expressions,
including the turbofish syntax for providing type arguments to generic functions.

### Forward references

A function can call any other function declared in the same file (or included file),
regardless of declaration order. Top-to-bottom ordering is not required.

```
fn early() -> int {
    return late()     // OK — late is defined later in the file
}

fn late() -> int {
    return 99
}
```

### All paths must return

Hokkaido does not perform control-flow analysis to verify that all paths return a value.
If a function declared with `-> return_type` reaches the end of its body without encountering
a `return` statement, the behavior is undefined (LLVM poison).

```
fn oops() -> int {
    // no return — undefined behavior at runtime!
}
```

## Generic functions

A function can be generic over one or more type parameters. Type parameters are declared
inside angle brackets immediately after the function name:

```
fn identity<T>(x: T) -> T {
    return x
}
fn pair<A, B>(a: A, b: B) -> A {
    return a
}
```

Type parameters can be used anywhere a type annotation appears: parameter types,
return type, local variable types, or as type arguments to other generic functions.

### Explicit type arguments (turbofish)

Generic functions must be called with explicit type arguments using the turbofish `::< >`
syntax. The compiler *never* infers type arguments.

```
let a: int = identity::<int>(42)
let b: float64 = identity::<float64>(3.14)
let f: int = pair::<int, float64>(42, 3.14)
```

The turbofish `::<` sequence disambiguates the opening `<` from the less-than operator.

### Generic calling generic

A generic function may call another generic function, passing its own type parameters
as arguments. The compiler substitutes the concrete types at monomorphization time.

```
fn identity<T>(x: T) -> T {
    return x
}
fn wrap<T>(x: T) -> T {
    return identity::<T>(x)      // T is substituted later
}
fn main() -> int {
    return wrap::<int>(42)       // → monomorphizes both wrap<int> and identity<int>
}
```

### Monomorphization

Each distinct combination of type arguments generates a separate copy of the function
body at compile time. For example, `identity::<int>` and `identity::<float64>` produce
two independent LLVM functions.

### Namespaced generics

Generic functions inside [namespaces](syntax-modules.md#namespaces) work the same way:

```
namespace util {
    fn identity<T>(x: T) -> T {
        return x
    }
}
fn main() -> int {
    return util::identity::<int>(42)
}
```

### Current limitations

- Generic *types* (structs, enums, arrays) are not supported — only generic functions.
- Type parameters cannot have bounds or constraints.
- Type parameters cannot be used in array sizes (e.g. `int[N]` is not valid).

## Return

The `return` statement exits the current function and optionally yields a value.

```
return              // exits a void function
return expression   // evaluates expression and returns it
```

```
fn add(a: int, b: int) -> int {
    return a + b
}
fn done() {
    return          // void return — optional at end of function
}
```

If `return` is used outside a function body, it is a compile error.
