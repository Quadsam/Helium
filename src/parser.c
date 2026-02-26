#include "helium.h"

/* ========================================================================= */
/* AST                                                                       */
/* ========================================================================= */

// Helper to make a new node
ASTNode *create_node(NodeType type)
{
	ASTNode *node = malloc(sizeof(ASTNode));
	if (!node)
		error("Failed to create node!");
	node->type = type;
	node->left = node->right = node->body = node->next = NULL;
	node->int_value = 0;
	node->var_name = NULL;
	node->member_name = NULL;
	node->increment = NULL;
	node->line = current_token.line;
	node->column = current_token.column;
	node->offset = current_token.offset;
	node->is_reachable = 0;
	node->is_arrow_access = 0;
	return node;
}

/* ========================================================================= */
/* PARSER                                                                    */
/* ========================================================================= */

ASTNode *parse_expression(void);
ASTNode *parse_block(void);
ASTNode *parse_syscall(void);
ASTNode *parse_statement(void);
ASTNode *parse_struct_definition(void);

static
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

static
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

ASTNode *parse_for(void)
{
	advance(); // Skip 'for'

	int has_parens = 0;
	if (current_token.type == TOKEN_LPAREN) {
		has_parens = 1;
		advance();
	}

	ASTNode *init = NULL;
	ASTNode *condition = NULL;
	ASTNode *increment = NULL;

	// Check if Rust-style: "identifier in" (e.g., for i in 0..10)
	if (current_token.type == TOKEN_IDENTIFIER && peek_next_token().type == TOKEN_IN) {
		char *var_name = strdup(current_token.name);
		advance(); // Consume identifier
		advance(); // Consume 'in'

		ASTNode *start_expr = parse_expression();
		if (current_token.type != TOKEN_DOTDOT) error("Expected '..' in range");
		advance(); // Consume '..'
		ASTNode *end_expr = parse_expression();

		// --- DESUGAR INTO C-STYLE ---
		
		// init: int i = start_expr;
		init = create_node(NODE_VAR_DECL);
		init->var_name = strdup(var_name);
		init->member_name = strdup("int");
		init->left = start_expr;

		// condition: i < end_expr
		condition = create_node(NODE_LT);
		condition->left = create_node(NODE_VAR_REF);
		condition->left->var_name = strdup(var_name);
		condition->right = end_expr;

		// increment: i++
		increment = create_node(NODE_POST_INC);
		increment->left = create_node(NODE_VAR_REF);
		increment->left->var_name = strdup(var_name);

		free(var_name); // Cleanup original
	} else {
		// --- C-STYLE --- (e.g., int i = 0; i < 10; i++)
		init = parse_statement(); // Automatically consumes the ';'

		condition = parse_expression();
		if (current_token.type != TOKEN_SEMI) error("Expected ';'");
		advance(); // consume ';'

		increment = parse_expression();
	}

	if (has_parens) {
		if (current_token.type != TOKEN_RPAREN) error("Expected ')'");
		advance();
	}

	ASTNode *body = parse_block();

	ASTNode *node = create_node(NODE_FOR);
	node->left = init;
	node->right = condition;
	node->increment = increment;
	node->body = body;

	return node;
}

