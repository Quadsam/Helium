#include "helium.h"

#define MAX_STACK_SIZE 4096

// Label generation for if/while/strings
static int label_counter = 0;

int new_label()
{
	return label_counter++;
}

/* ========================================================================= */
/* CODE GENERATOR															 */
/* ========================================================================= */

// Symbol Table to map "x" -> stack offset
typedef struct {
	char name[256];
	int offset; // e.g., -8, -16
} Symbol;

Symbol symbols[100];
int symbol_count = 0;
int current_stack_offset = 0;

int get_offset(char *name)
{
	for (int i = 0; i < symbol_count; i++) {
		if (strcmp(symbols[i].name, name) == 0) {
			return symbols[i].offset;
		}
	}
	printf("Error: Undefined variable %s\n", name);
	exit(1);
}

void add_symbol(char *name)
{
	current_stack_offset -= 8;  // Grow stack down by 8 bytes (64-bit)

	// WARNING CHECK
	if ((-current_stack_offset) > MAX_STACK_SIZE) {
		fprintf(stderr, "Warning: Stack overflow detected in function '%s'!\n", current_func_name);
		fprintf(stderr, "		  Variable '%s' pushes usage to %d bytes (Limit: %d)\n", 
				name, -current_stack_offset, MAX_STACK_SIZE);
	}

	strcpy(symbols[symbol_count].name, name);
	symbols[symbol_count].offset = current_stack_offset;
	symbol_count++;
}

