#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <string.h>

/* ========================================================================= */
/* LEXER                                                                     */
/* ========================================================================= */

typedef enum {
	TOKEN_EOF,
	TOKEN_INT,          // 123
	TOKEN_IDENTIFIER,   // x, main, count
	TOKEN_FN,           // fn
	TOKEN_INT_TYPE,     // int
	TOKEN_RETURN,       // return
	TOKEN_LPAREN,       // (
	TOKEN_RPAREN,       // )
	TOKEN_LBRACE,       // {
	TOKEN_RBRACE,       // }
	TOKEN_COMMA,		// ,
	TOKEN_SEMI,         // ;
	TOKEN_ASSIGN,       // =
	TOKEN_PLUS,         // +
	TOKEN_INC,			// ++
	TOKEN_MINUS,        // -
	TOKEN_STAR,         // *
	TOKEN_SLASH,        // /
	TOKEN_EQ,           // ==
	TOKEN_NEQ,          // !=
	TOKEN_LT,           // <
	TOKEN_GT,           // >
	TOKEN_IF,           // if
	TOKEN_ELSE,         // else
	TOKEN_WHILE,        // while
	TOKEN_SYSCALL,      // syscall
	TOKEN_STRING,		// "string"
} TokenType;

typedef struct {
	TokenType type;
	char name[256]; // To store "main", "count", "int", exc.
	int value;      // For integers
} Token;

char *source_code;
int src_pos = 0;
Token current_token;

// Helper to read the whole file into memory
char *read_file(const char *filename)
{
	FILE *f = fopen(filename, "r");
	if (!f) {
		fprintf(stderr, "Error: Could not open file %s\n", filename);
		exit(1);
	}
	fseek(f, 0, SEEK_END);
	long length = ftell(f);
	fseek(f, 0, SEEK_SET);
	
	char *buffer = malloc(length + 1);
	fread(buffer, 1, length, f);
	buffer[length] = '\0';
	fclose(f);
	return buffer;
}

