#include "helium.h"

#define MAX_STACK_SIZE 4096

// Label generation for if/while/strings
static int label_counter = 0;

int new_label()
{
	return label_counter++;
}


/* ========================================================================= */
/* STRUCT REGISTRY															 */
/* ========================================================================= */

// Structs
StructDef struct_registry[20];
int struct_count = 0;

StructDef *get_struct(const char *name)
{
	for (int i = 0; i < struct_count; i++) {
		if (strcmp(struct_registry[i].name, name) == 0)
			return &struct_registry[i];
	}
	return NULL;
}

/* ========================================================================= */
/* CODE GENERATOR															 */
/* ========================================================================= */

// Symbol Table to map "x" -> stack offset
typedef struct {
	char name[256];
	int offset; // e.g., -8, -16
	char type_name[64]; // int, ptr, Point
} Symbol;

Symbol symbols[100];
int symbol_count = 0;
int current_stack_offset = 0;

Symbol *get_symbol(char *name, int line, int col, int offset)
{
	for (int i = 0; i < symbol_count; i++) {
		if (strcmp(symbols[i].name, name) == 0) {
			return &symbols[i];
		}
	}

	char buffer[256];
	snprintf(buffer, sizeof(buffer), "Undefined variable '%s'", name);
	error_at_pos(line, col, offset, buffer);

	exit(1);
}

// Wrapper for old calls that just want the offset
int get_offset(char *name)
{
	return get_symbol(name, 0, 0, 0)->offset;
}

