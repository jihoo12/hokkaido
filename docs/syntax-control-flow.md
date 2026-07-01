# Control Flow

## If / Else

Conditional execution with `if`, optional `else if`, and optional `else`:

```
if condition {
    // runs when condition is truthy (non-zero)
} else if other_condition {
    // runs when condition is falsy AND other_condition is truthy
} else {
    // runs when all preceding conditions are falsy
}
```

### Condition semantics

The condition is coerced to `bool`: any non-zero value is truthy, zero is falsy.
A condition can be any expression of integer, boolean, or pointer type. The compiler
generates an `icmp ne` comparison when the expression type is not already `i1` (bool).

```
let x: int = 42
if x {
    // runs because 42 ≠ 0
}

let p: int* = &x
if p {
    // runs because pointer is non-null
}
```

### Nested and chained

```
if a {
    if b {
        // both a and b are truthy
    }
} else if c {
    // a is falsy and c is truthy
} else {
    // all false
}
```

The `if` construct is an *expression*, not a statement — it does not produce a value
but can be used in expression position in some contexts (the grammar may be extended
in the future to support `if` expressions that yield a value).

## For loop

A `for` loop has three parts: initializer, condition, and update expression, separated
by semicolons inside parentheses:

```
for init; condition; update {
    body
}
```

All three parts are optional. The loop executes:

1. **init** — once before the first iteration. Typically a `let` declaration or assignment.
2. **condition** — evaluated before each iteration (including the first). If falsy, the loop exits.
3. **body** — executed when the condition is truthy.
4. **update** — evaluated after each body execution, then back to step 2.

```
for let i: int = 0; i < 10; i = i + 1 {
    // runs 10 times, i = 0 .. 9
}
```

### While-like loop

Omit the init and update for a while loop:

```
let done: bool = false
for ; done; {
    // runs while done is false
    // ...
    done = true
}
```

### Infinite loop

Omit all three parts:

```
for ;; {
    // runs forever — exit with break or return
}
```

### Break

The `break` statement exits the innermost enclosing loop immediately, transferring
control to the first statement after the loop.

```
for ;; {
    if done {
        break
    }
    // ...
}
// execution continues here after break
```

### Continue

The `continue` statement skips the rest of the current loop iteration and jumps
to the loop's update expression (if any), followed by the condition check.

```
for let i: int = 0; i < 10; i = i + 1 {
    if i % 2 == 0 {
        continue      // skip even numbers
    }
    // process odd i only
}
```

### Errors

Using `break` or `continue` outside a loop is a compile-time error.

### Update expression

The update can be any expression whose side effects are useful. Compound assignment
works well:

```
for let i: int = 0; i < 10; i += 1 {
    // ...
}
```

## Match

The `match` expression performs pattern matching on an enum value, dispatching to the
arm that corresponds to the variant's tag.

```
match value {
    Variant1 { field1, field2 } => {
        // body using field1, field2
    }
    Variant2 { field } => {
        // body using field
    }
    // ...
}
```

Each arm consists of a variant name, an optional field-binding list in curly braces,
`=>`, and a body block.

### Destructuring fields

The fields listed inside the curly braces are bound to local variables of the same name
and type as the enum variant's fields. These local variables are visible only inside the
arm's body.

```
enum Shape {
    Circle { radius: float64 },
    Rect { w: float64, h: float64 },
}

fn area(s: Shape) -> float64 {
    match s {
        Circle { radius } => {
            return 3.14159 * radius * radius
        }
        Rect { w, h } => {
            return w * h
        }
    }
}
```

### Completeness

The compiler does not currently check that all variants are covered. If a variant is
not matched, it is ignored (the arm body is not reached). If no arm matches (which can
happen with unmatched variants), the behavior is undefined — there is no default arm
or catch-all pattern.

### Non-enum match

Matching on non-enum types (integers, strings) is not supported. Only enum-tagged dispatch
is available.

### Expression context

`match` is an expression. The body blocks evaluate to whatever they evaluate to, but
currently the result is not unified across arms — each arm executes its body for side
effects and/or `return`. A future version may allow match to yield a value.
