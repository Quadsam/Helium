#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <string.h>

/* ========================================================================= */
/* TOKENS                                                                    */
/* ========================================================================= */

typedef enum {
	TOKEN_EOF,
	TOKEN_INT,          // 123
	TOKEN_IDENTIFIER,   // x, main, count
	TOKEN_FN,           // fn
	TOKEN_INT_TYPE,     // int
	TOKEN_PTR_TYPE,     // ptr
	TOKEN_RETURN,       // return
	TOKEN_LPAREN,       // (
	TOKEN_RPAREN,       // )
	TOKEN_LBRACE,       // {
	TOKEN_RBRACE,       // }
	TOKEN_LBRACKET,     // [
	TOKEN_RBRACKET,     // ]
	TOKEN_COMMA,        // ,
	TOKEN_SEMI,         // ;
	TOKEN_COLON,        // :
	TOKEN_ASSIGN,       // =
	TOKEN_PLUS,         // +
	TOKEN_INC,          // ++
	TOKEN_MINUS,        // -
	TOKEN_STAR,         // *
	TOKEN_SLASH,        // /
	TOKEN_PIPE,			// |
	TOKEN_AMP,          // &
	TOKEN_EQ,           // ==
	TOKEN_NEQ,          // !=
	TOKEN_LT,           // <
	TOKEN_GT,           // >
	TOKEN_ARROW,        // ->
	TOKEN_IF,           // if
	TOKEN_ELSE,         // else
	TOKEN_WHILE,        // while
	TOKEN_SYSCALL,      // syscall
	TOKEN_STRING,       // "string"
} TokenType;

typedef struct {
	TokenType type;
	char name[256]; // To store "main", "count", "int", exc.
	int value;      // For integers
	int line;       // For error handling
	int column;     // For error handling
} Token;

Token current_token;

/* ========================================================================= */
/* GLOBAL STATE                                                              */
/* ========================================================================= */

char *source_code;
char *current_filename = "unknown"; // Default
int src_pos = 0;
int current_line = 1;
int current_col = 1;

// Simple Macro Table
typedef struct {
	char name[64];
	Token value;	// We store the token it represents
} Macro;

Macro macros[100];
int macro_count = 0;

void add_macro(char *name, Token value)
{
	strcpy(macros[macro_count].name, name);
	macros[macro_count].value = value;
	macro_count++;
}

Token *get_macro(char *name)
{
	for (int i = 0; i < macro_count; i++) {
		if (strcmp(macros[i].name, name) == 0)
			return &macros[i].value;
	}
	return NULL;
}

/* ========================================================================= */
/* ERROR HANDLING                                                            */
/* ========================================================================= */

void error_at(Token token, const char *message)
{
	fprintf(stderr, "%s:%d:%d: %s\n",
			current_filename, token.line, token.column, message);

	// Find the start of the line
	int line_start = src_pos;   // Start searching backwards from current pos

	int line = 1;
	int i = 0;
	while (line < token.line && source_code[i] != '\0') {
		if (source_code[i] == '\n') line++;
		i++;
	}
	line_start = i;

	// Find the end of the line
	int line_end = line_start;
	while (source_code[line_end] != '\n' && source_code[line_end] != '\0') {
		line_end++;
	}

	// Print the source line
	fprintf(stderr, "\t");  // Indent
	for (int j = line_start; j < line_end; j++) {
		fputc(source_code[j], stderr);
	}
	fprintf(stderr, "\n");

	// Print the caret (^) pointing to the column
	fprintf(stderr, "\t");  // Match indent
	for (int j = 1; j < token.column; j++) {
		fputc(' ', stderr);
	}
	fprintf(stderr, "^\n");

	exit(1);
}

// Wrapper for simple errors
void error(const char *message)
{
	error_at(current_token, message);
}

/* ========================================================================= */
/* PREPROCESSOR (Includes)                                                   */
/* ========================================================================= */

