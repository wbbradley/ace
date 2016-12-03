/* BUILTINS
 * str function overloads
 * */
#include "zion_rt.h"

char *__str_int(zion_int_t x) {
	char onstack[50];
	snprintf(onstack, sizeof(onstack), "%lld", x);
	onstack[sizeof(onstack) - 1] = 0;
	return strdup(onstack);
}

char *__str_float(zion_float_t x) {
	char onstack[12];
	snprintf(onstack, sizeof(onstack), "%f", x);
	onstack[sizeof(onstack) - 1] = 0;
	return strdup(onstack);
}

char *__str_type_id(type_id_t x) {
	char onstack[12];
	snprintf(onstack, sizeof(onstack), "%d", x);
	onstack[sizeof(onstack) - 1] = 0;
	return strdup(onstack);
}

char *__str_str(char *x) {
	return x;
}

char *__str_plus_str(char *x, char *y) {
	zion_int_t x_len = strlen(x);
	zion_int_t y_len = strlen(y) + 1;
	char *res = malloc(x_len + y_len);
	memcpy(res, x, x_len);
	memcpy(res + x_len, y, y_len);
	return res;
}

zion_bool_t __str_eq_str(char *x, char *y) {
	return strcmp(x, y) == 0;
}
