/* BUILTINS
 * zion_int_t overloads
 * */
#include "zion_rt.h"

zion_int_t __int_neg(zion_int_t x) {
	return -x;
}

zion_int_t __int_pos(zion_int_t x) {
	return x;
}

zion_int_t __int_int(zion_int_t x) {
	return x;
}

zion_int_t __int_float(zion_float_t x) {
	return (zion_int_t)x;
}

zion_int_t __int_str(char *x) {
	return atoi(x);
}

zion_int_t __int_mask_int(zion_int_t x, zion_int_t y) {
	return x & y;
}

zion_int_t __int_plus_int(zion_int_t x, zion_int_t y) {
	return x + y;
}

zion_int_t __int_minus_int(zion_int_t x, zion_int_t y) {
	return x - y;
}

zion_int_t __int_times_int(zion_int_t x, zion_int_t y) {
	return x * y;
}

zion_int_t __int_divide_int(zion_int_t x, zion_int_t y) {
	return x / y;
}

zion_int_t __int_modulus_int(zion_int_t x, zion_int_t y) {
	return x % y;
}

zion_int_t __int_lt_int(zion_int_t x, zion_int_t y) {
	return x < y;
}

zion_int_t __int_lte_int(zion_int_t x, zion_int_t y) {
	return x <= y;
}

zion_bool_t __int_gt_int(zion_int_t x, zion_int_t y) {
	return x > y;
}

zion_bool_t __int_gte_int(zion_int_t x, zion_int_t y) {
	return x >= y;
}

zion_bool_t __int_ineq_int(zion_int_t x, zion_int_t y) {
	return x != y;
}

zion_bool_t __int_eq_int(zion_int_t x, zion_int_t y) {
	return x == y;
}