// Helper to append string to buffer, resizing if necessary
void append_string(char **buffer, int *length, int *capacity, const char *str)
{
	int str_len = strlen(str);
	if (*length + str_len >= *capacity) {
		*capacity = (*capacity == 0) ? 1024 : *capacity * 2;
		while (*length + str_len >= *capacity) *capacity *= 2;
		*buffer = realloc(*buffer, *capacity);
	}
	strcpy(*buffer + *length, str);
	*length += str_len;
}

// Recursive function to handle #include
char *preprocess_file(const char *filename)
{
	FILE *f = fopen(filename, "r");
	if (!f) {
		fprintf(stderr, "Error: Could not open file %s\n", filename);
		exit(1);
	}

	char *buffer = NULL;
	int length = 0;
	int capacity = 0;

	char line[1024];
	while (fgets(line, sizeof(line), f)) {
		// Check for #include "filename"
		// We look for: #, then include, then quote
		char *include_ptr = strstr(line, "#include");
		
		if (include_ptr) {
			// Found an include! Extract filename.
			char *start_quote = strchr(line, '"');
			char *end_quote = strrchr(line, '"');
			
			if (start_quote && end_quote && end_quote > start_quote) {
				// Terminate the string at the end quote
				*end_quote = '\0';
				char *included_filename = start_quote + 1;
				
				// Recursively read the included file
				char *included_content = preprocess_file(included_filename);
				
				// Append the content instead of the #include line
				append_string(&buffer, &length, &capacity, included_content);
				
				// Append a newline to be safe
				append_string(&buffer, &length, &capacity, "\n");
				
				free(included_content);
				continue;	// Skip appending the original #include line
			}
		}

		// Normal line, just append it
		append_string(&buffer, &length, &capacity, line);
	}

	fclose(f);
	return buffer;
}

/* ========================================================================= */
/* LEXER                                                                     */
/* ========================================================================= */

