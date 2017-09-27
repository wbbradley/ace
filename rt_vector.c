#include "zion_rt.h"

struct var_t *create_var(struct type_info_t *type_info);

struct __vector_t {
	/* the managed portion of the heap allocated object */
	struct var_t var;

	/* the vector-specific parts */
	zion_int_t reserved;
	zion_int_t size;
	struct var_t **items;
	// TODO: var_t _fast_items[DEFAULT_ARRAY_RESERVE];
};

struct __vector_t __example_vector = {0};

static struct type_info_t __vector_type_info = {
};

struct var_t *__vectorcreate__(type_id_t typeid, type_id_t element_typeid) {
	
	return create_var(&__vector_type_info);
}

void __vectorfree__(struct __vector_t *vector) {
	assert(vector != 0);

	/* the gc will handle cleaning up everything that we pointed to */
	free(vector->items);

	/* zion will handle deleting the actual __vector_t, since it will be attached to the managed
	 * object */
}

struct var_t *__getvectoritem__(struct __vector_t *vector, zion_int_t index) {
	if (index >= 0 && index < vector->size) {
		return vector->items[index];
	} else {
		printf("zion: array index out of bounds (0x%08llx[%lld])", (long long) vector,
			(long long)index);
		exit(-1);
		return 0;
	}
}

void __setvectoritem__(struct __vector_t *vector, zion_int_t index, struct var_t *item) {
	if (index < 0) {
	   return;
	}
	if (index < vector->size) {
		vector->items[index] = item;
	} else {
		printf("zion: array index out of bounds (0x%08llx[%lld])", (long long) vector,
			(long long)index);
		exit(-1);
	}
}

void __vectorappend__(struct __vector_t *vector, struct var_t *item) {
	if (vector->reserved > vector->size) {
		vector->items[vector->size] = item;
		vector->size += 1;
	} else if (vector->items != 0) {
		assert(vector->reserved == vector->size);
		zion_int_t new_reserved = vector->reserved * 2;
		if (new_reserved < 16) {
			/* start at a level that we avoid a lot of extra calls to malloc */
			new_reserved = 16;
		}

		struct var_t **new_items = (struct var_t **)calloc(sizeof(struct var_t *), new_reserved);
		memcpy(new_items, vector->items, sizeof(struct var_t *) * vector->size);
		new_items[vector->size] = item;
		vector->size += 1;

		free(vector->items);
		vector->items = new_items;
	}
}

