/* BUILTINS
 * str function overloads
 * */
#include <errno.h>
#include "zion_rt.h"

zion_int_t __errno() {
	return errno;
}

void __set_locale__() {
#ifdef __linux__
#define ZION_LOCALE "C.UTF-8"
#else
#define ZION_LOCALE "en_US.UTF-8"
#endif

	if (setlocale(LC_CTYPE, ZION_LOCALE) == NULL) {
		printf("failed to set ctype locale to %s\n", ZION_LOCALE);
		exit(1);
	}
}

void mem_dump(void *addr, zion_int_t cb) {
	printf("dumping memory at:\n");
	char *pb = addr;
	for (zion_int_t i=0; i < cb / sizeof(long long); ++i) {
		printf("0x%08llx 0x%08llx\n", (long long)pb, *(long long*)pb);
		pb += sizeof(long long);
	}
}

char *__str_int_radix(zion_int_t x, zion_int_t radix) {
	char onstack[65];
	if (radix == 10) {
		snprintf(onstack, sizeof(onstack) / sizeof(onstack[0]), "%lld", (long long)x);
	} else if (radix == 8) {
		snprintf(onstack, sizeof(onstack) / sizeof(onstack[0]), "%llo", (long long)x);
	} else if (radix == 16) {
		snprintf(onstack, sizeof(onstack) / sizeof(onstack[0]), "%llx", (long long)x);
	} else {
		printf("unsupported radix requested in __str_int_radix for value %lld", (long long)x);
		exit(1);
	}
	return strdup(onstack);
}

char *__str_int(zion_int_t x) {
	char onstack[65];
	snprintf(onstack, sizeof(onstack) / sizeof(onstack[0]), "%lld", (long long)x);
	return strdup(onstack);
}

char *__str_float(zion_float_t x) {
	char onstack[65];
	snprintf(onstack, sizeof(onstack) / sizeof(onstack[0]), "%f", x);
	return strdup(onstack);
}

wchar_t *__wcs_float(zion_float_t x) {
	wchar_t onstack[65];
	swprintf(onstack, sizeof(onstack) / sizeof(onstack[0]), L"%f", x);
	return wcsdup(onstack);
}

wchar_t *__wcs_int(zion_int_t x) {
	wchar_t onstack[65];
	swprintf(onstack, sizeof(onstack) / sizeof(onstack[0]), L"%lld", x);
	return wcsdup(onstack);
}

char *__str_type_id(type_id_t x) {
	char onstack[65];
	snprintf(onstack, sizeof(onstack) / sizeof(onstack[0]), "%d", x);
	return strdup(onstack);
}

char *__str_str(char *x) {
	return x;
}

char *__mbs_concat(char *x, char *y) {
	zion_int_t x_len = strlen(x);
	zion_int_t y_len = strlen(y) + 1;
	char *res = malloc(x_len + y_len);
	memcpy(res, x, x_len);
	memcpy(res + x_len, y, y_len);
	return res;
}

wchar_t *__wcs_concat(wchar_t *x, wchar_t *y) {
	zion_int_t x_len = wcslen(x);
	zion_int_t x_cb = sizeof(wchar_t) * x_len;
	zion_int_t y_cb = sizeof(wchar_t) * (wcslen(y) + 1);
	wchar_t *res = malloc(x_cb + y_cb);
	memcpy(res, x, x_cb);
	memcpy(res + x_len, y, y_cb);
	return res;
}

char *__ptr_to_str_get_item(char **x, zion_int_t index) {
	return x[index];
}

zion_int_t hexdigit(zion_int_t val) {
	if (val < 0 || val >= 16) {
		printf("call to hexdigit with value %lld. aborting.\n",
				(long long)val);
		exit(1);
	}
	if (val >= 10)
		return 'a' + val - 10;
	return '0' + val;
}