Token get_next_token()
{
	// Skip whitespace
	while (source_code[src_pos] != '\0' && isspace(source_code[src_pos])) {
		src_pos++;
	}

	if (source_code[src_pos] == '\0') {
		return (Token){TOKEN_EOF, "", 0};
	}

	char current = source_code[src_pos];

	// Handle Identifiers and Keywords
	if (isalpha(current)) {
		Token t;
		int i = 0;
		while (isalnum(source_code[src_pos])) {
			t.name[i++] = source_code[src_pos++];
		}
		t.name[i] = '\0';

		if (strcmp(t.name, "fn") == 0) t.type = TOKEN_FN;
		else if (strcmp(t.name, "int") == 0) t.type = TOKEN_INT_TYPE;
		else if (strcmp(t.name, "return") == 0) t.type = TOKEN_RETURN;
		else if (strcmp(t.name, "if") == 0) t.type = TOKEN_IF;
		else if (strcmp(t.name, "else") == 0) t.type = TOKEN_ELSE;
		else if (strcmp(t.name, "while") == 0) t.type = TOKEN_WHILE;
		else if (strcmp(t.name, "syscall") == 0) t.type = TOKEN_SYSCALL;
		else t.type = TOKEN_IDENTIFIER;
		
		return t;
	}

	// Handle Integers
	if (isdigit(current)) {
		Token t;
		t.type = TOKEN_INT;
		t.value = 0;
		while (isdigit(source_code[src_pos])) {
			t.value = t.value * 10 + (source_code[src_pos++] - '0');
		}
		return t;
	}

	// Handle Symbols
	src_pos++;
	switch (current) {
		case '(': return (Token){TOKEN_LPAREN, "(", 0};
		case ')': return (Token){TOKEN_RPAREN, ")", 0};
		case '{': return (Token){TOKEN_LBRACE, "{", 0};
		case '}': return (Token){TOKEN_RBRACE, "}", 0};
		case ',': return (Token){TOKEN_COMMA, ",", 0};
		case ';': return (Token){TOKEN_SEMI, ";", 0};
		case '+': 
			if (source_code[src_pos] == '+') {
				src_pos++; 
				return (Token){TOKEN_INC, "++", 0};
			}
			return (Token){TOKEN_PLUS, "+", 0};
		case '-': return (Token){TOKEN_MINUS, "-", 0};
		case '*': return (Token){TOKEN_STAR, "*", 0};
		case '/': 
			// Check for comment "//"
			if (source_code[src_pos] == '/') {
				// Skip until newline
				while (source_code[src_pos] != '\0' && source_code[src_pos] != '\n')
					src_pos++;
				// Recursively call get_next_token to get the actual next token
				// after the comment is skipped.
				return get_next_token();
			}
			// Otherwise it's just division
			return (Token){TOKEN_SLASH, "/", 0};
		case '=':
			if (source_code[src_pos] == '=') {
				src_pos++; return (Token){TOKEN_EQ, "==", 0};
			}
			return (Token){TOKEN_ASSIGN, "=", 0};
		case '!':
			if (source_code[src_pos] == '=') {
				src_pos++; return (Token){TOKEN_NEQ, "!=", 0};
			}
			printf("Lexer Error: Expected '!='\n"); exit(1);
		case '<': return (Token){TOKEN_LT, "<", 0};
		case '>': return (Token){TOKEN_GT, ">", 0};
		case '"': {
			Token t;
			t.type = TOKEN_STRING;

			int i = 0;
			// Read until closing quote or EOF
			while (source_code[src_pos] != '"' && source_code[src_pos] != '\0') {
				// Simple handling for escaped newlines (\n)
				if (source_code[src_pos] == '\\' && source_code[src_pos+1] == 'n') {
					t.name[i++] = '\\';
					t.name[i++] = 'n';
					src_pos += 2;
				} else {
					t.name[i++] = source_code[src_pos++];
				}
			}

			t.name[i] = '\0';
			if (source_code[src_pos] == '"') src_pos++;	// Skip closing "
			return t;
		}
		default: 
			fprintf(stderr, "Lexer Error: Unknown char '%c'\n", current);
			exit(1);
	}
}

void advance()
{
	current_token = get_next_token();
}

/* ========================================================================= */
/* AST                                      */
/* ========================================================================= */

typedef enum {
	NODE_INT,		// Integer literal
	NODE_VAR_REF,	// x (usage of a variable)
	NODE_BINOP,		// Math (+, -, *, /)
	NODE_ASSIGN,	// x = ...;
	NODE_VAR_DECL,	// int x = ...;
	NODE_RETURN,	// return x;
	NODE_BLOCK,		// { ... }
	NODE_FUNCTION,	// Function definition
	NODE_IF,		// if ...
	NODE_WHILE,		// while ...
	NODE_GT,		// >
	NODE_LT,		// <
	NODE_EQ,		// ==
	NODE_NEQ,		// !=
	NODE_SYSCALL,	// syscall()
	NODE_POST_INC,	// i++
	NODE_STRING,	// "string"
} NodeType;

typedef struct ASTNode {
	NodeType type;
	int int_value;			// For literals
	char *var_name;			// For references/declarations
	char op;				// For binary ops
	struct ASTNode *left;	// Left child
	struct ASTNode *right;	// Right child
	struct ASTNode *body;	// For functions
	struct ASTNode *next;	// For linked lists in blocks
} ASTNode;

// Helper to make a new node
ASTNode *create_node(NodeType type)
{
	ASTNode *node = malloc(sizeof(ASTNode));
	node->type = type;
	node->left = node->right = node->body = node->next = NULL;
	node->var_name = NULL;
	return node;
}

/* ========================================================================= */
/* PARSER                                                                    */
/* ========================================================================= */

ASTNode *parse_expression();
ASTNode *parse_block();
ASTNode *parse_syscall();

