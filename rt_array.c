#include "zion_rt.h"

struct array_t {
	zion_int_t reserved;
	zion_int_t size;
	struct var_t **items;
};

struct var_t *__getarrayitem__(struct array_t *array, zion_int_t index) {
	if (index >= 0 && index < array->size) {
		return array->items[index];
	} else {
		// TODO: revisit array indexing out-of-bounds errors
		return 0;
	}
}

void __setarrayitem__(struct array_t *array, zion_int_t index, struct var_t *item) {
	if (index < 0) {
	   return;
	}
	if (index < array->size) {
		array->items[index] = item;
	} else {
		// TODO: revisit array indexing out-of-bounds errors
	}
}

void __arrayappend__(struct array_t *array, struct var_t *item) {
	if (array->reserved > array->size) {
		array->items[array->size] = item;
		array->size += 1;
	} else if (array->items != 0) {
		assert(array->reserved > 0);
		zion_int_t new_reserved = array->reserved * 3 / 2 + 1;
		struct var_t **new_items = (struct var_t **)calloc(sizeof(struct var_t *), new_reserved);
		memcpy(new_items, array->items, sizeof(struct var_t *) * array->size);
		new_items[array->size] = item;
		array->size += 1;
		free(array->items);
		array->items = new_items;
	}
}
