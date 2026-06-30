# Hokkaido Language Syntax

## Variable declarations

```
let int x = 42                 Integer variable
let float y = 3.14             Float variable
let string s = "hello"         String variable
let cubical n = "..."          Cubical inline expression
let cubical r = "file.cub"     Cubical from file
```

## Function definitions

```
fn add(int a, int b) -> int {
  return a + b
}
```

## Function calls

```
let int z = add(1, 2)          Function call
```
