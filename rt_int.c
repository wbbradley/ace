/* BUILTINS
 * zion_int_t overloads
 * */
#include "zion_rt.h"

zion_int_t __int_int32(int32_t x) {
    return x;
}

int32_t __int32_int(zion_int_t x) {
    return x;
}

zion_int_t __int_int16(int16_t x) {
    return x;
}

int16_t __int16_int(zion_int_t x) {
    return x;
}

zion_int_t __int_not(zion_int_t x) {
	return !x;
}

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

zion_int_t __int_from_utf8(char *x) {
	return strtol(x, NULL, 10);
}

zion_int_t __int_from_utf32(wchar_t *x) {
	return wcstoll(x, NULL, 10);
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

struct TI {
	int32_t type_id;
	int32_t type_kind;
	int64_t size;
	char *name;
};

struct VT {
	struct TI * type_info;
	int64_t mark;
	struct VT *next;
	struct VT *prev;
	int64_t allocation;
	int64_t data;
};

struct SFM {
	int32_t num_roots;
	int32_t num_meta;
	void *metadata;
};

struct SE {
	struct SE * next;
	struct SFM * map;
	struct VT *root[0];
};

void dbg_ti(struct TI *ti) {
	printf("type_id:\t%lld\ntype_kind:\t%lld\nsize:\t%lld\nname:\t%s\n",
			(long long)ti->type_id,
			(long long)ti->type_kind,
			(long long)ti->size,
			ti->name);
}

void dbg_vt(struct VT *vt) {
	dbg_ti(vt->type_info);
	printf("mark:\t%lld\n", (long long)vt->mark);
	printf("next:\t0x%08llx\n", (long long)vt->next);
	printf("prev:\t0x%08llx\n", (long long)vt->prev);
	printf("allocation:\t%lld\n", (long long)vt->allocation);
}

void dbg_se(void *p) {
	struct SE *se = p;
	if (se) {
		assert(se->map);
		printf("stack entry: (next: 0x%08llx, map: 0x%08llx {%lld roots})\n",
				(long long)se->next, (long long)se->map, (long long)se->map->num_roots);
		for (int64_t i=0; i < se->map->num_roots; ++i) {
			printf("root[%lld]: 0x%08llx\n", (long long)i, (long long)se->root[i]);
			if (se->root[i]) {
				dbg_vt(se->root[i]);
			}
		}
		if (se->next) {
			dbg_se(se->next);
		}
	}
}

