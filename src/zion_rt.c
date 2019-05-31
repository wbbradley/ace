#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

void *zion_malloc(uint64_t cb) {
  void *pv = calloc(cb, 1);
  // printf("allocating %d bytes -> 0x%08llx\n", (int)cb, (long long)pv);
  return pv;
}

int zion_strlen(const char *sz) {
  return strlen(sz);
}

void *zion_print_int64(int64_t x) {
  printf("%" PRId64 "\n", x);
  return 0;
}

int zion_write_char(int64_t fd, char x) {
  char sz[] = {x};
  return write(fd, sz, 1);
}

char *zion_itoa(int64_t x) {
  char sz[128];
  if (snprintf(sz, sizeof(sz), "%" PRId64, x) < 1) {
    perror("Failed in zion_itoa");
    exit(1);
  }
  return strdup(sz);
}

void zion_pass_test() {
  printf("PASS\n");
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
