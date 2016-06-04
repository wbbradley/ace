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

char *__str_str(char *x) {
	return x;
}