ASTNode *parse_if()
{
	advance(); // Skip 'if'
	
	// Parse Condition (no parens required in Helium)
	ASTNode *condition = parse_expression();
	
	// Parse 'If' Body
	ASTNode *if_body = parse_block();
	
	// Handle 'Else' (Optional)
	ASTNode *else_body = NULL;
	if (current_token.type == TOKEN_ELSE) {
		advance();
		// Support 'else if' by checking if next token is IF
		if (current_token.type == TOKEN_IF) {
			else_body = parse_if();	// Recursion for 'else if'
		} else {
			else_body = parse_block();
		}
	}
	
	ASTNode *node = create_node(NODE_IF);
	node->left = condition;
	node->body = if_body;
	node->right = else_body;
	return node;
}

ASTNode *parse_while()
{
	advance();	// Skip 'while'
	
	ASTNode *condition = parse_expression();
	ASTNode *body = parse_block();
	
	ASTNode *node = create_node(NODE_WHILE);
	node->left = condition;
	node->body = body;
	return node;
}

// Factor: handles integers
ASTNode *parse_factor()
{
	// Handle Integers
	if (current_token.type == TOKEN_INT) {
		ASTNode *node = create_node(NODE_INT);
		node->int_value = current_token.value;
		advance();
		return node;
	}

	// Handle Variables (Identifiers)
	if (current_token.type == TOKEN_IDENTIFIER) {
		ASTNode *node = create_node(NODE_VAR_REF);
		node->var_name = strdup(current_token.name);
		advance();

		// CHECK FOR POST-INCREMENT (++)
		if (current_token.type == TOKEN_INC) {
			advance(); // consume '++'
			ASTNode* inc_node = create_node(NODE_POST_INC);
			inc_node->left = node; // The variable is the child of the increment
			return inc_node;
		}

		return node;
	}

	// Handle Parentheses (Expression grouping)
	if (current_token.type == TOKEN_LPAREN) {
		advance();
		ASTNode *node = parse_expression();
		if (current_token.type != TOKEN_RPAREN) {
			fprintf(stderr, "Syntax Error: Expected ')'\n");
			exit(1);
		}
		advance();
		return node;
	}

	// Handle syscalls
	if (current_token.type == TOKEN_SYSCALL) {
		return parse_syscall();
	}

	// Handle String Literals
	if (current_token.type == TOKEN_STRING) {
		ASTNode *node = create_node(NODE_STRING);
		node->var_name = strdup(current_token.name);	// Store the string content
		advance();
		return node;
	}

	fprintf(stderr, "Syntax Error: Unexpected token in factor\n");
	exit(1);
}

// Term: handles * and /
ASTNode *parse_term()
{
	ASTNode *node = parse_factor();
	while (current_token.type == TOKEN_STAR || current_token.type == TOKEN_SLASH) {
		ASTNode *newNode = create_node(NODE_BINOP);
		newNode->op = (current_token.type == TOKEN_STAR) ? '*' : '/';
		newNode->left = node;
		advance();
		newNode->right = parse_factor();
		node = newNode;
	}
	return node;
}

// Expression: handles + and -
ASTNode *parse_math()
{
	ASTNode *node = parse_term();
	while (current_token.type == TOKEN_PLUS || current_token.type == TOKEN_MINUS) {
		ASTNode *newNode = create_node(NODE_BINOP);
		newNode->op = (current_token.type == TOKEN_PLUS) ? '+' : '-';
		newNode->left = node;
		advance();
		newNode->right = parse_term();
		node = newNode;
	}
	return node;
}

