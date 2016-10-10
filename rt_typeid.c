/* BUILTINS
 * zion_int_t overloads
 * */
#include "zion_rt.h"

zion_int_t __type_id_ineq_type_id(type_id_t x, type_id_t y) {
	return x != y;
}

zion_int_t __type_id_eq_type_id(type_id_t x, type_id_t y) {
	return x == y;
}

zion_int_t __type_id_int(type_id_t x) {
	return (zion_int_t)x;
}
