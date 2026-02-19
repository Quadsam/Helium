# Helium Compiler 🎈

**Helium** is a lightweight, C-like systems programming language that compiles directly to **x86_64 Assembly (NASM)**.

It is designed to be simple, transparent, and capable of generating bare-metal Linux executables without relying on `libc` or complex runtimes. The compiler itself is written in C and is self-hosting capable.

## ✨ Features

* **Native Compilation:** Generates clean, readable x86_64 assembly (Intel syntax).
* **Zero Dependencies:** Output binaries are linked with `ld` and use raw Linux syscalls.
* **Types:** Strong support for 64-bit integers (`int`), pointers (`ptr`), and 1-byte characters (`char`).
* **Memory Management:** Stack-based variables, arrays (`int arr[10]`), pointer arithmetic, and heap allocation (`malloc` / `free`).
* **Structs:** Group data together with `struct` and access heap members effortlessly with the `->` operator.
* **Control Flow:** `if`, `else`, `while`, and dual-syntax `for` loops (C-style and Rust-style).
* **Optimizer:** Built-in Constant Folding and Dead Code Elimination (DCE) for lean, fast binaries.
* **Rich Diagnostics:** Beautiful, precise compiler error messages pointing to the exact line and column.
* **Standard Library:** Includes a custom `std.he` for string manipulation, memory mapping, file I/O, and process control.

---

## 🚀 Quick Start

### 1. Prerequisites

You need **GCC** (to build the compiler), **NASM** (to assemble the output), and **LD** (to link the binary).

```bash
sudo pacman -S --needed gcc nasm
```

### 2. Build the Compiler

Clone the repository and build the `helium` executable:

```bash
git clone https://github.com/Quadsam/Helium
make
make test
```

### 3. Compile "Hello World"

Create a file named `main.he`:

```c
#include "lib/std.he"

fn main(argc: int, argv: ptr) -> int
{
    print("Hello from Helium!\n");
    return 0;
}
```

Compile and run it:

```bash
# 1. Compile to Assembly
./bin/heliumc -o main.s main.he

# 2. Assemble to Object File
nasm -felf64 -o main.o main.s

# 3. Link to Executable
ld -o main main.o

# 4. Run
./main
```

---

## 📖 Language Reference

### Variables & Types

Helium supports 64-bit signed integers, pointers, and 1-byte characters.

```c
int x = 42;          // 64-bit integer
ptr str = "Hello";   // Pointer to string literal
char c = 'A';        // 1-byte character
int arr[10];         // Array of 10 integers (80 bytes)

x = x + 1;           // Assignment
arr[0] = 5;          // Array access

```

### Structs & Heap Memory

Helium supports custom data structures and a direct page allocator (`malloc`). You can calculate sizes at compile time using `sizeof()`.

```c
struct Point {
    x: int,
    y: int
}

fn main()
{
    // Allocate 16 bytes on the heap
    Point p = malloc(sizeof(Point));

    // Access members effortlessly with the arrow operator
    p->x = 10;
    p->y = 20;

    free(p);
}
```

### Pointers

You can take the address of variables and dereference pointers.

```c
int a = 10;
int p = &a;  // p now holds the address of a
*p = 20;     // a is now 20
```

### Functions & Command Line Args

Functions are defined with the `fn` keyword. The `main` function can automatically capture command-line arguments.

```c
fn add(a: int, b: int) -> int
{
    return a + b;
}

fn main(argc: int, argv: ptr) -> int
{
    int sum = add(10, 20);
    return sum;
}
```

### Control Flow

Helium supports standard C-style `if/else` and `while` loops, plus a highly flexible `for` loop that accepts both C and Rust syntax!

```c
if x > 10 {
    print("Greater");
} else {
    print("Smaller");
}

// C-Style Loop
for (int i = 0; i < 10; i++) {
    print_int(i);
}

// Rust-Style Loop
for i in 0..10 {
    print_int(i);
}
```

### System Calls

You can invoke Linux syscalls directly for ultimate bare-metal control.

```c
// syscall(number, arg1, arg2, arg3, ...)
syscall(1, 1, "Raw Write\n", 10); // Syscall 1 = WRITE
syscall(60, 0);                   // Syscall 60 = EXIT
```

---

## 📚 Standard Library (`std.he`)

The compiler comes with a lightweight standard library that wraps common operations and syscalls.

| Category | Functions |
| --- | --- |
| **I/O** | `print()`, `print_int()`, `print_char()` |
| **Strings** | `strlen()`, `itoa()`, `atoi()` |
| **Memory** | `malloc()`, `free()`, `mmap()`, `munmap()` |
| **Files** | `open()`, `read()`, `write()`, `close()`, `lseek()`, `unlink()`, `mkdir()` |
| **Process** | `fork()`, `execve()`, `wait4()`, `getpid()`, `exit()` |

---

## TODO

* [X] **`char` Type (1-byte support)**
* [X] **Command Line Arguments (`argc`, `argv`)**
* [X] **Simple Heap Allocation (`malloc`, `free`)**
* [X] **Structs & Nested Structs (`p->x`)**
* [X] **`for` loops (C and Rust syntax)**
* [X] **Optimization passes** (Constant Folding, Dead Code Elimination)
* [X] **Detailed error messages** (Source-mapped carets)
* [ ] **Logical Operators (`&&`, `||`):** Currently, we can do `if a == b`, but we cannot do `if a == b && c < d`.
* [ ] **Implicit Bounds Checking**
* [ ] **Type Casting & Type Safety Improvements**
* [ ] **Debug info (DWARF generation)**

---

## 🛠️ Architecture

The compiler follows a traditional single-pass design with a powerful optimizer:

1. **Lexer:** Tokenizes source code into a stream of tokens (Identifiers, Keywords, Symbols) while tracking exact byte offsets.
2. **Preprocessor:** Handles `#include` recursion and `#define` macro substitution.
3. **Parser:** Constructs an Abstract Syntax Tree (AST), handles `sizeof` calculation, and desugars complex syntax (like `0..10` ranges).
4. **Optimizer:** Folds constants at compile time and runs a Dead Code Elimination pass to strip unused library functions.
5. **Code Generator:** Traverses the reachable AST and emits heavily optimized x86_64 NASM assembly instructions.

---

## 📝 License

This project is open source. Feel free to use, modify, and distribute it as you see fit.