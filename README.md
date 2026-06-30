hokkaido — LLVM-based compiler with cubical compile-time evaluation

Usage:
  my_compiler input.hk           Compile a .hk file
  my_compiler input.cub          Evaluate a .cub file
  my_compiler                    show this help

Hokkaido language syntax:
  let int x = 42                 Integer variable
  let float y = 3.14             Float variable
  let string s = "hello"         String variable
  let cubical n = "..."          Cubical inline expression
  let cubical r = "file.cub"     Cubical from file
  
## LICENSE
[LICENSE](LICENSE)
