#ifdef __APPLE__
#include <TargetConditionals.h>
#endif

#include <fcntl.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/errno.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>
#include <inttypes.h>

#include <gc/gc.h>

const char **cider_argv;
int64_t cider_argc;

void cider_init(int argc, const char *argv[]) {
	/* initialize the collector */
	GC_INIT();

	cider_argc = argc;
	cider_argv = argv;
	/* start mutator ... */
}

int64_t cider_sys_argc() {
	return (int64_t)cider_argc;
}

const char **cider_sys_argv() {
	return cider_argv;
}

int64_t cider_errno() {
	return (int64_t)errno;
}

int64_t cider_memcmp(const char *a, const char *b, int64_t len) {
  return memcmp(a, b, len);
}

int64_t cider_open(const char *path, int64_t flags, int64_t mode) {
  return open(path, flags, mode);
}

int64_t cider_seek(int fd, int64_t offset, int64_t whence) {
  return lseek(fd, offset, whence);
}

int64_t cider_creat(const char *path, int64_t mode) {
    return creat(path, mode);
}

int64_t cider_close(int64_t fd) {
  return close(fd);
}

int64_t cider_read(int64_t fd, char *pb, int64_t nbyte) {
  return read(fd, pb, nbyte);
}

int64_t cider_write(int64_t fd, char *pb, int64_t nbyte) {
  return write(fd, pb, nbyte);
}

int64_t cider_unlink(const char *filename) {
  return unlink(filename);
}

int64_t cider_socket(int64_t domain, int64_t type, int64_t protocol) {
  return socket(domain, type, protocol);
}

const char *cider_memmem(const char *big,
                        int64_t big_len,
                        const char *little,
                        int64_t little_len) {
  /* we need something to compare */
  if (little_len == 0) {
    return NULL;
  }

  if (big_len < little_len) {
    /* little block can't possibly exist in big block */
    return NULL;
  }

  if (big_len == 0) {
    return NULL;
  }

  if (little_len == sizeof(char)) {
    return memchr(big, *little, big_len);
  } else {
    const char *max_big = big + big_len - little_len + 1;

    while (big < max_big) {
      if (memcmp(big, little, little_len) == 0) {
        return big;
      }
      ++big;
    }

    return NULL;
  }
}

const char *cider_strerror(int errnum, char *buf, int64_t bufsize) {
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

void *cider_malloc(uint64_t cb) {
  void *pb = GC_MALLOC(cb);
  // printf("allocated %" PRId64 " bytes at 0x%08" PRIx64 "\n", cb,
  // (uint64_t)pb);
  return pb;
}

int64_t cider_strlen(const char *sz) {
	return strlen(sz);
}

void *cider_print_int64(int64_t x) {
  printf("%" PRId64 "\n", x);
  return 0;
}

int64_t cider_write_char(int64_t fd, char x) {
  char sz[] = {x};
  return write(fd, sz, 1);
}

int64_t cider_char_to_int(char ch) {
	return (int64_t)ch;
}

double cider_itof(int64_t x) {
  return (double)x;
}

char *cider_itoa(int64_t x) {
  char sz[128];
  if (snprintf(sz, sizeof(sz), "%" PRId64, x) < 1) {
    perror("Failed in cider_itoa");
    exit(1);
  }
  return GC_strndup(sz, strlen(sz));
}

const char *cider_dup_free(const char *src) {
  const char *sz = GC_strndup(src, strlen(src));
  free((void *)src);
  return sz;
}

char *cider_ftoa(double x) {
  char sz[128];
  /* IEEE double precision floats have about 15 decimal digits of precision */
  // For now, let's use 6.
  if (snprintf(sz, sizeof(sz), "%.6f", x) < 1) {
    perror("Failed in cider_ftoa");
    exit(1);
  }
  return GC_strndup(sz, strlen(sz));
}

double cider_atof(const char *sz, size_t n) {
  char buf[64];
  const size_t buf_size = sizeof(buf) / sizeof(buf[0]);
  size_t byte_count_to_copy = (n >= buf_size ? buf_size - 1 : n);
  memcpy(buf, sz, byte_count_to_copy);
  buf[byte_count_to_copy] = '\0';
  return atof(buf);
}

int64_t cider_atoi(const char *sz, size_t n) {
	char buf[64];
	const size_t buf_size = sizeof(buf) / sizeof(buf[0]);
	size_t byte_count_to_copy = (n >= buf_size ? buf_size - 1 : n);
	memcpy(buf, sz, byte_count_to_copy);
	buf[byte_count_to_copy] = '\0';
	return atoll(buf);
}

void cider_pass_test() {
  write(1, "PASS\n", 5);
}

int64_t cider_puts(char *sz) {
  if (sz == 0) {
    const char *error = "attempt to puts a null pointer!\n";
    write(1, error, cider_strlen(error));
  }
  write(1, sz, cider_strlen(sz));
  write(1, "\n", 1);
  return 0;
}

int64_t cider_epoch_millis() {
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

int64_t cider_hash_combine(uint64_t seed, uint64_t value) {
  return seed ^ (value + 0x9e3779b97f4a7c15LLU + (seed << 12) + (seed >> 4));
}