Token get_next_token()
{
	// Skip Whitespace and count lines
	while (source_code[src_pos] != '\0' && isspace(source_code[src_pos])) {
		if (source_code[src_pos] == '\n') {
			current_line++;
			current_col = 1;
		} else {
			current_col++;
		}
		src_pos++;
	}

	// Capture the start location of this token
	int start_line = current_line;
	int start_col = current_col;

	if (source_code[src_pos] == '\0') {
		return (Token){TOKEN_EOF, "EOF", 0, start_line, start_col};
	}

	char current = source_code[src_pos];

	// Handle Identifiers/Keywords (Allow _ at start)
	if (isalpha(current) || current == '_') {
		Token t;
		t.line = start_line;
		t.column = start_col;
		int i = 0;
		// Allow alphanumeric OR underscore in the body
		while (isalnum(source_code[src_pos]) || source_code[src_pos] == '_') {
			t.name[i++] = source_code[src_pos++];
			current_col++; 
		}
		t.name[i] = '\0';

		if (strcmp(t.name, "fn")            == 0) t.type = TOKEN_FN;
		else if (strcmp(t.name, "int")      == 0) t.type = TOKEN_INT_TYPE;
		else if (strcmp(t.name, "ptr")      == 0) t.type = TOKEN_PTR_TYPE;
		else if (strcmp(t.name, "return")   == 0) t.type = TOKEN_RETURN;
		else if (strcmp(t.name, "if")       == 0) t.type = TOKEN_IF;
		else if (strcmp(t.name, "else")     == 0) t.type = TOKEN_ELSE;
		else if (strcmp(t.name, "while")    == 0) t.type = TOKEN_WHILE;
		else if (strcmp(t.name, "syscall")  == 0) t.type = TOKEN_SYSCALL;
		else t.type = TOKEN_IDENTIFIER;

		// CHECK MACROS
		Token *macro = get_macro(t.name);
		if (macro) {
			// Return the stored token (e.g., the INT 100)
			// But preserve the current line/col info so errors point to the usage
			Token subst = *macro;
			subst.line = start_line;
			subst.column = start_col;
			return subst;
		}

		return t;
	}

	// Handle Integers
	if (isdigit(current)) {
		Token t;
		t.type = TOKEN_INT;
		t.line = start_line;
		t.column = start_col;
		t.value = 0;
		while (isdigit(source_code[src_pos])) {
			t.value = t.value * 10 + (source_code[src_pos++] - '0');
			current_col++;
		}
		return t;
	}

	// Handle Symbols
	// We increment src_pos and current_col for single chars
	// For multi-chars (e.g., // or ++), we handle inside.

	switch (current) {
		// Single char tokens
		case '(': src_pos++; current_col++; return (Token){TOKEN_LPAREN, "(", 0, start_line, start_col};
		case ')': src_pos++; current_col++; return (Token){TOKEN_RPAREN, ")", 0, start_line, start_col};
		case '{': src_pos++; current_col++; return (Token){TOKEN_LBRACE, "{", 0, start_line, start_col};
		case '}': src_pos++; current_col++; return (Token){TOKEN_RBRACE, "}", 0, start_line, start_col};
		case '[': src_pos++; current_col++; return (Token){TOKEN_LBRACKET, "[", 0, start_line, start_col};
		case ']': src_pos++; current_col++; return (Token){TOKEN_RBRACKET, "]", 0, start_line, start_col};
		case ',': src_pos++; current_col++; return (Token){TOKEN_COMMA, ",", 0, start_line, start_col};
		case ';': src_pos++; current_col++; return (Token){TOKEN_SEMI, ";", 0, start_line, start_col};
		case ':': src_pos++; current_col++; return (Token){TOKEN_COLON, ":", 0, start_line, start_col};
		case '*': src_pos++; current_col++; return (Token){TOKEN_STAR, "*", 0, start_line, start_col};
		case '|': src_pos++; current_col++; return (Token){TOKEN_PIPE, "|", 0, start_line, start_col};
		case '&': src_pos++; current_col++; return (Token){TOKEN_AMP, "&", 0, start_line, start_col};
		// Division or Comment
		case '/': 
			if (source_code[src_pos + 1] == '/') {
				// Comment: Skip until newline
				// Note: We don't change line number here; the next loop of get_next_token will handle the \n
				while (source_code[src_pos] != '\0' && source_code[src_pos] != '\n') {
					src_pos++;
					// Col updates aren't strictly necessary inside comments, but good practice
					current_col++; 
				}
				return get_next_token();    // Recursion to find real token
			}
			src_pos++; current_col++; 
			return (Token){TOKEN_SLASH, "/", 0, start_line, start_col};
		case '-': 
			if (source_code[src_pos+1] == '>') {
				src_pos+=2; current_col+=2; 
				return (Token){TOKEN_ARROW, "->", 0, start_line, start_col};
			}
			src_pos++; current_col++; 
			return (Token){TOKEN_MINUS, "-", 0, start_line, start_col};
		case '+': 
			if (source_code[src_pos+1] == '+') {
				src_pos+=2; current_col+=2; 
				return (Token){TOKEN_INC, "++", 0, start_line, start_col};
			}
			src_pos++; current_col++;
			return (Token){TOKEN_PLUS, "+", 0, start_line, start_col};

		case '=':
			if (source_code[src_pos+1] == '=') {
				src_pos+=2; current_col+=2; 
				return (Token){TOKEN_EQ, "==", 0, start_line, start_col};
			}
			src_pos++; current_col++;
			return (Token){TOKEN_ASSIGN, "=", 0, start_line, start_col};

		case '!':
			if (source_code[src_pos+1] == '=') {
				src_pos+=2; current_col+=2; 
				return (Token){TOKEN_NEQ, "!=", 0, start_line, start_col};
			}
			error_at((Token){0, "", 0, start_line, start_col}, "Expected '!='");
			exit(1);
		case '<': src_pos++; current_col++; return (Token){TOKEN_LT, "<", 0, start_line, start_col};
		case '>': src_pos++; current_col++; return (Token){TOKEN_GT, ">", 0, start_line, start_col};
		case '"': {
			Token t;
			t.type = TOKEN_STRING;
			t.line = start_line;
			t.column = start_col;

			src_pos++; current_col++;   // Skip opening "
			int i = 0;
			while (source_code[src_pos] != '"' && source_code[src_pos] != '\0') {
				if (source_code[src_pos] == '\\' && source_code[src_pos+1] == 'n') {
					t.name[i++] = '\\'; t.name[i++] = 'n';
					src_pos += 2; current_col += 2;
				} else {
					t.name[i++] = source_code[src_pos++];
					current_col++;
				}
			}
			t.name[i] = '\0';
			if (source_code[src_pos] == '"') {
				src_pos++; current_col++;   // Skip closing "
			}
			return t;
		}
		case '#': {
			// Handle #define
			// We assume #include was handled by the preprocessor pass already.
			src_pos++; // Skip '#'
			
			// Read "define"
			while (source_code[src_pos] != '\0' && isspace(source_code[src_pos])) src_pos++;
			
			// Check if it's "define"
			if (strncmp(&source_code[src_pos], "define", 6) == 0) {
				src_pos += 6;
				
				// Get Macro Name
				while (source_code[src_pos] != '\0' && isspace(source_code[src_pos])) src_pos++;
				
				char name[64];
				int i = 0;
				while (isalnum(source_code[src_pos]) || source_code[src_pos] == '_') {
					name[i++] = source_code[src_pos++];
				}
				name[i] = '\0';
				
				// Get Macro Value (We cheat and use get_next_token recursively!)
				// This allows us to parse integers, strings, etc.
				Token value = get_next_token();
				
				// FIX: Check for negative numbers (#define NEG -1)
                // If we see a minus, grab the next token and merge them if it's an int.
                if (value.type == TOKEN_MINUS) {
                    Token next = get_next_token();
                    if (next.type == TOKEN_INT) {
                        value.type = TOKEN_INT;
                        value.value = -next.value; // Negate the value
                    } else {
                        // If it's not an integer (e.g. #define NEG -x), 
                        // our simple 1-token macro system can't handle it yet.
                        error_at((Token){0,"",0,current_line,current_col}, "Macros must be single tokens or negative integers");
                    }
                }

				// Store it
				add_macro(name, value);
				
				// Recursively return the NEXT token (skip the define line)
				return get_next_token();
			}
			
			// If it's NOT define (maybe an include left over?), ignore line
			 while (source_code[src_pos] != '\0' && source_code[src_pos] != '\n') {
				src_pos++;
			}
			return get_next_token();
		}
		default: 
			error_at((Token){0, "", 0, start_line, start_col}, "Unknown character");
			exit(1);
	}
}

