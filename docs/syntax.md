# Hokkaido Language Syntax

## Variable declarations

```
let x: int = 42                 Integer variable
let y: float = 3.14             Float variable
let s: string = "hello"         String variable
let n: cubical = "..."          Cubical inline expression
let r: cubical = "file.cub"     Cubical from file
```

## Function definitions

```
fn add(a: int, b: int) -> int {
  return a + b
}

fn log(msg: string) -> void {
  asm("nop")
}
```

## Inline assembly

```
asm("nop")                       Inline assembly (void expression)
```

## Function calls

```
let int z = add(1, 2)          Function call
```
