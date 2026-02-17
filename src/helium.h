#ifndef HELIUM_H
#define HELIUM_H

#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <string.h>

/* ========================================================================= */
/* TYPES & ENUMS															 */
/* ========================================================================= */

typedef enum {
	TOKEN_EOF,
	TOKEN_IDENTIFIER,	// x, main, count
	TOKEN_FN,			// fn
	TOKEN_INT,			// 123
	TOKEN_INT_TYPE,		// int
	TOKEN_CHAR,			// 'a'
	TOKEN_CHAR_TYPE,	// char
	TOKEN_PTR_TYPE,		// ptr
	TOKEN_STRUCT,		// struct
	TOKEN_RETURN,		// return
	TOKEN_LPAREN,		// (
	TOKEN_RPAREN,		// )
	TOKEN_LBRACE,		// {
	TOKEN_RBRACE,		// }
	TOKEN_LBRACKET,		// [
	TOKEN_RBRACKET,		// ]
	TOKEN_COMMA,		// ,
	TOKEN_SEMI,			// ;
	TOKEN_COLON,		// :
	TOKEN_PERIOD,		// .
	TOKEN_ASSIGN,		// =
	TOKEN_PLUS,			// +
	TOKEN_INC,			// ++
	TOKEN_MINUS,		// -
	TOKEN_STAR,			// *
	TOKEN_SLASH,		// /
	TOKEN_PIPE,			// |
	TOKEN_AMP,			// &
	TOKEN_EQ,			// ==
	TOKEN_NEQ,			// !=
	TOKEN_LT,			// <
	TOKEN_GT,			// >
	TOKEN_ARROW,		// ->
	TOKEN_IF,			// if
	TOKEN_ELSE,			// else
	TOKEN_WHILE,		// while
	TOKEN_SYSCALL,		// syscall
	TOKEN_STRING,		// "string"
} TokenType;

typedef struct {
	char *name;			// To store "main", "count", "int", exc.
	TokenType type;		// To store the "TOKEN_" type
	int value;			// For integers
	int line;			// For error handling
	int column;			// For error handling
} Token;

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
	NODE_MEMBER_ACCESS,	// p.x
	NODE_STRUCT_DEFN,	// struct Point { x: int ... }
	NODE_FUNC_CALL,     // add(1, 2);
	NODE_ADDR,          // &x (Address of)
	NODE_DEREF,         // *x (Dereference)
} NodeType;

typedef struct ASTNode {
	NodeType type;
	int int_value;			// For literals
	char *var_name;			// For references/declarations
	char *member_name;		// For p.x, this stores "x"
	char op;				// For binary ops
	struct ASTNode *left;	// Left child
	struct ASTNode *right;	// Right child
	struct ASTNode *body;	// For functions
	struct ASTNode *next;	// For linked lists in blocks
	int line;				// For error handling
	int column;				// For error handling
} ASTNode;


// --- Struct Registry ---
typedef struct {
	char name[64];
	int offset;		// Offset from the start of the struct
} StructMember;

typedef struct {
	char name[64];
	StructMember members[20];	// Max of 20 for now
	int member_count;
	int size;					// Total size (bytes)
} StructDef;

/* ========================================================================= */
/* GLOBAL VARIABLES															 */
/* ========================================================================= */

extern Token current_token;
extern char *source_code;
extern char *current_filename;
extern char *current_func_name;
extern int src_pos;
extern int current_col;
extern int current_line;
extern int filename_allocated;

// Struct Registry Globals
extern StructDef struct_registry[20];
extern int struct_count;

/* ========================================================================= */
/* FUNCTION PROTOTYPES														 */
/* ========================================================================= */

// Lexer
Token get_next_token(void);
void advance(void);
void free_macros(void);

// Parser
ASTNode *create_node(NodeType type);
ASTNode *parse_function(void);
ASTNode *parse_struct_definition(void);
void free_ast(ASTNode *node);

// Codegen
void gen_asm(ASTNode *node);
StructDef *get_struct(const char *name);


// Preprocessor
char *preprocess_file(const char *filename);

// Utils
void error(const char *message);
void error_at(Token token, const char *message);
void error_line(int line, const char *message);
void error_coordinate(int line, int col, const char *message);

#endif /* HELIUM_H */
