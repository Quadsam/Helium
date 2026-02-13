#include "helium.h"

/* ========================================================================= */
/* PREPROCESSOR																 */
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

	// 1. Emit Initial Marker (#file "filename" 1)
	// This tells the lexer "We are starting this file at line 1"
	char marker[512];
	snprintf(marker, sizeof(marker), "#file \"%s\" 1\n", filename);
	append_string(&buffer, &length, &capacity, marker);

	char line[1024];
	int file_line_number = 1; // Track line numbers in THIS file

	while (fgets(line, sizeof(line), f)) {
		file_line_number++; // We are about to process this line
		
		// Check for #include
		char *include_ptr = strstr(line, "#include");
		
		if (include_ptr) {
			char *start_quote = strchr(line, '"');
			char *end_quote = strrchr(line, '"');
			
			if (start_quote && end_quote && end_quote > start_quote) {
				*end_quote = '\0';
				char *included_filename = start_quote + 1;
				
				// Recursively read the included file
				// (The child call will emit its own "start marker")
				char *included_content = preprocess_file(included_filename);
				
				append_string(&buffer, &length, &capacity, included_content);
				
				// 2. Emit Restore Marker
				// We just came back from an include. We must tell the lexer:
				// "We are back in 'filename' at 'file_line_number'"
				snprintf(marker, sizeof(marker), "\n#file \"%s\" %d\n", filename, file_line_number);
				append_string(&buffer, &length, &capacity, marker);
				
				free(included_content);
				continue; 
			}
		}

		append_string(&buffer, &length, &capacity, line);
	}

	fclose(f);
	return buffer;
}
