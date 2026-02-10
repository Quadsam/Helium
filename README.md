# Helium

A compiler and language built by Quadsam & Gemini


## Code Syntax

1. Variable Declarations

Variable declarations should be C-style (`type name = value;`)

```C
int x = 10;
```

---

2. Control Flow & Braces

```C
fn main()
{
	int i = 0;
	while i < 10 {
		i++;
	}
}
```

We don't require parentheses for `if`, `for`, and `while`.

We use `else if`


---

3. Function Definitions

Definitions should look like this: `fn add(a: int, b: int) -> int { ... }`


---

4. Type System

For the V1 compiler, types are limited to what can fit directly on x86-64 registers:

- `int`  (64-bit integer, maps to `rax`)
- `char` (8-bit unsigned, for chars/strings)
- `ptr`  (generic pointer)

---

5. Misc Features

1. **Direct Assembly inlining**: Drop `asm` blocks directly into the code
2. **No header files**: Just include files directly: `import "file.he"`
3. **Built in `syscall`**: A keyword to trigger Linux syscalls without `libc`

---

### Example code

```C
import "std/io"

fn main()
{
	int count = 10;
	int result = 0;

	while count > 0 {
		result = result + count;
		count = count - 1;
	}

	return result;
}
```

