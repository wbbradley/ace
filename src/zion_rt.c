#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>

void *zion_malloc(uint64_t cb) {
	void *pv = calloc(cb, 1);
	// printf("allocating %d bytes -> 0x%08llx\n", (int)cb, (long long)pv);
	return pv;
}

int zion_strlen(const char *sz) {
	const char *start = sz;
	while (*sz++) {}
	return sz - start;
}

void *zion_print_int64(int64_t x) {
  printf("%lld\n", (long long)x);
  return 0;
}

char *zion_itoa(int64_t x) {
	// TODO: change this to use GC
	char *sz;
    if (asprintf(&sz, "%lld", x) < 0) {
      perror("Failed in zion_itoa");
	  exit(1);
    }
	return sz;
}

int zion_puts(char *sz) {
	if (sz == 0) {
		const char *error = "attempt to puts a null pointer!\n";
		write(1, error, zion_strlen(error));
	}
	write(1, sz, zion_strlen(sz));
	write(1, "\n", 1);
	return 0;
}
