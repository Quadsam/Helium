#include "helium.h"

/* ========================================================================= */
/* AST                                                                       */
/* ========================================================================= */

// Helper to make a new node
ASTNode *create_node(NodeType type)
{
	ASTNode *node = malloc(sizeof(ASTNode));
	node->type = type;
	node->left = node->right = node->body = node->next = NULL;
	node->var_name = NULL;
	node->member_name = NULL; // Initialize new field
	return node;
}

/* ========================================================================= */
/* PARSER                                                                    */
/* ========================================================================= */

ASTNode *parse_expression(void);
ASTNode *parse_block(void);
ASTNode *parse_syscall(void);
ASTNode *parse_struct_definition(void);

ASTNode *parse_if(void)
{
	advance(); // Skip 'if'

	ASTNode *condition = parse_expression();
	ASTNode *if_body = parse_block();

	ASTNode *else_body = NULL;
	if (current_token.type == TOKEN_ELSE) {
		advance();
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

ASTNode *parse_while(void)
{
	advance();  // Skip 'while'

	ASTNode *condition = parse_expression();
	ASTNode *body = parse_block();

	ASTNode *node = create_node(NODE_WHILE);
	node->left = condition;
	node->body = body;
	return node;
}

// Factor: handles integers, variables, access
ASTNode *parse_factor(void)
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
		ASTNode *node = create_node(NODE_INT);
		node->int_value = current_token.value;
		advance();
		return node;
	}

	// Handle Variables & Member Access
	if (current_token.type == TOKEN_IDENTIFIER) {
		ASTNode *node = create_node(NODE_VAR_REF);
		node->var_name = strdup(current_token.name);
		advance();

		// 1. Check for Member Access: p.x
		if (current_token.type == TOKEN_PERIOD) {
			advance(); // Consume '.'
			if (current_token.type != TOKEN_IDENTIFIER) error("Expected member name");

			ASTNode *access = create_node(NODE_MEMBER_ACCESS);
			access->left = node; // The 'p'
			access->member_name = strdup(current_token.name); // The 'x'
			advance();

			// Allow chaining? (p.x.y) - Not for V1 (structs can't contain structs yet)
			return access;
		}

		// 2. Check for Function Call: add(1, 2)
		if (current_token.type == TOKEN_LPAREN) {
			advance();  // consume '('

			ASTNode *call_node = create_node(NODE_FUNC_CALL);
			call_node->var_name = node->var_name; // Reuse name

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

		// 3. Check for Array Access: x[i]
		if (current_token.type == TOKEN_LBRACKET) {
			advance();  // consume '['
			ASTNode *index = parse_expression();
			if (current_token.type != TOKEN_RBRACKET) error("Expected ']'");
			advance();  // consume ']'

			ASTNode *array_node = create_node(NODE_ARRAY_ACCESS);
			array_node->var_name = node->var_name;
			array_node->left = index;

			free(node);
			return array_node;
		}

		// 4. Post-Increment: i++
		if (current_token.type == TOKEN_INC) {
			advance();
			ASTNode* inc_node = create_node(NODE_POST_INC);
			inc_node->left = node;
			return inc_node;
		}

		return node;
	}

	// Handle Parentheses
	if (current_token.type == TOKEN_LPAREN) {
		advance();
		ASTNode *node = parse_expression();
		if (current_token.type != TOKEN_RPAREN) {
			error("Syntax Error: Expected ')'");
		}
		advance();
		return node;
	}

	if (current_token.type == TOKEN_SYSCALL) {
		return parse_syscall();
	}

	if (current_token.type == TOKEN_STRING) {
		ASTNode *node = create_node(NODE_STRING);
		node->var_name = strdup(current_token.name);
		advance();
		return node;
	}

	error("Syntax Error: Unexpected token in factor");
	exit(1);
}

ASTNode *parse_unary(void)
{
	// Address Of (&x or &p.x)
	if (current_token.type == TOKEN_AMP) {
		advance();
		ASTNode *node = create_node(NODE_ADDR);
		node->left = parse_unary();
		return node;
	}

	// Dereference (*x)
	if (current_token.type == TOKEN_STAR) {
		advance();
		ASTNode *node = create_node(NODE_DEREF);
		node->left = parse_unary();
		return node;
	}

	// Negative Numbers (-5)
	if (current_token.type == TOKEN_MINUS) {
		advance();
		ASTNode *node = create_node(NODE_BINOP);
		node->op = '-';
		node->left = create_node(NODE_INT);
		node->left->int_value = 0;
		node->right = parse_unary();
		return node;
	}

	return parse_factor();
}

ASTNode *parse_term(void)
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

ASTNode *parse_math(void)
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

ASTNode *parse_bitwise(void) {
	ASTNode *node = parse_math();
	while (current_token.type == TOKEN_AMP || current_token.type == TOKEN_PIPE) {
		char op = (current_token.type == TOKEN_AMP) ? '&' : '|';
		ASTNode *newNode = create_node(NODE_BINOP);
		newNode->op = op;
		newNode->left = node;
		advance();
		newNode->right = parse_math();
		node = newNode;
	}
	return node;
}

ASTNode *parse_comparison(void)
{
	ASTNode *node = parse_bitwise();
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

ASTNode *parse_expression(void)
{
	ASTNode* lhs = parse_comparison();

	// Check for Assignment
	if (current_token.type == TOKEN_ASSIGN) {
		advance(); // consume '='

		// Check valid L-value: Var, Array, Deref, or MEMBER ACCESS
		if (lhs->type != NODE_VAR_REF &&
			lhs->type != NODE_ARRAY_ACCESS &&
			lhs->type != NODE_DEREF &&
			lhs->type != NODE_MEMBER_ACCESS)
			error("Syntax Error: Invalid l-value.");

		ASTNode *assign = create_node(NODE_ASSIGN);

		if (lhs->type == NODE_DEREF) {
			assign->left = lhs;
			assign->var_name = NULL;
		} else if (lhs->type == NODE_ARRAY_ACCESS) {
			assign->left = lhs;
			assign->var_name = NULL;
		} else if (lhs->type == NODE_MEMBER_ACCESS) {
			assign->left = lhs;
			assign->var_name = NULL;
		} else {
			// Normal var
			assign->var_name = lhs->var_name;
			assign->left = NULL;
			free(lhs);
		}

		assign->right = parse_expression();
		return assign;
	}
	return lhs;
}

// Struct Definition: struct Point { x: int, y: int }
ASTNode *parse_struct_definition(void)
{
	advance(); // Skip 'struct'

	if (current_token.type != TOKEN_IDENTIFIER) error("Expected struct name");
	char *struct_name = strdup(current_token.name);
	advance();

	if (current_token.type != TOKEN_LBRACE) error("Expected '{'");
	advance();

	// Create Entry in Registry
	StructDef *new_struct = &struct_registry[struct_count++];
	strcpy(new_struct->name, struct_name);
	new_struct->member_count = 0;
	new_struct->size = 0;

	while (current_token.type != TOKEN_RBRACE) {
		if (current_token.type != TOKEN_IDENTIFIER) error("Expected member name");
		char *mem_name = strdup(current_token.name);
		advance();

		if (current_token.type != TOKEN_COLON) error("Expected ':'");
		advance();

		// Parse Type (int, ptr, char)
		int mem_size = 8; // Default 8 bytes
		// In the future we can support nested structs here
		if (current_token.type == TOKEN_INT_TYPE ||
			current_token.type == TOKEN_PTR_TYPE ||
			current_token.type == TOKEN_CHAR_TYPE) {
			advance();
		} else {
			error("Unknown member type");
		}

		// Add Member
		strcpy(new_struct->members[new_struct->member_count].name, mem_name);
		new_struct->members[new_struct->member_count].offset = new_struct->size;
		new_struct->member_count++;
		new_struct->size += mem_size;

		if (current_token.type == TOKEN_COMMA) advance();
		free(mem_name);
	}

	advance(); // Skip '}'

	// Optional semicolon
	if (current_token.type == TOKEN_SEMI) advance();

	free(struct_name);
	return NULL; // No executable code generated
}

// Variable Declaration: int x; OR Point p;
ASTNode *parse_var_declaration(void)
{
	char *type_name = NULL;

	// 1. Detect Type
	if (current_token.type == TOKEN_INT_TYPE) {
		type_name = "int";
		advance();
	} else if (current_token.type == TOKEN_PTR_TYPE) {
		type_name = "ptr";
		advance();
	} else if (current_token.type == TOKEN_CHAR_TYPE) {
		type_name = "char";
		advance();
	} else if (current_token.type == TOKEN_IDENTIFIER) {
		// Check if it's a known struct
		if (get_struct(current_token.name)) {
			type_name = strdup(current_token.name);
			advance();
		} else {
			error("Unknown type specifier");
		}
	} else {
		error("Expected type specifier");
	}

	if (current_token.type != TOKEN_IDENTIFIER) error("Expected variable name");
	char *name = strdup(current_token.name);
	advance();

	// Array: int x[10];
	if (current_token.type == TOKEN_LBRACKET) {
		advance();
		if (current_token.type != TOKEN_INT) error("Array size must be integer literal");
		int size = current_token.value;
		advance();
		if (current_token.type != TOKEN_RBRACKET) error("Expected ']'");
		advance();
		if (current_token.type != TOKEN_SEMI) error("Expected ';'");
		advance();

		ASTNode *node = create_node(NODE_ARRAY_DECL);
		node->var_name = name;
		node->int_value = size;
		return node;
	}

	// Variable: int x = 5; OR Point p;
	ASTNode *node = create_node(NODE_VAR_DECL);
	node->var_name = name;
	// CRITICAL: Pass the type name to Codegen!
	// We use member_name field to store the type name string for declarations
	node->member_name = type_name ? strdup(type_name) : strdup("int");

	if (current_token.type == TOKEN_ASSIGN) {
		advance();
		node->left = parse_expression();
	}

	if (current_token.type != TOKEN_SEMI) error("Expected ';' after declaration");
	advance();

	if (type_name && strcmp(type_name, "int") != 0 && strcmp(type_name, "ptr") != 0 && strcmp(type_name, "char") != 0) {
		free(type_name);
	}

	return node;
}

ASTNode *parse_statement(void)
{
	if (current_token.type == TOKEN_RETURN) {
		advance();
		ASTNode *node = create_node(NODE_RETURN);
		node->left = parse_expression();
		if (current_token.type != TOKEN_SEMI) error("Expected ';'");
		advance();
		return node;
	}

	// Struct Definition
	if (current_token.type == TOKEN_STRUCT) {
		return parse_struct_definition();
	}

	// Variable Declarations (int, ptr, char, OR struct names)
	if (current_token.type == TOKEN_INT_TYPE ||
		current_token.type == TOKEN_PTR_TYPE ||
		current_token.type == TOKEN_CHAR_TYPE) {
		return parse_var_declaration();
	}

	// Check for "Point p;" where Point is an identifier
	if (current_token.type == TOKEN_IDENTIFIER) {
		// Look ahead? No, get_struct handles lookup.
		if (get_struct(current_token.name)) {
			return parse_var_declaration();
		}
	}

	if (current_token.type == TOKEN_IF) return parse_if();
	if (current_token.type == TOKEN_WHILE) return parse_while();

	ASTNode *node = parse_expression();
	if (current_token.type != TOKEN_SEMI) error("Expected ';'");
	advance();
	return node;
}

ASTNode *parse_block(void)
{
	if (current_token.type != TOKEN_LBRACE) error("Expected '{'");
	advance();

	ASTNode *block = create_node(NODE_BLOCK);
	ASTNode *curr = NULL;

	while (current_token.type != TOKEN_RBRACE && current_token.type != TOKEN_EOF) {
		ASTNode *stmt = parse_statement();
		if (stmt) { // parse_struct_def returns NULL
			if (!block->left) {
				block->left = stmt;
				curr = stmt;
			} else {
				curr->next = stmt;
				curr = stmt;
			}
		}
	}

	if (current_token.type != TOKEN_RBRACE) error("Expected '}'");
	advance();
	return block;
}

ASTNode *parse_function(void)
{
	if (current_token.type != TOKEN_FN) return NULL;
	advance();

	char *name = strdup(current_token.name);
	advance();

	if (current_token.type != TOKEN_LPAREN) error("Expected '('");
	advance();

	ASTNode *first_param = NULL;
	ASTNode *current_param = NULL;

	while (current_token.type != TOKEN_RPAREN) {
		if (current_token.type != TOKEN_IDENTIFIER) error("Expected parameter name");
		ASTNode *param = create_node(NODE_VAR_DECL);
		param->var_name = strdup(current_token.name);
		param->left = NULL;
		advance();

		if (current_token.type != TOKEN_COLON) error("Expected ':'");
		advance();

		// Parameter Types
		// We use member_name to store the type string for params too
		char *type = "int";
		if (current_token.type == TOKEN_INT_TYPE) { type = "int"; advance(); }
		else if (current_token.type == TOKEN_PTR_TYPE) { type = "ptr"; advance(); }
		else if (current_token.type == TOKEN_CHAR_TYPE) { type = "char"; advance(); }
		else if (current_token.type == TOKEN_IDENTIFIER && get_struct(current_token.name)) {
			type = strdup(current_token.name);
			advance();
		} else {
			error("Invalid parameter type");
		}
		param->member_name = strdup(type);

		if (first_param == NULL) { first_param = param; current_param = param; }
		else { current_param->next = param; current_param = param; }

		if (current_token.type == TOKEN_COMMA) advance();
		else if (current_token.type != TOKEN_RPAREN) error("Expected ',' or ')'");
	}

	advance(); // ')'

	if (current_token.type == TOKEN_ARROW) {
		advance();
		// Skip return type for now, we don't enforce it strictly
		advance();
	}

	ASTNode *func = create_node(NODE_FUNCTION);
	func->var_name = name;
	func->left = first_param;
	func->body = parse_block();

	return func;
}

ASTNode *parse_syscall(void)
{
	advance(); // syscall
	if (current_token.type != TOKEN_LPAREN) error("Expected '('");
	advance();

	ASTNode *call_node = create_node(NODE_SYSCALL);
	ASTNode *current_arg = NULL;

	while (current_token.type != TOKEN_RPAREN) {
		ASTNode *expr = parse_expression();
		if (call_node->left == NULL) { call_node->left = expr; current_arg = expr; }
		else { current_arg->next = expr; current_arg = expr; }

		if (current_token.type == TOKEN_COMMA) advance();
		else if (current_token.type != TOKEN_RPAREN) error("Expected ',' or ')'");
	}
	advance();
	return call_node;
}

void free_ast(ASTNode *node)
{
	if (node == NULL) return;
	free_ast(node->left);
	free_ast(node->right);
	free_ast(node->body);
	free_ast(node->next);
	if (node->var_name) free(node->var_name);
	if (node->member_name) free(node->member_name);
	free(node);
}