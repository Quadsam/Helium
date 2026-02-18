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
			optimize_ast(func);
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

void error_at_pos(int line_num, int col_num, int offset, const char *fmt, ...)
{
	// Print Header: "file:line:col: "
	fprintf(stderr, "%s:%d:%d: \n", current_filename, line_num, col_num);

	// Print the formatted message
	va_list args;
	va_start(args, fmt);
	vfprintf(stderr, fmt, args);
	va_end(args);

	fprintf(stderr, "\n");

	// Scan backwards from offset to find start of line
	int line_start = offset;
	while (line_start > 0 && source_code[line_start - 1] != '\n')
		line_start--;

	// Scan forwards from offset to find end of line
	int line_end = offset;
	while (source_code[line_end] != '\0' && source_code[line_end] != '\n')
		line_end++;

	// Print the Source Line
	fprintf(stderr, "\t");
	for (int j = line_start; j < line_end; j++)
		fputc(source_code[j], stderr);
	fprintf(stderr, "\n");

	// Print the Caret
	// Calculate actual column distance based on offset
	int caret_col = offset - line_start; 
	
	fprintf(stderr, "\t");
	for (int j = 0; j < caret_col; j++)
		fputc(' ', stderr);
	fprintf(stderr, "^\n");

	exit(1);
}

void error_at(Token token, const char *message) {
	error_at_pos(token.line, token.column, token.offset, "%s", message);
}
