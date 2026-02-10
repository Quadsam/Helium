#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>

/* --- Lexer Logic --- */
typedef enum {
	TOKEN_INT,
	TOKEN_PLUS,
	TOKEN_MINUS,
	TOKEN_STAR,
	TOKEN_SLASH,
	TOKEN_EOF,
	TOKEN_ERROR
} TokenType;

typedef struct {
	TokenType type;
	int value;
	char lexeme;
} Token;

const char *source;
int pos = 0;
Token current_token;

Token get_next_token()
{
	// Skip whilespace
	while (isspace(source[pos])) pos++;

	if (source[pos] == '\0') return (Token){TOKEN_EOF, 0, 0};

	if (isdigit(source[pos])) {
		int val = 0;
		while (isdigit(source[pos])) {
			val = val * 10 + (source[pos] - '0');
			pos++;
		}
		return (Token){TOKEN_INT, val, 0};
	}

	// Handle operators
	char op = source[pos++];
	switch (op) {
		case '+': return (Token){TOKEN_PLUS,  0, '+'};
		case '-': return (Token){TOKEN_MINUS, 0, '-'};
		case '*': return (Token){TOKEN_STAR,  0, '*'};
		case '/': return (Token){TOKEN_SLASH, 0, '/'};
		default:  exit(1); // Basic error handling
	}
}

void advance()
{
	current_token = get_next_token();
}

/* --- Parser & AST Logic --- */
typedef enum {
	NODE_INT,
	NODE_BINOP // +, -, *, /
} NodeType;

typedef struct ASTNode {
	NodeType type;
	int int_value;          // For NODE_INT
	char op;                // For NODE_BINOP
	struct ASTNode *left;   // Left child
	struct ASTNode *right;  // Right child
} ASTNode;

ASTNode *create_node(NodeType type)
{
	ASTNode *node = malloc(sizeof(ASTNode));
	node->type = type;
	node->left = node->right = NULL;
	return node;
}

// Forward declarations for recursive descent
ASTNode* parse_expression();
ASTNode* parse_term();
ASTNode* parse_factor();

// Factor: handles integers
ASTNode *parse_factor(void)
{
	if (current_token.type == TOKEN_INT) {
		ASTNode *node = create_node(NODE_INT);
		node->int_value = current_token.value;
		advance();
		return node;
	}
	return NULL;
}

// Term: handles * and /
ASTNode *parse_term(void)
{
	ASTNode *node = parse_factor();

	while (current_token.type == TOKEN_STAR || current_token.type == TOKEN_SLASH) {
		ASTNode * newNode = create_node(NODE_BINOP);
		newNode->op = current_token.lexeme;
		newNode->left = node;
		advance();
		newNode->right = parse_factor();
		node = newNode;
	}
	return node;
}

// Expression: handles + and -
ASTNode *parse_expression()
{
	ASTNode *node = parse_term();

	while (current_token.type == TOKEN_PLUS || current_token.type == TOKEN_MINUS) {
		ASTNode* newNode = create_node(NODE_BINOP);
		newNode->op = current_token.lexeme;
		newNode->left = node;
		advance();
		newNode->right = parse_term();
		node = newNode;
	}
	return node;
}

/* --- Evaluation logic --- */
int evaluate(ASTNode *node)
{
	if (node->type == NODE_INT) return node->int_value;
	int left = evaluate(node->left);
	int right = evaluate(node->right);
	if (node->op == '+') return left + right;
	if (node->op == '-') return left - right;
	if (node->op == '*') return left * right;
	if (node->op == '/') return left / right;
	return 0;
}

void free_ast(ASTNode *node)
{
	if (node == NULL) return;

	free_ast(node->left);
	free_ast(node->right);

	free(node);
}

void generate_asm(ASTNode* node)
{
	if (node->type == NODE_INT) {
		printf("    pushq   $%d\n", node->int_value);
		return;
	}

	// Recurse down
	generate_asm(node->left);
	generate_asm(node->right);

	// Pop the values into registers
	// Note: The right child is on top of the stack
	printf("    popq    %%rdx\n"); // Right operand
	printf("    popq    %%rax\n"); // Left operand

	switch (node->op) {
		case '+':
			printf("    addq    %%rdx, %%rax\n");
			break;
		case '-':
			printf("    subq    %%rdx, %%rax\n");
			break;
		case '*':
			printf("    imulq   %%rdx, %%rax\n");
			break;
		case '/':
			printf("    cqto\n");      // Sign-extend rax into rdx:rax
			printf("    idivq   %%rdx\n");
			break;
	}

	// Push the result back to the stack
	printf("    pushq   %%rax\n");
}

void compile(ASTNode* root)
{
    // Assembly Boilerplate
    printf(".section .text\n");
    printf(".global main\n");
    printf("main:\n");

    // Generate the tree logic
    generate_asm(root);

    // The final result is on the stack; pop it into rax to return it
    printf("    popq    %%rax\n");
    printf("    ret\n");
}

/* --- Main Entry Point --- */
int main()
{
	source = "10 + 2 * 5"; // Example input
	advance(); // Prime the first token
	
	ASTNode* root = parse_expression();
	
	// printf("Source: %s\n", source);
	// printf("Result: %d\n", evaluate(root));
	compile(root);

	free_ast(root);
	
	return 0;
}