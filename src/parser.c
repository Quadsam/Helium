#include "helium.h"

/* ========================================================================= */
/* AST																		 */
/* ========================================================================= */

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
/* PARSER																	 */
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

	// Handle Characters ('a')
	if (current_token.type == TOKEN_CHAR) {
		ASTNode *node = create_node(NODE_INT); // We treat chars as ints internally
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
	if (current_token.type != TOKEN_INT_TYPE &&
		current_token.type != TOKEN_PTR_TYPE &&
		current_token.type != TOKEN_CHAR_TYPE) {
		error("Expected type specifier");
	}
	advance();	// Consume type (int/ptr/char)

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
	if (current_token.type == TOKEN_INT_TYPE ||
		current_token.type == TOKEN_PTR_TYPE ||
		current_token.type == TOKEN_CHAR_TYPE) {
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

		// Expect Type (int OR ptr OR char)
		if (current_token.type != TOKEN_INT_TYPE &&
			current_token.type != TOKEN_PTR_TYPE &&
			current_token.type != TOKEN_CHAR_TYPE) {
			error("Expected parameter type 'int' or 'ptr'");
		}
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
		if (current_token.type != TOKEN_INT_TYPE &&
			current_token.type != TOKEN_PTR_TYPE &&
			current_token.type != TOKEN_CHAR_TYPE) {
			error("Expected return type 'int' or 'ptr'");
		}
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