void add_symbol(char *name, char *type_name, int size)
{
	current_stack_offset -= size;  // Grow stack down by size bytes

	// WARNING CHECK
	if ((-current_stack_offset) > MAX_STACK_SIZE) {
		fprintf(stderr, "Warning: Stack overflow detected in function '%s'!\n", current_func_name);
		fprintf(stderr, "		  Variable '%s' pushes usage to %d bytes (Limit: %d)\n",
				name, -current_stack_offset, MAX_STACK_SIZE);
	}

	strcpy(symbols[symbol_count].name, name);
	strcpy(symbols[symbol_count].type_name, type_name);
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
			Symbol *sym = get_symbol(node->var_name, node->line, node->column, node->offset);

			// If it's a STRUCT, we push its address (like an array)
			// If it's an INT/PTR, we push its value
			StructDef *sdef = get_struct(sym->type_name);
			if (sdef) {
				// Struct: Push address (lea)
				// This allows 'p = p2' to work via memcpy logic if we implemented it,
				// but for now, passing structs by value isn't fully supported.
				// We treat struct vars as their base address for member access.
				printf("  lea rax, [rbp + %d]\n", sym->offset);
			} else if (strcmp(sym->type_name, "char") == 0) {
				printf("  movzx rax, byte [rbp + %d]\n", sym->offset);
			} else {
				printf("  mov rax, [rbp + %d]\n", sym->offset);
			}
			printf("  push rax\n");
			break;
		}

		case NODE_VAR_DECL: {
			// Determine Type & Size
			char *type = node->member_name; // We stored type here in Parser
			if (type == NULL) type = "int"; // Default safety

			int size = 8;
			if (strcmp(type, "char") == 0) size = 1;

			StructDef *sdef = get_struct(type);
			if (sdef) {
				size = sdef->size;
			}

			// Evaluate Initializer (if any)
			if (node->left) {
				 gen_asm(node->left);
			} else {
				// If no initializer (e.g. 'Point p;'), verify we reserve space
				// but we don't push anything.
				// Actually, our stack convention requires the value to be on stack
				// to pop into the variable slot.
				// For structs, we usually just reserve space.
			}

			// Register Symbol
			add_symbol(node->var_name, type, size);

			// Move data from stack to variable slot
			if (node->left) {
				// If it's a primitive, pop into [rbp + offset]
				if (!sdef) {
					printf("  pop rax\n");
					printf("  mov [rbp + %d], rax\n", get_offset(node->var_name));
				}
				// If it's a struct assignment (Point p = other_p), we need memcpy?
				// For this tutorial, we assume 'Point p;' (no init) or manual member init.
			}
			break;
		}

		case NODE_MEMBER_ACCESS: {
			// node->left is the variable (e.g., 'p' in 'p.x')
			// node->member_name is "x"

			if (node->left->type != NODE_VAR_REF) {
				fprintf(stderr, "Error: Member access only supported on variables\n");
				exit(1);
			}

			// 1. Find variable 'p'
			Symbol *sym = get_symbol(node->left->var_name, node->line, node->column, node->offset);

			// 2. Find struct definition 'Point'
			StructDef *sdef = get_struct(sym->type_name);
			if (!sdef) {
				fprintf(stderr, "Error: Variable '%s' is not a struct\n", sym->name);
				exit(1);
			}

			// 3. Find member 'x' offset
			int mem_offset = -1;
			for (int i = 0; i < sdef->member_count; i++) {
				if (strcmp(sdef->members[i].name, node->member_name) == 0) {
					mem_offset = sdef->members[i].offset;
					break;
				}
			}
			if (mem_offset == -1) {
				fprintf(stderr, "Error: Struct '%s' has no member '%s'\n", sdef->name, node->member_name);
				exit(1);
			}

			// 4. Calculate Address: rbp + struct_base + member_offset
			// Stack grows down, but struct layout is positive from base?
			// If p is at -16 (size 16), it spans -16 to 0.
			// sdef->members[0] is at offset 0.
			// So address is (rbp + sym->offset) + mem_offset.

			int total_offset = sym->offset + mem_offset;

			// For reading (right side of assignment): Load value
			// For writing (left side): handled in NODE_ASSIGN special case?
			// Current naive implementation: Load Value.
			printf("  mov rax, [rbp + %d]\n", total_offset);
			printf("  push rax\n");
			break;
		}

		case NODE_ADDR: {
			// &p.x
			if (node->left->type == NODE_MEMBER_ACCESS) {
				ASTNode *access = node->left;
				Symbol *sym = get_symbol(access->left->var_name, node->line, node->column, node->offset);
				StructDef *sdef = get_struct(sym->type_name);

				int mem_offset = 0;
				for (int i=0; i<sdef->member_count; i++) {
					 if (strcmp(sdef->members[i].name, access->member_name) == 0) {
						 mem_offset = sdef->members[i].offset;
						 break;
					 }
				}
				int total_offset = sym->offset + mem_offset;
				printf("  lea rax, [rbp + %d]\n", total_offset);
				printf("  push rax\n");
				break;
			}

			// Standard variable &x
			if (node->left->type == NODE_VAR_REF) {
				int offset = get_offset(node->left->var_name);
				printf("  lea rax, [rbp + %d]\n", offset);
				printf("  push rax\n");
				break;
			}
			// If it's an array access (&arr[i]), we need to add the index
			if (node->left->type == NODE_ARRAY_ACCESS) {
			   // This is tricky because we need to evaluate the index first.
			   // For simplicity, let's just support basic variable addresses for now.
			}
			break;
		}

		case NODE_ASSIGN:
			// MEMBER ASSIGNMENT (p.x = 10)
			if (node->left && node->left->type == NODE_MEMBER_ACCESS) {
				gen_asm(node->right); // Push Value

				ASTNode *access = node->left;
				Symbol *sym = get_symbol(access->left->var_name, node->line, node->column, node->offset);
				StructDef *sdef = get_struct(sym->type_name);

				int mem_offset = 0;
				for (int i=0; i<sdef->member_count; i++) {
					 if (strcmp(sdef->members[i].name, access->member_name) == 0) {
						 mem_offset = sdef->members[i].offset;
						 break;
					 }
				}

				printf("  pop rax\n"); // Value
				int total_offset = sym->offset + mem_offset;
				printf("  mov [rbp + %d], rax\n", total_offset);
			}
			// POINTER ASSIGNMENT (*ptr = val)
			else if (node->left && node->left->type == NODE_DEREF) {
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

				int offset = get_offset(node->left->var_name);

				// Check array type for scaling
				Symbol *sym = get_symbol(node->left->var_name, node->line, node->column, node->offset);
				int is_char_arr = (strncmp(sym->type_name, "char", 4) == 0);
				int scale = is_char_arr ? 1 : 8;

				printf("  pop rbx\n");        // Index
				printf("  pop rax\n");        // Value

				// Calc Address: rbp + offset + (index * 8)
				printf("  mov rcx, %d\n", offset);
				printf("  imul rbx, %d\n", scale);
				printf("  add rcx, rbx\n");
				printf("  add rcx, rbp\n");

				// Store based on type
				if (is_char_arr)
					printf("  mov [rcx], al\n");
				else
					printf("  mov [rcx], rax\n");
			}
			// STANDARD VARIABLE ASSIGNMENT (x = val)
			else {
				if (node->var_name == NULL) {
					 fprintf(stderr, "Compiler Error: Assignment with NULL variable name\n");
					 exit(1);
				}

				Symbol *sym = get_symbol(node->var_name, node->line, node->column, node->offset);
				int offset = sym->offset;

				// OPTIMIZATION: Immediate Assignment
				// If right side is a constant INT, don't push/pop stack
				if (node->right->type == NODE_INT) {
					printf("  mov rax, %d\n", node->right->int_value);
					
					if (strcmp(sym->type_name, "char") == 0)
						printf("  mov [rbp + %d], al\n", offset);
					else
						printf("  mov [rbp + %d], rax\n", offset);
					
					break; // Done!
				}

				// Standard way (for complex expressions)
				gen_asm(node->right);
				printf("  pop rax\n");

				if (strcmp(sym->type_name, "char") == 0)
					printf("  mov [rbp + %d], al\n", offset);
				else
					printf("  mov [rbp + %d], rax\n", offset);
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

			// Handle Parameters
			ASTNode *param = node->left;
			int param_idx = 0;
			const char* regs[] = {"rdi", "rsi", "rdx", "rcx", "r8", "r9"};

			while (param) {
				// For params, type is usually int/ptr.
				// We use member_name as type (see parser).
				char *type = param->member_name ? param->member_name : "int";
				add_symbol(param->var_name, type, 8); // Params are always 8 bytes on stack

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

			// Calculate size based on type
			char *type = node->member_name ? node->member_name : "int";
			int elem_size = (strcmp(type, "char") == 0) ? 1 : 8;
			int total_size = size * elem_size;

			// Store type as "char[]" or "int[]" for symbol table
			char type_sig[64];
			snprintf(type_sig, 64, "%s[]", type);

			add_symbol(node->var_name, type_sig, total_size);
			break;
		}

		case NODE_ARRAY_ACCESS: {
			gen_asm(node->left); // Push Index

			int offset = get_offset(node->var_name);

			// Determine scale
			Symbol *sym = get_symbol(node->var_name, node->line, node->column, node->offset);
			int is_char_arr = (strncmp(sym->type_name, "char", 4) == 0);
			int scale = is_char_arr ? 1 : 8;

			printf("  pop rbx\n"); // Index
			printf("  mov rax, %d\n", offset);
			printf("  imul rbx, %d\n", scale);
			printf("  add rax, rbx\n");
			printf("  add rax, rbp\n");

			// Dereference based on size
			if (is_char_arr)
				printf("  movzx rax, byte [rax]\n");
			else
				printf("  mov rax, [rax]\n");

			printf("  push rax\n");
			break;
		}

		case NODE_DEREF:
			gen_asm(node->left); // Evaluate the pointer (puts address in rax)

			printf("  pop rax\n");         // Get Address
			printf("  mov rax, [rax]\n");  // Load value AT that address
			printf("  push rax\n");
			break;

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

		// Struct definitions are handled entireley by the parser. They do not
		// generate any assembly code.
		case NODE_STRUCT_DEFN: break;
	}
}