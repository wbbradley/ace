#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char *argv[]) {
	if (argc != 2) {
		return EXIT_FAILURE;
	}

	char *src = strdup(argv[1]);
	char *input = src;
	while (1) {
		src = strtok(input, "_");
		if (!src) {
			break;
		}
		input = NULL;
		if (strcmp(src, "t") == 0) {
			break;
		} else if (strlen(src) != 0) {
			putchar(toupper(src[0]));
			src = &src[1];
			printf("%s", src);
		}
	}

	printf("\n");
	return EXIT_SUCCESS;
}
