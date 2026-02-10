#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <string.h>

/* --- Lexer Logic --- */
typedef enum {
	TOKEN_INT,
	TOKEN_PLUS,
	TOKEN_MINUS,
	TOKEN_STAR,
	TOKEN_SLASH,
	TOKEN_EOF,
	TOKEN_ERROR,
	TOKEN_RPAREN,
	TOKEN_ARROW,
	TOKEN_FN,
	TOKEN_INT_TYPE,
	TOKEN_IDENTIFIER,
	TOKEN_LBRACE,
	TOKEN_RBRACE
} TokenType;

typedef struct {
	TokenType type;
	int value;
	char lexeme;	// For single chars like '+'
	char name[256];	// To store "main", "count", "int", exc.
} Token;

const char *source;
int pos = 0;
Token current_token;

Token get_next_token()
{
	// Skip whilespace
	while (isspace(source[pos])) pos++;

	if (isalpha(source[pos])) {
		Token t;
		int i = 0;
		while (isalnum(source[pos])) t.name[i++] = source[pos++];
		t.name[i] = '\0';

		// Check if the identifier is actually a keyword
		if (strcmp(t.name, "fn") == 0) {
			t.type = TOKEN_FN;
		} else if (strcmp(t.name, "int") == 0) {
			t.type = TOKEN_INT_TYPE;
		} else {
			t.type = TOKEN_IDENTIFIER;
		}
		return t;
	}

	if (source[pos] == '\0') return (Token){TOKEN_EOF, 0, 0, "\0"};

	if (isdigit(source[pos])) {
		int val = 0;
		while (isdigit(source[pos])) {
			val = val * 10 + (source[pos] - '0');
			pos++;
		}
		return (Token){TOKEN_INT, val, 0, "int"};
	}

	// Handle operators
	char op = source[pos++];
	switch (op) {
		case '+': return (Token){TOKEN_PLUS,  0, '+', "Add"};
		case '-': return (Token){TOKEN_MINUS, 0, '-', "Subtract"};
		case '*': return (Token){TOKEN_STAR,  0, '*', "Multiply"};
		case '/': return (Token){TOKEN_SLASH, 0, '/', "Divide"};
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
	NODE_BINOP, // +, -, *, /
	NODE_FUNCTION,
	NODE_BLOCK,
	NODE_RETURN
} NodeType;

typedef struct ASTNode {
	NodeType type;

	// For Math (Binary Ops)
	int int_value;          // For NODE_INT
	char op;                // For NODE_BINOP
	struct ASTNode *left;   // Left child
	struct ASTNode *right;  // Right child

	// For Functions
	char *func_name;		// Name of the function
	struct ASTNode *body;	// The block of code beloging to the function

	// For Blocks (Linked List of Statements)
	struct ASTNode *list;	// Points to the first statement in the block
	struct ASTNode *next;	// Points to the next statement in the chain
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

ASTNode* create_function_node(char *name, ASTNode *body)
{
	ASTNode *node = malloc(sizeof(ASTNode));
	if (!node) {
		fprintf(stderr, "Memory allocation failed\n");
		exit(1);
	}

	node->type = NODE_FUNCTION;
	node->func_name = strdup(name);	// Make a copy of the name
	node->body = body;

	// Initialize unused fields to keep things clean
	node->left = node->right = node->list = node->next = NULL;

	return node;
}

ASTNode *parse_statement();

ASTNode *create_block_node(ASTNode *statement_list)
{
	ASTNode *node = malloc(sizeof(ASTNode));
	node->type = NODE_BLOCK;
	node->list = statement_list;
	node->left = node->right = node->next = node->body = NULL;
	return node;
}

ASTNode *parse_block()
{
	// Ensure we are strictly looking at a '{'
	if (current_token.type != TOKEN_LBRACE) {
		fprintf(stderr, "Syntax Error: Expected '{' at start of block\n");
		exit(1);
	}
	advance();	// Consume '{'

	ASTNode *head = NULL;
	ASTNode *current = NULL;

	// Keep parsing statements untill we hit '}' or EOF
	while (current_token.type != TOKEN_RBRACE && current_token.type != TOKEN_EOF) {
		ASTNode* stmt = parse_statement();

        if (stmt) {
            if (head == NULL) {
                head = stmt;
                current = stmt;
            } else {
                current->next = stmt;
                current = stmt;
            }
        }
	}

	if (current_token.type != TOKEN_RBRACE) {
		fprintf(stderr, "Syntax Error: Expected '}' at end of block\n");
		exit(1);
	}
	advance();	// Consume '}'

	return create_block_node(head);
}

ASTNode *parse_function()
{
	// current_token is 'fn'
    advance();

    // Now current_token is the IDENTIFIER (e.g., main)
    char *func_name = strdup(current_token.name);
    advance(); // Skip '('
    
    // Parse parameters: name : type
    while (current_token.type != TOKEN_RPAREN) {
        // ... logic for parameters ...
    }
    
    if (current_token.type == TOKEN_ARROW) { // '->'
        advance();
        // Parse return type
    }

    // Now parse the block { ... }
    ASTNode* body = parse_block();

    return create_function_node(func_name, body);
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