ASTNode *parse_comparison()
{
	ASTNode *node = parse_math();
	
	// While current token is <, >, ==, !=
	while (current_token.type == TOKEN_GT || current_token.type == TOKEN_LT || 
		   current_token.type == TOKEN_EQ || current_token.type == TOKEN_NEQ) {
		
		NodeType type;
		if (current_token.type == TOKEN_GT) type = NODE_GT;
		if (current_token.type == TOKEN_LT) type = NODE_LT;
		if (current_token.type == TOKEN_EQ) type = NODE_EQ;
		if (current_token.type == TOKEN_NEQ) type = NODE_NEQ;
		
		ASTNode *newNode = create_node(type);
		newNode->left = node;
		advance();
		newNode->right = parse_math();
		node = newNode;
	}
	return node;
}

// Update parse_expression to call parse_comparison instead of parse_math
ASTNode *parse_expression()
{
	ASTNode* lhs = parse_comparison();

	// Check for Assignment
	if (current_token.type == TOKEN_ASSIGN) {
		advance();	// consume '='

		// Ensure LHS is actually a variable!
		// You can't do "10 = x" or "(5+5) = x"
		if (lhs->type != NODE_VAR_REF) {
			fprintf(stderr, "Syntax Error: Invalid l-value. Can only assign to variables.\n");
			exit(1);
		}
		ASTNode *assign = create_node(NODE_ASSIGN);
		assign->var_name = lhs->var_name;	// Reuse the name
		assign->left = NULL;

		// The Right-Hand Side (RHS) is another expression		
		assign->right = parse_expression();


		// We can free the 'lhs' node now because we moved its data into 'assignNode'
		free(lhs); // We moved the name to 'assign', so free the shell
		return assign;
	}
	return lhs;
}

ASTNode *parse_statement()
{
	// Return Statement
	if (current_token.type == TOKEN_RETURN) {
		advance();
		ASTNode *node = create_node(NODE_RETURN);
		node->left = parse_expression();
		if (current_token.type != TOKEN_SEMI) {
			fprintf(stderr, "Error: Expected ';' after return\n"); exit(1);
		}
		advance();
		return node;
	}

	// Variable Declaration (int x = ...)
	// Note: We check specifically for 'int', 'char', or 'ptr' types here
	if (current_token.type == TOKEN_INT_TYPE) {
		advance(); // consume 'int'
		if (current_token.type != TOKEN_IDENTIFIER) {
			fprintf(stderr, "Error: Expected variable name\n"); exit(1);
		}
		char *name = strdup(current_token.name);
		advance();
		if (current_token.type != TOKEN_ASSIGN) {
			fprintf(stderr, "Error: Expected '='\n"); exit(1);
		}
		advance();
		
		ASTNode *node = create_node(NODE_VAR_DECL);
		node->var_name = name;
		node->left = parse_expression();

		if (current_token.type != TOKEN_SEMI) {
			fprintf(stderr, "Error: Expected ';' after declaration\n"); exit(1);
		}
		advance();
		return node;
	}

	if (current_token.type == TOKEN_IF) return parse_if();
	if (current_token.type == TOKEN_WHILE) return parse_while();


	// Default: Expression statement (x = 5;)
	ASTNode *node = parse_expression();
	if (current_token.type != TOKEN_SEMI) {
		fprintf(stderr, "Error: Expected ';' after expression\n");
		exit(1);
	}
	advance();
	return node;
}

ASTNode *parse_block()
{
	// Ensure we are strictly looking at a '{'
	if (current_token.type != TOKEN_LBRACE) {
		fprintf(stderr, "Syntax Error: Expected '{' at start of block\n");
	}
	advance();	// Consume '{'

	ASTNode *block = create_node(NODE_BLOCK);
	ASTNode *curr = NULL;

	// Keep parsing statements untill we hit '}' or EOF
	while (current_token.type != TOKEN_RBRACE && current_token.type != TOKEN_EOF) {
		ASTNode *stmt = parse_statement();
		if (!block->left) {
			block->left = stmt;	// Head of list
			curr = stmt;
		} else {
			curr->next = stmt;	// Append to list
			curr = stmt;
		}
	}

	if (current_token.type != TOKEN_RBRACE) {
		printf("Error: Expected '}'\n"); exit(1);
	}
	advance();	// Consume '}'
	return block;
}

