/* BUILTINS
 * zion_float_t overloads
 * */
#include "zion_rt.h"

zion_float_t __float_neg(zion_float_t x) {
	return -x;
}

zion_float_t __float_pos(zion_float_t x) {
	return x;
}

zion_float_t __float_int(zion_int_t x) {
	return x;
}

zion_float_t __float_float(zion_float_t x) {
	return x;
}

zion_float_t __float_from_utf8(char *x) {
	return strtod(x, NULL);
}

zion_float_t __float_from_utf32(wchar_t *x) {
	return wcstod(x, NULL);
}

zion_float_t __float_plus_float(zion_float_t x, zion_float_t y) {
	return x + y;
}

zion_float_t __float_minus_float(zion_float_t x, zion_float_t y) {
	return x - y;
}

zion_float_t __float_times_float(zion_float_t x, zion_float_t y) {
	return x * y;
}

zion_float_t __float_divide_float(zion_float_t x, zion_float_t y) {
	return x / y;
}

zion_float_t __int_plus_float(zion_int_t x, zion_float_t y) {
	return (zion_float_t)(x) + y;
}

zion_float_t __int_minus_float(zion_int_t x, zion_float_t y) {
	return (zion_float_t)(x) - y;
}

zion_float_t __int_times_float(zion_int_t x, zion_float_t y) {
	return (zion_float_t)(x) * y;
}

zion_float_t __int_divide_float(zion_int_t x, zion_float_t y) {
	return (zion_float_t)(x) / y;
}

zion_float_t __float_plus_int(zion_float_t x, zion_int_t y) {
	return y + (zion_float_t)x;
}

zion_float_t __float_minus_int(zion_float_t x, zion_int_t y) {
	return x - (zion_float_t)y;
}

zion_float_t __float_times_int(zion_float_t x, zion_int_t y) {
	return x * (zion_float_t)y;
}

zion_float_t __float_divide_int(zion_float_t x, zion_int_t y) {
	return x / (zion_float_t)y;
}

zion_bool_t __float_eq_float(zion_float_t x, zion_float_t y) {
	return x == y;
}

zion_bool_t __float_ineq_float(zion_float_t x, zion_float_t y) {
	return x != y;
}

zion_bool_t __float_gt_float(zion_float_t x, zion_float_t y) {
	return x > y;
}

zion_bool_t __float_gte_float(zion_float_t x, zion_float_t y) {
	return x >= y;
}

zion_bool_t __float_lt_float(zion_float_t x, zion_float_t y) {
	return x < y;
}

zion_bool_t __float_lte_float(zion_float_t x, zion_float_t y) {
	return x <= y;
}

zion_bool_t __float_lt_int(zion_float_t x, zion_int_t y) {
	return x < y;
}

zion_bool_t __float_lte_int(zion_float_t x, zion_int_t y) {
	return x <= y;
}