// Factor: handles integers, variables, access
static
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

		// Check for Member Access: p.x
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

		// Check for Arrow Access: p->x
		if (current_token.type == TOKEN_ARROW) {
			advance(); // Consume '->'
			if (current_token.type != TOKEN_IDENTIFIER) error("Expected member name after '->'");

			ASTNode *access = create_node(NODE_MEMBER_ACCESS);
			access->left = node; // The pointer 'p'
			access->member_name = strdup(current_token.name); // The member 'x'
			access->is_arrow_access = 1; // <--- MARK AS ARROW
			advance();

			return access;
		}

		// Check for Function Call: add(1, 2)
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

		// Check for Array Access: x[i]
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

		// Post-Increment: i++
		if (current_token.type == TOKEN_INC) {
			advance();
			ASTNode* inc_node = create_node(NODE_POST_INC);
			inc_node->left = node;
			return inc_node;
		}

		return node;
	}

	// Handle sizeof(Type)
	if (current_token.type == TOKEN_SIZEOF) {
		advance();	// Skip 'sizeof'
		if (current_token.type != TOKEN_LPAREN) error("Expected '(' after sizeof");
		advance();	// Skip '('

		int size = 0;

		if (current_token.type == TOKEN_INT_TYPE) {
			size = 8;
			advance();
		} else if (current_token.type == TOKEN_CHAR_TYPE) {
			size = 1;
			advance();
		} else if (current_token.type == TOKEN_PTR_TYPE) {
			size = 8;
			advance();
		} else if (current_token.type == TOKEN_IDENTIFIER) {
			const StructDef *sdef = get_struct(current_token.name);
			if (sdef) {
				size = sdef->size;
				advance();
			} else
				error("Unknown type in sizeof");
		}

		if (current_token.type != TOKEN_RPAREN) error("Expected ')'");
		advance(); // Skip ')'

		// Return a literal Integer node
		ASTNode *node = create_node(NODE_INT);
		node->int_value = size;
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

static
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

static
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

static
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

static
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

static
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

ASTNode *parse_logical_and(void)
{
	// AND binds tighter than OR, so it calls comparison
	ASTNode *node = parse_comparison();
	while (current_token.type == TOKEN_AND) {
		ASTNode *newNode = create_node(NODE_AND);
		newNode->left = node;
		advance();
		newNode->right = parse_comparison();
		node = newNode;
	}
	return node;
}

ASTNode *parse_logical_or(void)
{
	// OR has lowest precedence, so it calls AND
	ASTNode *node = parse_logical_and();
	while (current_token.type == TOKEN_OR) {
		ASTNode *newNode = create_node(NODE_OR);
		newNode->left = node;
		advance();
		newNode->right = parse_logical_and();
		node = newNode;
	}
	return node;
}


ASTNode *parse_expression(void)
{
	ASTNode* lhs = parse_logical_or();

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

		if (current_token.type == TOKEN_CHAR_TYPE) {
			mem_size = 1;
			advance();
		} else if (current_token.type == TOKEN_INT_TYPE ||
					current_token.type == TOKEN_PTR_TYPE) {
			advance();
		} else if (current_token.type == TOKEN_IDENTIFIER) {
			// Support nested structs (e.g. p: Point)
			const StructDef *sub = get_struct(current_token.name);
			if (sub) {
				mem_size = sub->size;
				advance();
			} else {
				error("Unknown member type");
			}
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
static
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

		node->member_name = type_name ? strdup(type_name) : strdup("int");
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
	if (current_token.type == TOKEN_FOR) return parse_for();

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
		const char *type = "int";
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
	free_ast(node->increment);
	if (node->var_name) free(node->var_name);
	if (node->member_name) free(node->member_name);
	free(node);
}

void optimize_ast(ASTNode *node)
{
	if (!node) return;
	// Optimize Children First (Bottom-Up)
	optimize_ast(node->left);
	optimize_ast(node->right);
	optimize_ast(node->body);
	optimize_ast(node->next);
	optimize_ast(node->increment);

	// Constant Folding: BinOp(Int, Int) -> Int
	if (node->type == NODE_BINOP) {
		if (node->left && node->left->type == NODE_INT &&
			node->right && node->right->type == NODE_INT) {
			
			int v1 = node->left->int_value;
			int v2 = node->right->int_value;
			int res = 0;
			int handled = 1;

			switch (node->op) {
				case '+': res = v1 + v2; break;
				case '-': res = v1 - v2; break;
				case '*': res = v1 * v2; break;
				case '/':
					if (v2 != 0) res = v1 / v2;
					else handled = 0;
					break;
				case '|': res = v1 | v2; break;
				case '&': res = v1 & v2; break;
				default: handled = 0; break;

			}

			if (handled) {
				// Transform this node into a literal INT
				node->type = NODE_INT;
				node->int_value = res;

				// Free the dead children
				free(node->left);
				free(node->right);
				node->left = NULL;
				node->right = NULL;
			}
		}
	}
}

// Helper: Find a function definition in the global list
static
ASTNode *find_function(ASTNode *list, const char *name)
{
	while (list) {
		if (list->type == NODE_FUNCTION && strcmp(list->var_name, name) == 0)
			return list;
		list = list->next;
	}
	return NULL;
}

// Recursive marker
static
void mark_reachable(ASTNode *node, ASTNode *all_funcs) {
	if (!node) return;

	// If we find a function call, mark the definition as reachable
	if (node->type == NODE_FUNC_CALL) {
		ASTNode *target = find_function(all_funcs, node->var_name);
		
		// If found and not yet visited, mark it and recurse into IT
		if (target && !target->is_reachable) {
			target->is_reachable = 1;
			mark_reachable(target->body, all_funcs);
		}
	}

	// Traverse children
	mark_reachable(node->left, all_funcs);
	mark_reachable(node->right, all_funcs);
	mark_reachable(node->body, all_funcs);
	mark_reachable(node->increment, all_funcs);
	
	// CAREFUL: Don't traverse 'next' if it leaves the current scope function chain
	// But for blocks/statements, we do need 'next'. 
	// Since functions are chained via 'next' at the top level, we handle that in analyze_reachability
	if (node->type != NODE_FUNCTION) {
		mark_reachable(node->next, all_funcs);
	}
}

// Main entry point for DCE
void analyze_reachability(ASTNode *all_funcs) {
	// Find 'main'
	ASTNode *main_func = find_function(all_funcs, "main");
	
	if (main_func) {
		main_func->is_reachable = 1;
		mark_reachable(main_func->body, all_funcs);
	}
}