ASTNode *parse_function()
{
	if (current_token.type != TOKEN_FN) return NULL;
	advance();	// current_token is 'fn'
	
	// Now current_token is the IDENTIFIER (e.g., main)
	char *name = strdup(current_token.name);
	advance();
	
	// Skip parens (no args support yet)
	// Parse parameters: name : type
	if (current_token.type != TOKEN_LPAREN) { printf("Error: Expected '('\n"); exit(1); }
	advance();
	if (current_token.type != TOKEN_RPAREN) { printf("Error: Expected ')'\n"); exit(1); }
	advance();

	ASTNode *func = create_node(NODE_FUNCTION);
	func->var_name = name;
	func->body = parse_block();
	return func;
}

ASTNode *parse_syscall()
{
	advance();	// Skip 'syscall'
	
	if (current_token.type != TOKEN_LPAREN) {
		fprintf(stderr, "Error: Expected '(' after syscall\n");
		exit(1);
	}
	advance();	// Skip '('
	
	ASTNode *call_node = create_node(NODE_SYSCALL);
	ASTNode *current_arg = NULL;

	// Parse arguments (comma separated)
	while (current_token.type != TOKEN_RPAREN) {
		ASTNode *expr = parse_expression();
		
		if (call_node->left == NULL) {
			call_node->left = expr;		// First arg
			current_arg = expr;
		} else {
			current_arg->next = expr;	// Chain subsequent args
			current_arg = expr;
		}

		if (current_token.type == TOKEN_COMMA) {
			advance();	// Skip ','
		} else if (current_token.type != TOKEN_RPAREN) {
			fprintf(stderr, "Error: Expected ',' or ')'\n"); exit(1);
		}
	}
	
	advance(); // Skip ')'
	return call_node;
}

void free_ast(ASTNode *node)
{
	if (node == NULL) return;

	// Recursively free all children
	free_ast(node->left);
	free_ast(node->right);
	free_ast(node->body); // For functions

	// For blocks, we need to free the linked list of statements
	free_ast(node->next); 

	// Free the string if this node has one (var_name or func_name)
	if (node->var_name) free(node->var_name);

	// Finally, free the node container itself
	free(node);
}

int label_counter = 0;
int new_label()
{
	return label_counter++;
}

/* ========================================================================= */
/* CODE GENERATOR                                                            */
/* ========================================================================= */

// Simple Symbol Table to map "x" -> stack offset
typedef struct {
	char name[256];
	int offset; // e.g., -8, -16
} Symbol;

Symbol symbols[100];	// Fixed size for simplicity
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
	current_stack_offset -= 8;	// Grow stack down by 8 bytes (64-bit)
	strcpy(symbols[symbol_count].name, name);
	symbols[symbol_count].offset = current_stack_offset;
	symbol_count++;
}

