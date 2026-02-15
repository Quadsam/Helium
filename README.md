# Helium Compiler 🎈

**Helium** is a lightweight, C-like systems programming language that compiles directly to **x86_64 Assembly (NASM)**.

It is designed to be simple, transparent, and capable of generating bare-metal Linux executables without relying on `libc` or complex runtimes. The compiler itself is written in C and is self-hosting capable.

## ✨ Features

* **Native Compilation:** Generates clean, readable x86_64 assembly (Intel syntax).
* **Zero Dependencies:** Output binaries are linked with `ld` and use raw Linux syscalls.
* **Types:** Strong support for 64-bit integers (`int`) and pointers (`ptr`).
* **Memory Management:** Stack-based variables, arrays (`int arr[10]`), and pointer arithmetic (`*ptr`, `&val`).
* **Preprocessor:** Supports `#include` for file modularity and `#define` for constants.
* **Control Flow:** `if`, `else`, `while` loops.
* **Standard Library:** Includes a custom `std.he` for string manipulation, I/O, and math.

---

## 🚀 Quick Start

### 1. Prerequisites

You need **GCC** (to build the compiler), **NASM** (to assemble the output), and **LD** (to link the binary).

```bash
sudo apt install build-essential nasm
```

### 2. Build the Compiler

Clone the repository and build the `helium` executable:

```bash
git clone https://github.com/Quadsam/Helium
gcc compiler.c -o helium && ./test.py
```

### 3. Compile "Hello World"

Create a file named `main.he`:

```c
#include "lib/std.he"

fn main()
{
    print("Hello from Helium!\n");
    exit(0);
}
```

Compile and run it:

```bash
# 1. Compile to Assembly
./helium -o main.s main.he

# 2. Assemble to Object File
nasm -f elf64 main.s -o main.o

# 3. Link to Executable
ld main.o -o main

# 4. Run
./main
```

---

## 📖 Language Reference

### Variables & Types

Helium supports 64-bit signed integers and pointers.

```c
int x = 42;          // Variable definition
ptr str = "Hello";   // Pointer to string literal
int arr[10];         // Array of 10 integers (80 bytes)

x = x + 1;           // Assignment
arr[0] = 5;          // Array access

```

### Pointers

You can take the address of variables and dereference pointers.

```c
int a = 10;
int p = &a;  // p now holds the address of a
*p = 20;     // a is now 20

```

### Functions

Functions are defined with the `fn` keyword. They support parameters and return types.

```c
fn add(a: int, b: int) -> int
{
    return a + b;
}

fn main()
{
    int sum = add(10, 20);
    exit(sum);
}

```

### Control Flow

Standard C-style control structures.

```c
if x > 10 {
    print("Greater");
} else {
    print("Smaller");
}

while i < 10 {
    i++;
}

```

### System Calls

You can invoke Linux syscalls directly.

```c
// syscall(number, arg1, arg2, arg3...)
syscall(1, 1, "Raw Write\n", 10); // Syscall 1 = WRITE
syscall(60, 0);                   // Syscall 60 = EXIT

```

---

## 📚 Standard Library (`std.he`)

The compiler comes with a lightweight standard library that wraps common syscalls.

| Function | Description |
| --- | --- |
| `print(str: ptr)` | Prints a string to stdout. |
| `print_int(n: int)` | Prints a signed integer. |
| `strlen(str: ptr)` | Returns the length of a string. |
| `read(fd: int, buf: ptr, count: int)` | Read count bytes from fd to buffer buf. |
| `write(fd: int, str: ptr, count: int)` | Write count bytes to fd from buffer str. |
| `exit(code: int)` | Exits the program with the given status code. |

---

## TODO

1. **Logical Operators (`&&`, `||`):** Currently, we can do `if a == b`, but we cannot do `if a == b && c < d`.

2. **`char` Type (1-byte support):** Right now, ptr math assumes everything is 1 byte, but variables are 8 bytes. Adding a distinct char type would make string manipulation significantly safer and easier than manually masking & 255.

3. **Command Line Arguments:** Impliment `argv` and `argc`

4. **Implicit Bounds Checking**

5. **Detailed error messages**

6. **`for` loops**

7. **Optimization passes on generated assembly**

---


## 🛠️ Architecture

The compiler follows a traditional single-pass design:

1. **Lexer:** Tokenizes source code into a stream of tokens (Identifiers, Keywords, Symbols).
2. **Preprocessor:** Handles `#include` recursion and `#define` macro substitution.
3. **Parser:** Constructs an Abstract Syntax Tree (AST) from the token stream.
4. **Code Generator:** Traverses the AST and emits x86_64 NASM assembly instructions.

---

## 📝 License

This project is open source. Feel free to use, modify, and distribute it as you see fit.