void advance()
{
	current_token = get_next_token();
}

/* ========================================================================= */
/* AST                                                                       */
/* ========================================================================= */

typedef enum {
	NODE_INT,           // Integer literal
	NODE_VAR_REF,       // x (usage of a variable)
	NODE_BINOP,         // Math (+, -, *, /)
	NODE_ASSIGN,        // x = ...;
	NODE_VAR_DECL,      // int x = ...;
	NODE_RETURN,        // return x;
	NODE_BLOCK,         // { ... }
	NODE_FUNCTION,      // Function definition
	NODE_IF,            // if ...
	NODE_WHILE,         // while ...
	NODE_GT,            // >
	NODE_LT,            // <
	NODE_EQ,            // ==
	NODE_NEQ,           // !=
	NODE_SYSCALL,       // syscall()
	NODE_POST_INC,      // i++
	NODE_STRING,        // "string"
	NODE_ARRAY_DECL,    // int x[10];
	NODE_ARRAY_ACCESS,  // x[i]
	NODE_FUNC_CALL,     // add(1, 2);
	NODE_ADDR,          // &x (Address of)
	NODE_DEREF,         // *x (Dereference)
} NodeType;

typedef struct ASTNode {
	NodeType type;
	int int_value;          // For literals
	char *var_name;         // For references/declarations
	char op;                // For binary ops
	struct ASTNode *left;   // Left child
	struct ASTNode *right;  // Right child
	struct ASTNode *body;   // For functions
	struct ASTNode *next;   // For linked lists in blocks
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
ASTNode *parse_bitwise();

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
			else_body = parse_if(); // Recursion for 'else if'
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
	advance();  // Skip 'while'

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

		// Function Call: add(1, 2)
		if (current_token.type == TOKEN_LPAREN) {
			advance();  // consume '('

			ASTNode *call_node = create_node(NODE_FUNC_CALL);
			call_node->var_name = node->var_name; // Reuse name

			// Parse Arguments
			ASTNode *current_arg = NULL;
			while (current_token.type != TOKEN_RPAREN) {
				ASTNode *expr = parse_expression();

				if (call_node->left == NULL) {
					call_node->left = expr;
					current_arg = expr;
				} else {
					current_arg->next = expr;
					current_arg = expr;
				}

				if (current_token.type == TOKEN_COMMA) advance();
				else if (current_token.type != TOKEN_RPAREN) error("Expected ',' or ')'");
			}
			advance();  // consume ')'

			free(node); // Free the original var_ref shell
			return call_node;
		}

		// Array Access: x[i]
		if (current_token.type == TOKEN_LBRACKET) {
			advance();  // consume '['
			ASTNode *index = parse_expression();
			if (current_token.type != TOKEN_RBRACKET) error("Expected ']'");
			advance();  // consume ']'

			// Convert to ARRAY_ACCESS node
			ASTNode *array_node = create_node(NODE_ARRAY_ACCESS);
			array_node->var_name = node->var_name;
			array_node->left = index; 

			free(node); // Cleanup
			return array_node;
		}

		// Post-Increment: i++
		if (current_token.type == TOKEN_INC) {
			advance();
			ASTNode* inc_node = create_node(NODE_POST_INC);
			inc_node->left = node; 
			return inc_node;
		}

		return node;
	}

	// Handle Parentheses (Expression grouping)
	if (current_token.type == TOKEN_LPAREN) {
		advance();
		ASTNode *node = parse_expression();
		if (current_token.type != TOKEN_RPAREN) {
			error("Syntax Error: Expected ')'");
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
		node->var_name = strdup(current_token.name);    // Store the string content
		advance();
		return node;
	}

	error("Syntax Error: Unexpected token in factor");
	exit(1);
}

