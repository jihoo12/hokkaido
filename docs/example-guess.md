# Example: Number Guessing Game

This example demonstrates C FFI, loops, conditionals, pointers, and user I/O by
implementing a classic number guessing game.

## The code

```
extern fn puts(s: string) -> int
extern fn printf(fmt: string, ...) -> int
extern fn scanf(fmt: string, ...) -> int
extern fn rand() -> int
extern fn srand(seed: int) -> void
extern fn time(t: int*) -> int

fn main() -> int {
    let t: int = 0
    srand(time(&t))

    let secret: int = rand() % 100 + 1

    let guess: int = 0
    let attempts: int = 0

    puts("Guess the number (1-100)!")

    for ;; {
        printf("Enter guess: ")
        scanf("%ld", &guess)
        attempts = attempts + 1

        if guess < secret {
            puts("Too low!")
        } else if guess > secret {
            puts("Too high!")
        } else {
            printf("Correct! You got it in %ld tries!\n", attempts)
            break
        }
    }

    return 0
}
```

## Running it

```
$ hokkaido guess.hk
$ cc guess.o -o guess
$ ./guess
Guess the number (1-100)!
Enter guess: 50
Too low!
Enter guess: 75
Too high!
Enter guess: 62
Correct! You got it in 3 tries!
```

## What it demonstrates

| Feature                | How it's used |
|------------------------|---------------|
| **C FFI (`extern fn`)** | Calls C standard library: `puts`, `printf`, `scanf`, `rand`, `srand`, `time` |
| **Variadic FFI**        | `printf(fmt: string, ...)` and `scanf(fmt: string, ...)` accept a variable number of arguments |
| **Pointers**            | `&t` passes the address of `t` to `time()`; `&guess` passes the address of `guess` to `scanf()` |
| **For loop**            | Infinite loop `for ;;` with `break` to exit on correct guess |
| **Break**               | Exits the loop when the correct number is guessed |
| **If / else if / else** | Three-way branch for too-low / too-high / correct |
| **Comparison**          | `!=`, `<`, `>` on integer values |
| **Assignment**          | `attempts = attempts + 1` increments the counter |
| **Compound assignment** | Could also write `attempts += 1` |
| **Strings**             | String literals passed directly to C functions |
| **Modulo operator**     | `rand() % 100 + 1` maps a random value to the range `1..100` |

## Notes

- `%ld` in `scanf`/`printf` matches Hokkaido's 64-bit `int` type on x86-64 Linux.
- The game uses an infinite `for` loop with an explicit `break` when the correct
  number is guessed — no condition in the loop header.