void gen_asm(ASTNode *node) {
	if (!node) return;

	switch (node->type) {
		case NODE_INT:
			printf("  movq $%d, %%rax\n", node->int_value);
			printf("  pushq %%rax\n");
			break;
		case NODE_VAR_REF:
			int offset = get_offset(node->var_name);
			printf("  movq %d(%%rbp), %%rax\n", offset);
			printf("  pushq %%rax\n");
			break;
		case NODE_VAR_DECL:
			// Calculate the value (it's pushed to stack)
			gen_asm(node->left); 
			// Assign it a slot in our symbol table
			add_symbol(node->var_name);
			// Move it from stack to local variable slot
			printf("  popq %%rax\n");
			printf("  movq %%rax, %d(%%rbp)\n", get_offset(node->var_name));
			break;
		case NODE_ASSIGN:
			// Calculate the new value
			gen_asm(node->right);
			// Store it in the existing slot
			printf("  popq %%rax\n");
			printf("  movq %%rax, %d(%%rbp)\n", get_offset(node->var_name));
			break;
		case NODE_RETURN:
			gen_asm(node->left);					// value to return is now on stack
			printf("  popq %%rax\n");				// move to rax (return register)
			printf("  movq %%rbp, %%rsp\n");		// Restore stack pointer
			printf("  popq %%rbp\n");				// Restore base pointer
			printf("  ret\n");
			break;
		case NODE_BINOP:
			gen_asm(node->left);
			gen_asm(node->right);
			
			printf("  popq %%rbx\n");				// Right operand
			printf("  popq %%rax\n");				// Left operand
			
			if (node->op == '+') printf("  addq %%rbx, %%rax\n");
			if (node->op == '-') printf("  subq %%rbx, %%rax\n");
			if (node->op == '*') printf("  imulq %%rbx, %%rax\n");
			if (node->op == '/') {
				printf("  cqto\n");					// sign extend rax to rdx:rax
				printf("  idivq %%rbx\n");
			}
			
			printf("  pushq %%rax\n");
			break;
		case NODE_BLOCK:
			// Iterate through the linked list of statements
			ASTNode *stmt = node->left;
			while (stmt) {
				gen_asm(stmt);
				stmt = stmt->next;
			}
			break;
		case NODE_FUNCTION:
			printf(".global %s\n", node->var_name);
			printf("%s:\n", node->var_name);
			
			// Function Prologue
			printf("  pushq %%rbp\n");
			printf("  movq %%rsp, %%rbp\n");
			
			// Reserve space for variables
			// 256 bytes allows for 32 variables (8 bytes each).
			// This keeps the scratch stack (push/pop) safely below them.
			printf("  subq $256, %%rsp\n"); 
			
			gen_asm(node->body);
			
			// Function Epilogue (Safety catch)
			printf("  movq %%rbp, %%rsp\n");
			printf("  popq %%rbp\n");
			printf("  ret\n");
			break;
		case NODE_IF: {
			int label_else = new_label();
			int label_end = new_label();

			// 1. Condition
			gen_asm(node->left);
			printf("  popq %%rax\n");
			printf("  cmpq $0, %%rax\n");			// Is condition false (0)?
			printf("  je .L%d\n", label_else);		// Jump to Else if false

			// 2. If Body
			gen_asm(node->body);
			printf("  jmp .L%d\n", label_end);		// Skip Else

			// 3. Else Body
			printf(".L%d:\n", label_else);
			if (node->right) {
				gen_asm(node->right);
			}

			// 4. End
			printf(".L%d:\n", label_end);
			break;
		}
		case NODE_WHILE: {
			int label_start = new_label();
			int label_end = new_label();

			printf(".L%d:\n", label_start);			// Start of loop

			// 1. Condition
			gen_asm(node->left);
			printf("  popq %%rax\n");
			printf("  cmpq $0, %%rax\n");
			printf("  je .L%d\n", label_end);		// Exit if false

			// 2. Body
			gen_asm(node->body);
			printf("  jmp .L%d\n", label_start);	// Loop back

			printf(".L%d:\n", label_end);
			break;
		}
		// x86 comparison sets flags (EFLAGS). 'setX' instruction sets a byte register based on flags.
		case NODE_GT:
		case NODE_LT:
		case NODE_EQ:
		case NODE_NEQ:
			gen_asm(node->left);
			gen_asm(node->right);
			printf("  popq %%rbx\n");			// Right
			printf("  popq %%rax\n");			// Left
			printf("  cmpq %%rbx, %%rax\n");	// Compare Left - Right
			// Set AL to 1 if true, 0 if false
			if (node->type == NODE_EQ) printf("  sete %%al\n");
			if (node->type == NODE_NEQ) printf("  setne %%al\n");
			if (node->type == NODE_GT) printf("  setg %%al\n");
			if (node->type == NODE_LT) printf("  setl %%al\n");
			printf("  movzbq %%al, %%rax\n");	// Zero-extend byte to 64-bit
			printf("  pushq %%rax\n");
			break;
		case NODE_SYSCALL: {
			int arg_count = 0;
			ASTNode* arg = node->left;
			
			while (arg) {
				gen_asm(arg);	// Evaluates arg and pushes result to stack
				arg_count++;
				arg = arg->next;
			}
			
			const char* regs[] = {"rdi", "rsi", "rdx", "r10", "r8", "r9"};
			
			// We need to pop into the correct register.
			// Since stack has ArgN on top, we need to map:
			// Pop -> regs[arg_count - 1]
			// Pop -> regs[arg_count - 2]
			// ...
			
			// The first arg is actually the Syscall Number (rax) for Linux?
			// User syntax: syscall(60, 0) -> 60 is rax, 0 is rdi.
			// Let's assume the first argument in user code IS the syscall number.
			
			for (int i = arg_count - 1; i >= 0; i--) {
				if (i == 0) {
					printf("  popq %%rax\n"); // The syscall number
				} else {
					// Map argument i to register i-1 (since 0 is rax)
					if (i-1 < 6) {
						printf("  popq %%%s\n", regs[i-1]);
					}
				}
			}
			
			printf("  syscall\n");
			printf("  pushq %%rax\n"); // Push return value (result/error)
			break;
		}
		case NODE_POST_INC: {
            // Retrieve the variable name from the child node
            char* var_name = node->left->var_name;
            int offset = get_offset(var_name);

            printf("  movq %d(%%rbp), %%rax\n", offset);	// Load i
            printf("  incq %%rax\n");						// Increment i
            printf("  movq %%rax, %d(%%rbp)\n", offset);	// Store i back
            break;
        }
    	case NODE_STRING: {
    		int label = new_label();

    		// Switch to data section to store the bytes
    		printf("  .section .rodata\n");
    		printf(".LC%d: .string \"%s\"\n", label, node->var_name);

    		// Switch back to code section
    		printf("  .section .text\n");

    		// Load the ADDRESS of the string into rax
    		printf("  leaq .LC%d(%%rip), %%rax\n", label);
    		printf("  pushq %%rax\n");
    		break;
    	}
	}
}