ASTNode *parse_unary()
{
	// Address Of (&x)
	if (current_token.type == TOKEN_AMP) {
		advance();
		ASTNode *node = create_node(NODE_ADDR);
		node->left = parse_unary();         // Recursive to handle &*x
		return node;
	}

	// Dereference (*x)
	if (current_token.type == TOKEN_STAR) {
		advance();
		ASTNode *node = create_node(NODE_DEREF);
		node->left = parse_unary();         // Recursive to handle **x
		return node;
	}

	// Negative Numbers (-5)
	if (current_token.type == TOKEN_MINUS) {
		advance();
		ASTNode *node = create_node(NODE_BINOP);
		node->op = '-';
		node->left = create_node(NODE_INT); // Implicit 0 - x
		node->left->int_value = 0;
		node->right = parse_unary();
		return node;
	}

	return parse_factor();
}

// Term: handles * and /
ASTNode *parse_term()
{
	ASTNode *node = parse_unary();
	while (current_token.type == TOKEN_STAR || current_token.type == TOKEN_SLASH) {
		ASTNode *newNode = create_node(NODE_BINOP);
		newNode->op = (current_token.type == TOKEN_STAR) ? '*' : '/';
		newNode->left = node;
		advance();
		newNode->right = parse_unary();
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

ASTNode *parse_bitwise() {
	// Parse the left side (Math: +, -)
	ASTNode *node = parse_math();

	// Look for & or |
	while (current_token.type == TOKEN_AMP || current_token.type == TOKEN_PIPE) {
		char op = (current_token.type == TOKEN_AMP) ? '&' : '|';

		// We reuse NODE_BINOP for convenience
		ASTNode *newNode = create_node(NODE_BINOP);
		newNode->op = op;
		newNode->left = node;

		advance();

		// 3. Parse the right side
		newNode->right = parse_math();

		node = newNode;
	}
	return node;
}

ASTNode *parse_comparison()
{
	ASTNode *node = parse_bitwise();

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
		newNode->right = parse_bitwise();
		node = newNode;
	}
	return node;
}

ASTNode *parse_expression()
{
	ASTNode* lhs = parse_comparison();

	// Check for Assignment
	if (current_token.type == TOKEN_ASSIGN) {
		advance(); // consume '='

		// Check valid L-value: Var, Array, or DEREF
		if (lhs->type != NODE_VAR_REF && lhs->type != NODE_ARRAY_ACCESS && lhs->type != NODE_DEREF)
			error("Syntax Error: Invalid l-value.");

		ASTNode *assign = create_node(NODE_ASSIGN);

		// Handle Pointer Assignment (*ptr = val)
		if (lhs->type == NODE_DEREF) {
			assign->left = lhs;
			// DEREF nodes don't have names, but we might need a dummy one to avoid segfaults 
			// if other code checks var_name. Ideally we check type first.
			assign->var_name = NULL;
		} else if (lhs->type == NODE_ARRAY_ACCESS) {
			assign->left = lhs; 
			// FIX: Use strdup to create a COPY of the string.
			// Now both nodes own their own string memory.
			assign->var_name = strdup(lhs->var_name); 
		} else {
			// For normal variables, we destroy the old node (lhs),
			// so we can just steal its pointer safely.
			assign->var_name = lhs->var_name;
			assign->left = NULL; 
			free(lhs); // Free the shell (struct only)
		}

		assign->right = parse_expression();
		return assign;
	}
	return lhs;
}

// Note: We check specifically for 'int', 'char', or 'ptr' types here
ASTNode *parse_var_declaration()
{
	advance();  // Consume 'int'

	if (current_token.type != TOKEN_IDENTIFIER) error("Expected variable name");
	char *name = strdup(current_token.name);
	advance();

	// Check for Array Declaration: int x[10];
	if (current_token.type == TOKEN_LBRACKET) {
		advance();  // consume '['

		if (current_token.type != TOKEN_INT) error("Array size must be an integer literal");
		int size = current_token.value;
		advance();  // consume size

		if (current_token.type != TOKEN_RBRACKET) error("Expected ']'");
		advance();  // consume ']'

		if (current_token.type != TOKEN_SEMI) error("Expected ';'");
		advance();  // consume ';'

		ASTNode *node = create_node(NODE_ARRAY_DECL);
		node->var_name = name;
		node->int_value = size;
		return node;
	}

	// Normal Variable Declaration: int x = 5;
	if (current_token.type != TOKEN_ASSIGN) error("Expected '=' or '['");
	advance(); // consume '='

	ASTNode *node = create_node(NODE_VAR_DECL);
	node->var_name = name;
	node->left = parse_expression();

	if (current_token.type != TOKEN_SEMI) error("Expected ';' after declaration");
	advance(); // consume ';'
	return node;
}


ASTNode *parse_statement()
{
	// Return Statement
	if (current_token.type == TOKEN_RETURN) {
		advance();
		ASTNode *node = create_node(NODE_RETURN);
		node->left = parse_expression();
		if (current_token.type != TOKEN_SEMI) {
			error("Error: Expected ';' after return");
		}
		advance();
		return node;
	}

	// Variable Declaration (int x... OR ptr x...)
	if (current_token.type == TOKEN_INT_TYPE || current_token.type == TOKEN_PTR_TYPE) {
		return parse_var_declaration(); 
	}

	if (current_token.type == TOKEN_IF) return parse_if();
	if (current_token.type == TOKEN_WHILE) return parse_while();


	// Default: Expression statement (x = 5;)
	ASTNode *node = parse_expression();
	if (current_token.type != TOKEN_SEMI)
		error("Error: Expected ';' after expression");

	advance();
	return node;
}

ASTNode *parse_block()
{
	// Ensure we are strictly looking at a '{'
	if (current_token.type != TOKEN_LBRACE)
		error("Syntax Error: Expected '{' at start of block");

	advance();  // Consume '{'

	ASTNode *block = create_node(NODE_BLOCK);
	ASTNode *curr = NULL;

	// Keep parsing statements untill we hit '}' or EOF
	while (current_token.type != TOKEN_RBRACE && current_token.type != TOKEN_EOF) {
		ASTNode *stmt = parse_statement();
		if (!block->left) {
			block->left = stmt; // Head of list
			curr = stmt;
		} else {
			curr->next = stmt;  // Append to list
			curr = stmt;
		}
	}

	if (current_token.type != TOKEN_RBRACE) {
		error("Error: Expected '}'");
	}
	advance();  // Consume '}'
	return block;
}

ASTNode *parse_function()
{
	if (current_token.type != TOKEN_FN) return NULL;
	advance();  // consume 'fn'

	char *name = strdup(current_token.name);
	advance();  // consume name

	if (current_token.type != TOKEN_LPAREN) error("Expected '('");
	advance();  // consume '('

	// Parse Parameters (a: int, b: int)
	ASTNode *first_param = NULL;
	ASTNode *current_param = NULL;

	while (current_token.type != TOKEN_RPAREN) {
		if (current_token.type != TOKEN_IDENTIFIER) error("Expected parameter name");

		// Capture Parameter Name
		ASTNode *param = create_node(NODE_VAR_DECL);
		param->var_name = strdup(current_token.name);
		param->left = NULL; 
		advance(); 

		// Expect Colon
		if (current_token.type != TOKEN_COLON) error("Expected ':' after parameter name");
		advance();

		// Expect Type (int OR ptr)
		if (current_token.type != TOKEN_INT_TYPE && current_token.type != TOKEN_PTR_TYPE)
			error("Expected parameter type 'int' or 'ptr'");
		advance();

		// Link the parameter node
		if (first_param == NULL) {
			first_param = param;
			current_param = param;
		} else {
			current_param->next = param;
			current_param = param;
		}

		if (current_token.type == TOKEN_COMMA) {
			advance();
		} else if (current_token.type != TOKEN_RPAREN) {
			error("Expected ',' or ')'");
		}
	}

	advance(); // consume ')'

	// Parse Return Type (-> int)
	if (current_token.type == TOKEN_ARROW) {
		advance(); // consume '->'
		if (current_token.type != TOKEN_INT_TYPE && current_token.type != TOKEN_PTR_TYPE)
			error("Expected return type 'int' or 'ptr'");
		advance(); // consume 'int'
	}

	ASTNode *func = create_node(NODE_FUNCTION);
	func->var_name = name;
	func->left = first_param; 
	func->body = parse_block();

	return func;
}

ASTNode *parse_syscall()
{
	advance();  // Skip 'syscall'

	if (current_token.type != TOKEN_LPAREN)
		error("Error: Expected '(' after syscall");

	advance();  // Skip '('

	ASTNode *call_node = create_node(NODE_SYSCALL);
	ASTNode *current_arg = NULL;

	// Parse arguments (comma separated)
	while (current_token.type != TOKEN_RPAREN) {
		ASTNode *expr = parse_expression();

		if (call_node->left == NULL) {
			call_node->left = expr;     // First arg
			current_arg = expr;
		} else {
			current_arg->next = expr;   // Chain subsequent args
			current_arg = expr;
		}

		if (current_token.type == TOKEN_COMMA) {
			advance();  // Skip ','
		} else if (current_token.type != TOKEN_RPAREN) {
			error("Error: Expected ',' or ')'");
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

Symbol symbols[100];    // Fixed size for simplicity
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
			printf("  sub rsp, 4096\n"); // Reserve stack space

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
			current_filename = input_filename;
		}
	}

	if (!input_filename) {
		fprintf(stderr, "Error: No input file specified\n");
		return 1;
	}

	// Read Input
	source_code = preprocess_file(input_filename);

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
	printf("section .text\n");

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