void gen_asm(ASTNode *node) {
	if (!node) return;

	switch (node->type) {
		case NODE_INT:
			printf("  mov rax, %d\n", node->int_value); // Load immediate
			printf("  push rax\n");
			break;

		case NODE_VAR_REF: {
			int offset = get_offset(node->var_name);
			printf("  mov rax, [rbp + %d]\n", offset); // Load from stack
			printf("  push rax\n");
			break;
		}

		case NODE_VAR_DECL:
			// Calculate value
			gen_asm(node->left); 
			// Assign symbol
			add_symbol(node->var_name);
			// Move from stack to local variable slot
			printf("  pop rax\n");
			printf("  mov [rbp + %d], rax\n", get_offset(node->var_name));
			break;
		case NODE_ADDR: {
			// &x
			// We need the address of the variable, not its value.
			// This is equivalent to `lea rax, [rbp - offset]`

			// Note: & only works on variables/arrays, not literals like &(5)
			if (node->left->type != NODE_VAR_REF && node->left->type != NODE_ARRAY_ACCESS) {
				printf("; Error: Can only take address of variable\n");
				exit(1);
			}

			int offset = get_offset(node->left->var_name);
			printf("  lea rax, [rbp + %d]\n", offset);

			// If it's an array access (&arr[i]), we need to add the index
			if (node->left->type == NODE_ARRAY_ACCESS) {
			   // This is tricky because we need to evaluate the index first.
			   // For simplicity, let's just support basic variable addresses for now.
			}

			printf("  push rax\n");
			break;
		}

		case NODE_DEREF:
			// *ptr
			gen_asm(node->left); // Evaluate the pointer (puts address in rax)

			printf("  pop rax\n");         // Get Address
			printf("  mov rax, [rax]\n");  // Load value AT that address
			printf("  push rax\n");
			break;

		case NODE_ASSIGN:
			// POINTER ASSIGNMENT (*ptr = val)
			// We check this FIRST because var_name is NULL here.
			if (node->left && node->left->type == NODE_DEREF) {
				gen_asm(node->right);       // Generate Value (pushes to stack)
				gen_asm(node->left->left);  // Generate Pointer Address (pushes to stack)

				printf("  pop rax\n");      // Address
				printf("  pop rbx\n");      // Value
				printf("  mov [rax], rbx\n"); // Write Value to Address
			}
			// ARRAY ASSIGNMENT (x[i] = val)
			else if (node->left && node->left->type == NODE_ARRAY_ACCESS) {
				gen_asm(node->right);      // Push Value
				gen_asm(node->left->left); // Push Index

				int offset = get_offset(node->var_name); // var_name is valid ("x")
				printf("  pop rbx\n");        // Index
				printf("  pop rax\n");        // Value

				// Calc Address: rbp + offset + (index * 8)
				printf("  mov rcx, %d\n", offset);
				printf("  imul rbx, 8\n");
				printf("  add rcx, rbx\n");
				printf("  add rcx, rbp\n");

				printf("  mov [rcx], rax\n"); // Store
			} 
			// STANDARD VARIABLE ASSIGNMENT (x = val)
			else {
				// Safety check
				if (node->var_name == NULL) {
					 fprintf(stderr, "Compiler Error: Assignment with NULL variable name\n");
					 exit(1);
				}

				gen_asm(node->right);
				printf("  pop rax\n");
				printf("  mov [rbp + %d], rax\n", get_offset(node->var_name));
			}
			break;

		case NODE_RETURN:
			gen_asm(node->left);        // Value to return is now on stack
			printf("  pop rax\n");      // Move to rax
			printf("  mov rsp, rbp\n"); // Restore stack pointer
			printf("  pop rbp\n");      // Restore base pointer
			printf("  ret\n");
			break;

		case NODE_BINOP:
			gen_asm(node->left);
			gen_asm(node->right);

			printf("  pop rbx\n"); // Right operand
			printf("  pop rax\n"); // Left operand

			if (node->op == '+') printf("  add rax, rbx\n");
			if (node->op == '-') printf("  sub rax, rbx\n");
			if (node->op == '*') printf("  imul rax, rbx\n");
			if (node->op == '/') {
				printf("  cqo\n");     // Sign extend rax to rdx:rax (NASM equivalent of cqto)
				printf("  idiv rbx\n");
			}
			if (node->op == '&') printf("  and rax, rbx\n");
			if (node->op == '|') printf("  or rax, rbx\n");

			printf("  push rax\n");
			break;

		case NODE_BLOCK:
			ASTNode *stmt = node->left;
			while (stmt) {
				gen_asm(stmt);
				stmt = stmt->next;
			}
			break;

		case NODE_FUNCTION:
			// Set current function for warnings
			current_func_name = node->var_name;

			// Reset symbol table
			symbol_count = 0;
			current_stack_offset = 0;

			// CHECK FOR MAIN -> _start
			if (strcmp(node->var_name, "main") == 0) {
				printf("global _start\n");
				printf("_start:\n");
			} else {
				printf("global %s\n", node->var_name);
				printf("%s:\n", node->var_name);
			}

			printf("  push rbp\n");
			printf("  mov rbp, rsp\n");
			printf("  sub rsp, %d\n", MAX_STACK_SIZE); // Reserve stack space

			// Handle Parameters (Move registers to stack)
			ASTNode *param = node->left;
			int param_idx = 0;
			const char* regs[] = {"rdi", "rsi", "rdx", "rcx", "r8", "r9"};

			while (param) {
				add_symbol(param->var_name);
				int offset = get_offset(param->var_name);

				if (param_idx < 6) {
					printf("  mov [rbp + %d], %s\n", offset, regs[param_idx]);
				}
				param = param->next;
				param_idx++;
			}

			gen_asm(node->body);

			// Epilogue safety
			printf("  mov rsp, rbp\n");
			printf("  pop rbp\n");
			printf("  ret\n");
			break;

		case NODE_IF: {
			int label_else = new_label();
			int label_end = new_label();

			gen_asm(node->left); // Condition
			printf("  pop rax\n");
			printf("  cmp rax, 0\n");
			printf("  je .L%d\n", label_else); // Jump if 0 (False)

			gen_asm(node->body);
			printf("  jmp .L%d\n", label_end);

			printf(".L%d:\n", label_else);
			if (node->right) {
				gen_asm(node->right);
			}

			printf(".L%d:\n", label_end);
			break;
		}

		case NODE_WHILE: {
			int label_start = new_label();
			int label_end = new_label();

			printf(".L%d:\n", label_start);

			gen_asm(node->left);
			printf("  pop rax\n");
			printf("  cmp rax, 0\n");
			printf("  je .L%d\n", label_end);

			gen_asm(node->body);
			printf("  jmp .L%d\n", label_start);

			printf(".L%d:\n", label_end);
			break;
		}

		case NODE_GT:
		case NODE_LT:
		case NODE_EQ:
		case NODE_NEQ:
			gen_asm(node->left);
			gen_asm(node->right);
			printf("  pop rbx\n");
			printf("  pop rax\n");
			printf("  cmp rax, rbx\n");

			if (node->type == NODE_EQ) printf("  sete al\n");
			if (node->type == NODE_NEQ) printf("  setne al\n");
			if (node->type == NODE_GT) printf("  setg al\n");
			if (node->type == NODE_LT) printf("  setl al\n");

			printf("  movzx rax, al\n"); // Zero-extend byte
			printf("  push rax\n");
			break;

		case NODE_SYSCALL: {
			int arg_count = 0;
			ASTNode* arg = node->left;
			while (arg) {
				gen_asm(arg); 
				arg_count++;
				arg = arg->next;
			}

			const char* regs[] = {"rdi", "rsi", "rdx", "r10", "r8", "r9"};

			for (int i = arg_count - 1; i >= 0; i--) {
				if (i == 0) {
					printf("  pop rax\n"); // Syscall number
				} else {
					if (i-1 < 6) {
						printf("  pop %s\n", regs[i-1]);
					}
				}
			}

			printf("  syscall\n");
			printf("  push rax\n"); // Return value
			break;
		}

		case NODE_POST_INC: {
			char* var_name = node->left->var_name;
			int offset = get_offset(var_name);

			printf("  mov rax, [rbp + %d]\n", offset);
			printf("  inc rax\n");
			printf("  mov [rbp + %d], rax\n", offset);
			break;
		}

		case NODE_STRING: {
			int label = new_label();

			printf("  section .rodata\n");
			// NASM string syntax: db "string", 0
			printf(".LC%d: db `%s`, 0\n", label, node->var_name);

			printf("  section .text\n");
			printf("  lea rax, [rel .LC%d]\n", label); // Position Independent Code (PIC) access
			printf("  push rax\n");
			break;
		}

		case NODE_ARRAY_DECL: {
			int size = node->int_value;
			int total_size = size * 8;
			current_stack_offset -= total_size;

			// WARNING CHECK FOR ARRAYS
			if ((-current_stack_offset) > MAX_STACK_SIZE) {
				fprintf(stderr, "Warning: Stack overflow detected in function '%s'!\n", current_func_name);
				fprintf(stderr, "         Array '%s' pushes usage to %d bytes (Limit: %d)\n", 
						node->var_name, -current_stack_offset, MAX_STACK_SIZE);
			}

			add_symbol(node->var_name);
			symbols[symbol_count-1].offset = current_stack_offset; 
			break;
		}

		case NODE_ARRAY_ACCESS: {
			gen_asm(node->left); // Push Index

			int offset = get_offset(node->var_name);
			printf("  pop rbx\n"); // Index
			printf("  mov rax, %d\n", offset);
			printf("  imul rbx, 8\n");
			printf("  add rax, rbx\n");
			printf("  add rax, rbp\n");

			printf("  mov rax, [rax]\n"); // Dereference
			printf("  push rax\n");
			break;
		}

		case NODE_FUNC_CALL: {
			int arg_count = 0;
			ASTNode* arg = node->left;
			while (arg) {
				gen_asm(arg); 
				arg_count++;
				arg = arg->next;
			}

			const char* regs[] = {"rdi", "rsi", "rdx", "rcx", "r8", "r9"};
			for (int i = arg_count - 1; i >= 0; i--) {
				if (i < 6) {
					printf("  pop %s\n", regs[i]);
				}
			}

			printf("  call %s\n", node->var_name);
			printf("  push rax\n");
			break;
		}
	}
}