/* ========================================================================= */
/* MAIN                                                                      */
/* ========================================================================= */

int main(int argc, char **argv)
{
	if (argc < 2) {
		printf("Usage: %s [options] <input_file>\n", argv[0]);
		printf("Options:\n");
		printf("  -o <file>  Specify output assembly file (default: out.s)\n");
		return 1;
	}

	char *input_filename = NULL;
	char *output_filename = "out.s";    // Default output

	// Parse Arguments
	for (int i = 1; i < argc; i++) {
		if (strcmp(argv[i], "-o") == 0) {
			if (i + 1 < argc) {
				output_filename = argv[i + 1];
				i++;    // Skip the filename
			} else {
				fprintf(stderr, "Error: -o requires a filename\n");
				return 1;
			}
		} else {
			input_filename = argv[i];
		}
	}

	if (!input_filename) {
		printf("Error: No input file specified\n");
		return 1;
	}

	// Read Input
	source_code = read_file(input_filename);
	
	// Prime the lexer
	advance(); 

	// Generate Output
	// Redirect stdout to the file so our existing printf statements work
	FILE *out_file = freopen(output_filename, "w", stdout);
	if (!out_file) {
		fprintf(stderr, "Error: Could not open output file %s\n", output_filename);
		free(source_code);
		return 1;
	}

	// Write the assembly header (required for linking)
	printf(".section .text\n");
	
	// Keep parsing until end of file
	while (current_token.type != TOKEN_EOF) {
		if (current_token.type == TOKEN_FN) {
			ASTNode* func = parse_function();
			gen_asm(func);
			free_ast(func);    // Free each function after generating code
		} else {
			// Skip imports or unknown top-level tokens for now
			advance();
		}
	}

	// Cleanup
	fclose(stdout); 
	free(source_code);

	return 0;
}