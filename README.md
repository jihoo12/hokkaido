# MyLang Documentation

This document describes the syntax and features of the MyLang toy compiler.

## 1. Types

The language supports the following primitive types:

- `i32`: 32-bit signed integer
- `i64`: 64-bit signed integer
- `double`: Double-precision floating point number

## 2. Variable Declaration

Variables must be declared with an explicit type. The syntax is:

```
let <Type> <VariableName> = <Expression>
```

**Examples:**

```
let i32 a = 10
let double b = 3.14
let i64 count = 1000
```

### Implicit Casting
The compiler supports implicit type casting during assignment:
- **Int to Double**: You can assign an integer value to a `double` variable. It will be automatically converted.
- **Double to Int**: You can assign a double value to an `int` variable. It will be truncated (converted to integer).

```
let i32 x = 10
let double y = x   // Valid: x is converted to double
let i32 z = 5.9    // Valid: 5.9 is converted to 5
```

## 3. Arithmetic Operations

The standard arithmetic operators are supported:
- `+` (Addition)
- `-` (Subtraction)
- `*` (Multiplication)
- `/` (Division)

**Mixed-Type Arithmetic:**
If you perform arithmetic between an integer and a double, the integer is automatically promoted to a double for the calculation.

```
let i32 a = 5
let double b = 2.5
let double c = a + b  // Result is 7.5 (double)
```

## 4. return Statement

The program must end with a return statement (or it will default to returning 0).

```
return <Expression>
```

**Example:**
```
let i32 a = 10
let i32 b = 20
return a + b
```

## 5. Control Flow

The language supports basic jumping and conditional branching.

### Labels
Define a jump target using the `label` keyword.

```
label my_target
```

### Comparison
Use `cmp` to compare two values. This sets the internal comparison flag used by branches.

```
cmp <a>, <b>
```
This computes `a - b`. If the result is **non-zero** (i.e., `a != b`), the flag is set.

### Branching
Use `br` to conditionally jump to a label.

```
br <label_name>
```
The jump occurs **only if** the result of the last `cmp` was **not equal to zero**. If `cmp` resulted in zero (equality), execution falls through to the next instruction.

**Example (Loop-like structure):**
```
let i32 x = 10
label start
cmp x, 0        // Compare x with 0
br end          // If x != 0, jump to 'end' (Wait, logic is: if diff != 0 jump. So if x=10, diff=10, jumps.)
                // Note: The logic is currently "Jump if Not Equal".
```
*(Note: The current implementation of `br` jumps if the comparison result is **non-zero**.)*

## 6. Inline Assembly

You can execute inline LLVM assembly strings and pass variables as operands.
We follow **GCC-style syntax**.

```
asm "assembly_code", arg1, arg2, ...
```

### Important: Pass-by-Reference
Variables passed to the `asm` block are passed by **address (pointer)**.
To modify a variable, you must **dereference** the register.

- `%0`, `%1`: Registers holding the **pointers** to your variables.
- `(%0)`: Accessing the **value** at that pointer.

### Ambiguity & Suffixes
Because you are accessing memory directly, you **must use explicit instruction suffixes** (like `l` for long/4-byte) to tell the assembler the size of the operation.

- `movl`: Move 4 bytes (for `i32`)
- `movq`: Move 8 bytes (for `i64`)

**Example:**
```
let i32 val = 10

// Correct: Move immediate 20 into the memory address at %0 using 4-byte move
asm "movl $20, (%0)", val

return val // Returns 20
```

This passes the *address* of `val` to the assembly snippet. The instruction `movl $20, (%0)` writes the value 20 to that address.
