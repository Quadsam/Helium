#include "helium.h"

/* ========================================================================= */
/* MACROS																	 */
/* ========================================================================= */

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
/* LEXER																	 */
/* ========================================================================= */

Token get_next_token(void)
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
		return (Token){"EOF", TOKEN_EOF, 0, start_line, start_col};
	}

	char current = source_code[src_pos];

	// Handle Identifiers/Keywords (Allow _ at start)
	if (isalpha(current) || current == '_') {
		Token t;
		t.line = start_line;
		t.column = start_col;

		char buffer[256];
		int i = 0;
		// Allow alphanumeric OR underscore in the body
		while (isalnum(source_code[src_pos]) || source_code[src_pos] == '_') {
			buffer[i++] = source_code[src_pos++];
			current_col++; 
		}
		buffer[i] = '\0';

		t.name = strdup(buffer);

		if (strcmp(t.name, "fn")            == 0) t.type = TOKEN_FN;
		else if (strcmp(t.name, "int")      == 0) t.type = TOKEN_INT_TYPE;
		else if (strcmp(t.name, "ptr")      == 0) t.type = TOKEN_PTR_TYPE;
		else if (strcmp(t.name, "char")     == 0) t.type = TOKEN_CHAR_TYPE;
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

			if (subst.type == TOKEN_IDENTIFIER || subst.type == TOKEN_STRING) {
				subst.name = strdup(subst.name);
			}

			free(t.name);

			return subst;
		}

		return t;
	}

	// Handle Integers
	if (isdigit(current)) {
		Token t;
		t.type = TOKEN_INT;
		t.name = NULL;
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
		case '(': src_pos++; current_col++; return (Token){"(", TOKEN_LPAREN, 0, start_line, start_col};
		case ')': src_pos++; current_col++; return (Token){")", TOKEN_RPAREN, 0, start_line, start_col};
		case '{': src_pos++; current_col++; return (Token){"{", TOKEN_LBRACE, 0, start_line, start_col};
		case '}': src_pos++; current_col++; return (Token){"}", TOKEN_RBRACE, 0, start_line, start_col};
		case '[': src_pos++; current_col++; return (Token){"[", TOKEN_LBRACKET, 0, start_line, start_col};
		case ']': src_pos++; current_col++; return (Token){"]", TOKEN_RBRACKET, 0, start_line, start_col};
		case ',': src_pos++; current_col++; return (Token){",", TOKEN_COMMA, 0, start_line, start_col};
		case ';': src_pos++; current_col++; return (Token){";", TOKEN_SEMI, 0, start_line, start_col};
		case ':': src_pos++; current_col++; return (Token){":", TOKEN_COLON, 0, start_line, start_col};
		case '*': src_pos++; current_col++; return (Token){"*", TOKEN_STAR, 0, start_line, start_col};
		case '|': src_pos++; current_col++; return (Token){"|", TOKEN_PIPE, 0, start_line, start_col};
		case '&': src_pos++; current_col++; return (Token){"&", TOKEN_AMP, 0, start_line, start_col};
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
			return (Token){"/", TOKEN_SLASH, 0, start_line, start_col};
		case '-': 
			if (source_code[src_pos+1] == '>') {
				src_pos+=2; current_col+=2; 
				return (Token){"->", TOKEN_ARROW, 0, start_line, start_col};
			}
			src_pos++; current_col++; 
			return (Token){"-", TOKEN_MINUS, 0, start_line, start_col};
		case '+': 
			if (source_code[src_pos+1] == '+') {
				src_pos+=2; current_col+=2; 
				return (Token){"++", TOKEN_INC, 0, start_line, start_col};
			}
			src_pos++; current_col++;
			return (Token){"+", TOKEN_PLUS, 0, start_line, start_col};

		case '=':
			if (source_code[src_pos+1] == '=') {
				src_pos+=2; current_col+=2; 
				return (Token){"==", TOKEN_EQ, 0, start_line, start_col};
			}
			src_pos++; current_col++;
			return (Token){"=", TOKEN_ASSIGN, 0, start_line, start_col};

		case '!':
			if (source_code[src_pos+1] == '=') {
				src_pos+=2; current_col+=2; 
				return (Token){"!=", TOKEN_NEQ, 0, start_line, start_col};
			}
			error_at((Token){"", 0, 0, start_line, start_col}, "Expected '!='");
			exit(1);
		case '<': src_pos++; current_col++; return (Token){"<", TOKEN_LT, 0, start_line, start_col};
		case '>': src_pos++; current_col++; return (Token){">", TOKEN_GT, 0, start_line, start_col};
		case '"': {
			Token t;
			t.type = TOKEN_STRING;
			t.line = start_line;
			t.column = start_col;

			char buffer[1024];
			int i = 0;

			src_pos++; current_col++;   // Skip opening "
			while (source_code[src_pos] != '"' && source_code[src_pos] != '\0') {
				if (source_code[src_pos] == '\\' && source_code[src_pos+1] == 'n') {
					buffer[i++] = '\\'; buffer[i++] = 'n';
					src_pos += 2; current_col += 2;
				} else {
					buffer[i++] = source_code[src_pos++];
					current_col++;
				}
			}
			buffer[i] = '\0';

			t.name = strdup(buffer);

			if (source_code[src_pos] == '"') {
				src_pos++; current_col++;   // Skip closing "
			}
			return t;
		}
		case '#': {
			src_pos++; // Skip '#'
			
			// Skip whitespace
			while (source_code[src_pos] != '\0' && isspace(source_code[src_pos]) && source_code[src_pos] != '\n') {
				 src_pos++;
			}

			// #file "name" line
			if (strncmp(&source_code[src_pos], "file", 4) == 0) {
				src_pos += 4;	// Skip "file"
				
				// Parse Filename
				while (source_code[src_pos] != '"' && source_code[src_pos] != '\n') src_pos++;
				if (source_code[src_pos] == '"') {
					src_pos++;	// Skip opening "
					
					char new_name[256];
					int i = 0;
					while (source_code[src_pos] != '"' && i < 255) {
						new_name[i++] = source_code[src_pos++];
					}
					new_name[i] = '\0';
					src_pos++;	// Skip closing "

					if (filename_allocated) free(current_filename);
					
					// Update Global State
					// (Note: In a real compiler we'd manage memory better, but strdup is fine for V1)
					current_filename = strdup(new_name);
					filename_allocated = 1;
				}

				// Parse Line Number
				while (!isdigit(source_code[src_pos]) && source_code[src_pos] != '\n') src_pos++;
				if (isdigit(source_code[src_pos])) {
					int num = 0;
					while (isdigit(source_code[src_pos])) {
						num = num * 10 + (source_code[src_pos++] - '0');
					}
					// Update Global State
					// Subtract 1 because the very next newline will increment it to the correct number
					current_line = num - 1; 
				}
				
				// Skip the rest of the line
				while (source_code[src_pos] != '\n' && source_code[src_pos] != '\0') src_pos++;
				
				return get_next_token(); // Recurse to get the next real token
			}

			// CHECK 2: #define ... (Existing logic)
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
				 
				 Token value = get_next_token();
				 
				 // NEGATIVE NUMBER FIX
				 if (value.type == TOKEN_MINUS) {
					Token next = get_next_token();
					if (next.type == TOKEN_INT) {
						value.type = TOKEN_INT;
						value.value = -next.value; 
					} else {
						error("Macros must be single tokens or negative integers");
					}
				}

				 add_macro(name, value);
				 return get_next_token();
			}
			
			// Unknown directive? Ignore line
			while (source_code[src_pos] != '\0' && source_code[src_pos] != '\n') src_pos++;
			return get_next_token();
		}

		case '\'': { // Handle 'c'
			Token t;
			t.type = TOKEN_CHAR;
			t.name = NULL;
			t.line = start_line;
			t.column = start_col;

			src_pos++; current_col++; // Skip opening '

			if (source_code[src_pos] == '\'') {
				error_at(t, "Empty character literal");
			}

			// Handle Escape Sequences
			if (source_code[src_pos] == '\\') {
				src_pos++; current_col++;
				char escape = source_code[src_pos];
				if (escape == 'n') t.value = 10;       // \n
				else if (escape == 't') t.value = 9;   // \t
				else if (escape == '0') t.value = 0;   // \0
				else if (escape == '\\') t.value = 92; // Backslash
				else if (escape == '\'') t.value = 39; // \'
				else error_at(t, "Unknown escape sequence");
			} else {
				t.value = (int)source_code[src_pos];
			}
			src_pos++; current_col++;

			if (source_code[src_pos] != '\'') {
				error_at(t, "Expected closing '");
			}
			src_pos++; current_col++; // Skip closing '

			return t;
		}

		default: 
			error_at((Token){"", 0, 0, start_line, start_col}, "Unknown character");
			exit(1);
	}
}

void advance(void)
{
	if (current_token.name) {
		if (
			current_token.type == TOKEN_IDENTIFIER ||
			current_token.type == TOKEN_STRING ||
			current_token.type == TOKEN_FN ||
			current_token.type == TOKEN_INT_TYPE ||
			current_token.type == TOKEN_PTR_TYPE ||
			current_token.type == TOKEN_CHAR_TYPE ||
			current_token.type == TOKEN_RETURN ||
			current_token.type == TOKEN_IF ||
			current_token.type == TOKEN_ELSE ||
			current_token.type == TOKEN_WHILE ||
			current_token.type == TOKEN_SYSCALL) {
			free(current_token.name);
		}
	}

	current_token = get_next_token();
}

void free_macros(void)
{
	for (int i = 0; i < macro_count; i++) {
		// Check if the token inside the macro has an allocated name
		if (macros[i].value.name && (
			macros[i].value.type == TOKEN_IDENTIFIER ||
			macros[i].value.type == TOKEN_STRING)) {
			free(macros[i].value.name);
		}
	}
}