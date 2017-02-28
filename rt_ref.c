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
	int64_t size;
};

struct var_t {
	/* each runtime variable has a pointer to its type info */
	struct type_info_t *type_info;

	/* and a ref-count of its own */
	zion_int_t ref_count;

	//////////////////////////////////////
	// THE ACTUAL DATA IS APPENDED HERE //
	//////////////////////////////////////
};

struct tag_t {
	struct type_info_t *type_info;

	/* tags don't have refcounts - as described in their refs_count of -1 */
};


/* An example tag (for use in examining LLIR) */

int16_t test_array[] = {
	2, 3, 4
};

struct type_info_t __tag_type_info_Example = {
	.type_id = 42,
	.name = "True",
	.refs_count = 3,
	.ref_offsets = test_array,
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

const char *_zion_rt = "zion-rt: ";

#define MEM_PANIC(msg, str, error_code) \
	do { \
		write(2, _zion_rt, strlen(_zion_rt)); \
		write(2, msg, strlen(msg)); \
		write(2, str, strlen(str)); \
		write(2, "\n", 1); \
		exit(error_code); \
	} while (0)


void addref_var(struct var_t *var) {
	if (var == 0) {
		MEM_PANIC("attempt to addref a null value", "", 111);
	} else if (var->type_info == 0) {
		MEM_PANIC("attempt to addref a value with a null type_info", "", 111);
	} else if (var->type_info->refs_count >= 0) {
		++var->ref_count;
		printf("addref %s 0x%08lx to (%ld)\n", var->type_info->name, (intptr_t)var, var->ref_count);
	} else {
		printf("attempt to addref a singleton of type %s\n", var->type_info->name);
	}
}

void release_var(struct var_t *var) {
	printf("attempt to release var 0x%08lx\n", (intptr_t)var);
	if (var->type_info->refs_count >= 0) {
		if (var->ref_count <= 0) {
			MEM_PANIC("invalid release (bad ref_count) ", var->type_info->name, 113);
		}

		// TODO: atomicize for multi-threaded purposes
		--var->ref_count;
		printf("release %s 0x%08lx to (%ld)\n", var->type_info->name, (intptr_t)var, var->ref_count);

		if (var->ref_count == 0) {
			for (int16_t i = var->type_info->refs_count - 1; i >= 0; --i) {
				struct var_t *ref = (struct var_t *)(((char *)var) + sizeof(struct var_t));
				release_var(ref);
			}
			printf("freeing %s 0x%08lx\n", var->type_info->name, (intptr_t)var);
			mem_free(var, var->type_info->size);
		}
	} else {
		printf("attempt to release a singleton of type %s\n", var->type_info->name);
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
	struct var_t *var = (struct var_t *)mem_alloc(type_info->size);
	var->type_info = type_info;
	var->ref_count = 1;
	printf("creating %s 0x%08lx at (%ld)\n", type_info->name, (intptr_t)var, var->ref_count);
	return var;
}
