# Hokkaido Language Syntax

Hokkaido is a small, explicit, systems-programming language that compiles to native code via LLVM.
It draws inspiration from Rust, C, and modern language design while keeping the feature set
deliberately minimal.

## Contents

- [Comments](#comments)
- [Types](syntax-types.md#types)
- [Variables](syntax-types.md#variables)
- [Functions](syntax-functions.md#functions)
- [Generic functions](syntax-functions.md#generic-functions)
- [Return](syntax-functions.md#return)
- [If / Else](syntax-control-flow.md#if--else)
- [For loop](syntax-control-flow.md#for-loop)
- [Break](syntax-control-flow.md#break)
- [Continue](syntax-control-flow.md#continue)
- [Match](syntax-control-flow.md#match)
- [Operator precedence](syntax-expressions.md#operator-precedence)
- [Comparison operators](syntax-expressions.md#comparison-operators)
- [Logical operators](syntax-expressions.md#logical-operators)
- [Bitwise operators](syntax-expressions.md#bitwise-operators)
- [Arithmetic operators](syntax-expressions.md#arithmetic-operators) (including `%` modulo)
- [Shift operators](syntax-expressions.md#shift-operators)
- [Assignment](syntax-expressions.md#assignment)
- [Pointers](syntax-expressions.md#pointers)
- [Function calls](syntax-expressions.md#function-calls)
- [Arrays](syntax-data-structures.md#arrays)
- [Structs](syntax-data-structures.md#structs)
- [Enums](syntax-data-structures.md#enums)
- [Namespaces](syntax-modules.md#namespaces)
- [Include](syntax-modules.md#include)
- [C FFI](syntax-ffi-cubical.md#c-ffi)
- [Freestanding mode](syntax-ffi-cubical.md#freestanding-mode)
- [Inline assembly](syntax-ffi-cubical.md#inline-assembly)
- [Cubical](syntax-ffi-cubical.md#cubical)
- [Example: Number guessing game](example-guess.md)

## Comments

Hokkaido supports two comment forms:

```
// Line comment — everything from // to the end of the line is ignored.

/* Block comment — can span multiple lines.
   Block comments do NOT nest: the first */ ends the comment
   even if it appears inside what looks like a nested /* ... */ pair. */
```

Line comments (`//`) are the idiomatic choice for most documentation. Block comments (`/* */`)
are useful for temporarily disabling large regions during development.
