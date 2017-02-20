/* the zion garbage collector */
#include "zion_rt.h"

struct type_info_t {
	/* the id for the type - a unique number */
	type_id_t type_id;

	/* refs_count gives the type-map for memory management/ref counting. */
	int16_t refs_count;

	/* ref_offsets is the list of offsets to managed members */
	int16_t *ref_offsets;

	/* a helpful name for this type */
	const char *name;

	/* the size of the allocation for memory profiling purposes */
	int16_t size;
};

struct var_t {
	/* each runtime variable has a pointer to its type info */
	struct type_info_t *type_info;

	/* and a ref-count of its own */
	zion_int_t refcount;

	//////////////////////////////////////
	// THE ACTUAL DATA IS APPENDED HERE //
	//////////////////////////////////////
};

struct tag_t {
	struct type_info_t *type_info;

	/* tags don't have refcounts - as described in their refs_count of -1 */
};


/* An example tag (for use in examining LLIR) */

struct type_info_t __tag_type_info_Example = {
	.type_id = 42,
	.name = "True",
	.refs_count = -1,
	.ref_offsets = NULL,
	.size = 0,
};

struct tag_t __tag_Example = {
	.type_info = &__tag_type_info_Example,
};

struct var_t *Example = (struct var_t *)&__tag_Example;


static size_t _bytes_allocated = 0;

void *mem_alloc(zion_int_t cb) {
	_bytes_allocated += cb;

	// fprintf(stdout, "total allocated %lu\n", previous_total + cb);
	return calloc(cb, 1);
}

void mem_free(void *p, size_t cb) {
	_bytes_allocated -= cb;

	free(p);
}

#define MEM_PANIC(msg, str, error_code) \
	do { \
		write(2, msg, strlen(msg)); \
		write(2, str, strlen(str)); \
		write(2, "\n", 1); \
		exit(error_code); \
	} while (0)


void addref_var(struct var_t *var) {
	if (var->type_info->refs_count >= 0) {
		// TODO: atomicize for multi-threaded purposes
		++var->refcount;
	} else {
		MEM_PANIC("attempt to addref a singleton of type ", var->type_info->name, 111);
	}
}

void construct_var(struct var_t *var) {
	if (var->type_info->refs_count >= 0) {
		if (var->refcount != 0) {
			MEM_PANIC("invalid construction (bad refcount) ", var->type_info->name, 114);
		}

		var->refcount = 1;

		/* increment the refcounts for all subordinate objects */
		for (int16_t i = var->type_info->refs_count - 1; i >= 0; --i) {
			struct var_t *ref = (struct var_t *)(((char *)var) + sizeof(struct var_t));
			addref_var(ref);
		}
	} else {
		MEM_PANIC("attempt to construct a singleton of type ", var->type_info->name, 115);
	}
}

void release_var(struct var_t *var) {
	if (var->type_info->refs_count >= 0) {
		if (var->refcount <= 0) {
			MEM_PANIC("invalid release (bad refcount) ", var->type_info->name, 113);
		}

		// TODO: atomicize for multi-threaded purposes
		--var->refcount;

		if (var->refcount == 0) {
			for (int16_t i = var->type_info->refs_count - 1; i >= 0; --i) {
				struct var_t *ref = (struct var_t *)(((char *)var) + sizeof(struct var_t));
				release_var(ref);
			}
			mem_free(var, var->type_info->size);
		}
	} else {
		MEM_PANIC("attempt to release a singleton of type ", var->type_info->name, 112);
	}
}

zion_bool_t isnil(struct var_t *p) {
    return p == 0;
}

type_id_t get_var_type_id(struct var_t *var) {
    if (var != 0) {
        return var->type_info->type_id;
    } else {
		MEM_PANIC("attempt to get_var_type_id of a null value ", "", 116);
        return 0;
    }
}

struct var_t *create_var(struct type_info_t *type_info)
{
	/* allocate the variable tracking object */
	return (struct var_t *)mem_alloc(type_info->size);
}
