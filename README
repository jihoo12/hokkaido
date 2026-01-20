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

## 5. Inline Assembly

You can execute inline LLVM assembly strings and pass variables as operands.
We follow **GCC-style syntax** for clarity.

```
asm "assembly_code", arg1, arg2, ...
```

### Syntax Rules
- **Variables (Operands)**: Use `%0`, `%1`, etc.
- **Constants (Immediates)**: Use `$` (e.g., `$10`).
- **Registers**: Use normal register names (e.g., `%eax`).

**Example:**
```
let i32 val = 10
// Move constant 20 into variable %0 (which corresponds to 'val')
asm "mov $20, %0", val
```

This is automatically converted to LLVM's internal format by the compiler.
