#include "zion_rt.h"

struct __vector_t {
	/* the managed portion of the heap allocated object */
	VAR_CONTENTS;

	/* the vector-specific parts */
	zion_int_t reserved;
	zion_int_t size;
	struct var_t **items;
	// TODO: var_t _fast_items[DEFAULT_ARRAY_RESERVE];
};

struct __vector_t __example_vector = {0};

void __vectorfree__(struct var_t *var) {
	assert(var != 0);
	struct __vector_t *vector = (struct __vector_t *)var;

	/* the gc will handle cleaning up everything that we pointed to, but we need to free up our extra allocated heap
	 * space. */
	free(vector->items);

	/* zion will handle deleting the actual __vector_t, since it will be attached to the managed
	 * object */
}

void mark_allocation(struct var_t *var);

void __vectormark__(struct var_t *var) {
	assert(var != 0);
	struct __vector_t *vector = (struct __vector_t *)var;
	struct var_t **items = vector->items;
	for (zion_int_t index = 0, end = vector->size; index < end; ++index) {
		mark_allocation(items[index]);
	}
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

void __vectorappend__(struct var_t *vector_, struct var_t *item) {
	struct __vector_t *vector = (struct __vector_t *)vector_;
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

