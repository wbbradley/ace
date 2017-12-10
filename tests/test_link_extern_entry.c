#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct Entry {
	char *name;
	int number;
};

struct Entry *make_entry(char *name, int number) {
	printf("allocating entry for %s: %d\n", name, number);
	struct Entry *entry = calloc(sizeof(struct Entry), 1);
	entry->name = strdup(name);
	entry->number = number;
	return entry;
}

char *get_name(struct Entry *entry) {
	return entry->name;
}

int get_number(struct Entry *entry) {
	return entry->number;
}

void free_entry(struct Entry *entry) {
	printf("freeing entry %s: %d\n", entry->name, entry->number);
	free(entry->name);
	free(entry);
}
