#ifdef linux
#define _GNU_SOURCE
#endif

#include <inttypes.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/errno.h>
#include <time.h>
#include <unistd.h>

#include <gc/gc.h>

const char **zion_argv;
int zion_argc;

void zion_init(int argc, const char *argv[]) {
	/* initialize the collector */
	GC_INIT();

	zion_argc = argc;
	zion_argv = argv;
	/* start mutator ... */
}

int64_t zion_sys_argc() {
	return (int64_t)zion_argc;
}

const char **zion_sys_argv() {
	return zion_argv;
}

int64_t zion_errno() {
	return (int64_t)errno;
}

int64_t zion_memcmp(const char *a, const char *b, int64_t len) {
	return memcmp(a, b, len);
}

const char * zion_memmem(const char *big, int64_t big_len, const char *little, int64_t little_len) {
	return memmem(big, big_len, little, little_len);
}

const char *zion_strerror(int errnum, char *buf, int64_t bufsize) {
#ifdef __APPLE__
	if (strerror_r(errnum, buf, bufsize) == 0) {
		return buf;
	} else {
		return "Failed to find error description.";
	}
#else
	strncpy(buf, "Unknown error", bufsize);
	strerror_r(errnum, buf, bufsize);
	return buf;
#endif
}
void *zion_malloc(uint64_t cb) {
  void *pb = GC_MALLOC(cb);
  // printf("allocated %" PRId64 " bytes at 0x%08" PRIx64 "\n", cb, (uint64_t)pb);
  return pb;
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

int64_t zion_char_to_int(char ch) {
	return (int64_t)ch;
}

char *zion_itoa(int64_t x) {
  char sz[128];
  if (snprintf(sz, sizeof(sz), "%" PRId64, x) < 1) {
    perror("Failed in zion_itoa");
    exit(1);
  }
  return GC_strndup(sz, strlen(sz));
}

char *zion_ftoa(double x) {
  char sz[128];
  /* IEEE double precision floats have about 15 decimal digits of precision */
  if (snprintf(sz, sizeof(sz), "%.15f", x) < 1) {
    perror("Failed in zion_ftoa");
    exit(1);
  }
  return GC_strndup(sz, strlen(sz));
}

void zion_pass_test() {
  write(1, "PASS\n", 5);
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

int64_t zion_epoch_millis() {
  long ms;  // Milliseconds
  time_t s; // Seconds
  struct timespec spec;

  clock_gettime(CLOCK_REALTIME, &spec);

  s = spec.tv_sec;
  ms = lround(spec.tv_nsec / 1.0e6); // Convert nanoseconds to milliseconds
  if (ms > 999) {
    s++;
    ms = 0;
  }
  return (int64_t)s * 1000 + ms;
}
