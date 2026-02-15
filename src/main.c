#include "helium.h"

/* ========================================================================= */
/* DEFINE GLOBALS															 */
/* ========================================================================= */
Token current_token;
char *source_code;
char *current_filename = "unknown";
char *current_func_name = "unknown";
int src_pos = 0;
int current_line = 1;
int current_col = 1;
int filename_allocated = 0;

/* ========================================================================= */
/* MAIN																		 */
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
		} else if (current_token.type == TOKEN_STRUCT) {
			parse_struct_definition();
		} else {
			// Skip imports/macros
			advance();
		}
	}

	// Cleanup
	fclose(stdout); 
	free(source_code);

	if (filename_allocated) free(current_filename);

	free_macros();

	return 0;
}

/* ========================================================================= */
/* ERROR HANDLING															 */
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