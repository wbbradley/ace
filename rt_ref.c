/* the zion garbage collector */
#include "zion_rt.h"


typedef void (*mark_fn_t)(void *object);

void mark_fn_default(void *object) {
	/* do nothing */
}

struct var_t {
	int16_t size;
	const char *name;
	type_id_t type_id;
	mark_fn_t mark_fn;

	//////////////////////////////////////
	// THE ACTUAL DATA IS APPENDED HERE //
	//////////////////////////////////////
};

struct tag_t {
	int16_t size;
	const char *name;
	type_id_t type_id;
};


/* An example tag (for use in examining LLIR)
 * Note that tag's data structure is identical to var_t up to type_id */

struct tag_t __tag_Example = {
	.size = 0,
	.name = "True",
	.type_id = 42,
};

struct var_t *Example = (struct var_t *)&__tag_Example;


#define VAR_DATA_ADDR(var) (((char *)(var)) + sizeof(var))

zion_bool_t __isnil(struct var_t *p) {
    return p == 0;
}

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

type_id_t get_var_type_id(struct var_t *var) {
    if (var != 0) {
        return var->type_id;
    } else {
        return 0;
    }
}

struct var_t *create_var(
		const char *name,
		mark_fn_t mark_fn,
		type_id_t type_id,
		uint64_t object_size)
{
	/* compute the size of the allocation we'll want to do */

	// TODO: validate that this math isn't super sketchy and unaligned
	size_t size = /* sizeof(struct var_t) + */  object_size;

	/* allocate the variable tracking object */
	struct var_t *var = (struct var_t *)mem_alloc(size);

	/* set up the variable's size info */
	// TODO: consider whether we want to store the actual size pointed to by
	// var_t, or just the user program's notion of the size
	var->size = size;

	/* give it a name */
	var->name = strdup(name);

	/* store the type identity */
	var->type_id = type_id;

	/* store the GC memory marking function */
	var->mark_fn = mark_fn;

	return var;
}
