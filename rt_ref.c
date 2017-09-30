/* the zion garbage collector */
#include <assert.h>
#include <signal.h>
#include "zion_rt.h"

const uint32_t TYPE_ID_VECTOR = -2;

#define GET_CHILD_REF(var, index) \
	(*(struct var_t **)(((char *)var) + \
						((struct type_info_offsets_t *)var->type_info)->ref_offsets[index]))

struct var_t {
	VAR_CONTENTS;

	//////////////////////////////////////
	// THE ACTUAL DATA IS APPENDED HERE //
	//////////////////////////////////////
};

struct type_info_t {
	TYPE_INFO_HEADER;
};

struct type_info_offsets_t {
	TYPE_INFO_HEADER;

	/* the destructor for this type, if one exists. NB: if you change the index
	 * of this dimension, update DTOR_INDEX */
	dtor_fn_t dtor_fn;

	/* refs_count gives the type-map for memory management/ref counting. */
	int16_t refs_count;

	/* ref_offsets is the list of offsets to managed members */
	int16_t *ref_offsets;
};

struct type_info_mark_fn_t {
	TYPE_INFO_HEADER;

	/* the destructor for this type, if one exists. NB: if you change the index
	 * of this dimension, update DTOR_INDEX */
	dtor_fn_t dtor_fn;

	/* the mark function for this type, if one exists. NB: if you change the index
	 * of this dimension, update MARK_FN_INDEX */
	mark_fn_t mark_fn;
};

struct tag_t {
	struct type_info_t *type_info;

	/* tags don't have refcounts - as described in their refs_count of -1 */
};

struct type_info_offsets_t __type_info_Offsets = {
	.type_id = 42,
	.size = sizeof(struct type_info_offsets_t) + 0,
	.type_kind = type_kind_use_offsets,
	.name = "example-1",
	.refs_count = 0,
	.ref_offsets = 0,
	.dtor_fn = 0,
};

struct type_info_mark_fn_t __type_info_MarkFn = {
	.type_id = 43,
	.size = sizeof(struct type_info_mark_fn_t) + 0,
	.type_kind = type_kind_use_mark_fn,
	.name = "example-2",
	.dtor_fn = 0,
	.mark_fn = 0,
};

struct tag_t __tag_Example = {
	.type_info = (struct type_info_t *)&__type_info_Offsets,
};

struct var_t *Example = (struct var_t *)&__tag_Example;



static size_t _bytes_allocated = 0;
static size_t _all_bytes_allocated = 0;

void *mem_alloc(zion_int_t cb) {
	_bytes_allocated += cb;
	_all_bytes_allocated += cb;
#ifdef MEMORY_DEBUGGING
	printf("memory allocation is at %ld %ld\n", _bytes_allocated,
			_all_bytes_allocated);
#endif

	return calloc(cb, 1);
}

void mem_free(void *p, size_t cb) {
	_bytes_allocated -= cb;
	free(p);
#ifdef MEMORY_DEBUGGING
	printf("memory allocation is at %ld %ld\n", _bytes_allocated,
			_all_bytes_allocated);
#endif
}

zion_int_t get_total_allocated() {
	return _bytes_allocated;
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


struct var_t head_var = {
	.type_info = 0,
	.ref_count = 1,
	.allocation = 0,
	.next = 0,
	.prev = 0,
};

void check_node_existence(struct var_t *node, zion_bool_t should_exist) {
	struct var_t *p = &head_var;
	assert(p->prev == 0);

	if (should_exist) {
		assert(p->next != 0);
		assert(node != 0);
		assert(node->prev != (struct var_t *)0xdeadbeef && node->next != (struct var_t *)0xdeadbeef);
		assert(node->prev != 0);
	}

	while (p != 0) {
		if (p == node) {
			if (!should_exist) {
				printf("node 0x%08lx of type %s already exists!\n",
						(intptr_t)node, node->type_info->name);
				assert(should_exist);
			} else {
				/* found it, and that's expected. */
				return;
			}
		}
		p = p->next;
	}

	if (should_exist) {
#ifdef MEMORY_DEBUGGING
		printf("node 0x%08lx #%lld of type %s does not exist in memory tracking list!\n",
				(intptr_t)node, (long long)node->allocation, node->type_info->name);
#endif
		assert(!should_exist);
	}
}

void add_node(struct var_t *node) {
	assert(node->ref_count == 1);

	check_node_existence(node, 0 /* should_exist */);

	if (node->prev != 0 || node->next != 0) {
		printf("node 0x%08lx #%lld of type %s already has prev and next ptrs?!\n",
				(intptr_t)node, (long long)node->allocation, node->type_info->name);
		exit(-1);
	}

	assert(!head_var.next || head_var.next->prev == &head_var);

	node->prev = &head_var;
	node->next = head_var.next;
	if (node->next != 0) {
		node->next->prev = node;
	}
	head_var.next = node;

	assert(head_var.prev == 0);
	assert(head_var.next->prev == &head_var);
	assert(node->prev->next == node);
	assert(!node->next || node->next->prev == node);

	check_node_existence(node, 1 /* should_exist */);
}

void remove_node(struct var_t *node) {
#ifdef MEMORY_DEBUGGING
	printf("removing node 0x%08llx %s\n",
		   	(long long)node, node->type_info->name);
#endif
	assert(node->ref_count == 0 || node->mark == 0);

	check_node_existence(node, 1 /* should_exist */);

	assert(node->prev->next == node);
	assert(!node->next || node->next->prev == node);

	node->prev->next = node->next;
	if (node->next != 0) {
		node->next->prev = node->prev;
	}
	node->next = (struct var_t *)0xdeadbeef;
	node->prev = (struct var_t *)0xdeadbeef;

	check_node_existence(node, 0 /* should_exist */);
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

int64_t _allocation = 1;

struct var_t *create_var(struct type_info_t *type_info) {
	/* allocate the variable tracking object */
	struct var_t *var = (struct var_t *)mem_alloc(type_info->size);
	var->type_info = type_info;
	var->ref_count = 1;

	/* track this allocation */
	var->allocation = _allocation;
	_allocation += 1;

	add_node(var);

#ifdef MEMORY_DEBUGGING
	printf("creating %s #%lld 0x%08lx\n", type_info->name, var->allocation, (intptr_t)var);
#endif

	return var;
}

/*  The map for a single function's stack frame.  One of these is
 *  compiled as constant data into the executable for each function.
 * 
 *  Storage of metadata values is elided if the %metadata parameter to
 *  @llvm.gcroot is null. */
struct stack_frame_map_t {
	int32_t num_roots;    //< Number of roots in stack frame.
	int32_t num_meta;     //< Number of metadata entries.  May be < num_roots.
	const void *meta[0];  //< Metadata for each root.
};

/*  A link in the dynamic shadow stack.  One of these is embedded in
 *  the stack frame of each function on the call stack. */
struct llvm_stack_entry_t {
	struct llvm_stack_entry_t *next;     //< Link to next stack entry (the caller's).
	const struct stack_frame_map_t *map; //< Pointer to constant stack_frame_map_t.
	struct var_t *stack_roots[0];        //< Stack roots (in-place array).
};

/*  The head of the singly-linked list of llvm_stack_entry_t's.  Functions push and pop onto this in
 *  their prologue and epilogue.
 * 
 *  Since there is only a global list, this technique is not threadsafe. */
struct llvm_stack_entry_t *llvm_gc_root_chain;

/* Calls heap_visit(root, meta) for each GC root on the stack.
 *        root and meta are exactly the values passed to
 *        @llvm.gcroot.
 *
 * heap_visit could be a function to recursively mark live objects.  Or it
 * might copy them to another heap or generation.
 *
 * @param heap_visit A function to invoke for every GC root on the stack. */
void visit_heap_roots(void (*heap_visit)(struct var_t *var)) {
	for (struct llvm_stack_entry_t *R = llvm_gc_root_chain; R; R = R->next) {
		if (R->map->num_meta != 0) {
			raise(SIGTRAP);
		}

		// For roots [num_meta, num_roots), the metadata pointer is null.
		for (unsigned i = 0, e = R->map->num_roots; i != e; ++i) {
			/* we have a heap variable */
			heap_visit(R->stack_roots[i]);
		}
	}
}

void visit_allocations(void (*visit)(struct var_t *var)) {
	struct var_t *node = head_var.next;
	while (node != 0) {
		/* cache the next node in case our current node gets deleted as part of the fn */
		struct var_t *next = node->next;

		/* visit the node */
		visit(node);

		/* move along */
		node = next;
	}
}

void mark_allocation(struct var_t *var) {
	if (var != 0) {
#ifdef MEMORY_DEBUGGING
		printf("heap variable is referenced on the stack at 0x%08llx and is a '%s'\n", (long long)var, var->type_info->name);
#endif
		if (var->mark == 0) {
			/* mark this node in the heap so that we break any potential cycles */
			var->mark = 1;

#ifdef MEMORY_DEBUGGING
			printf("marking heap variable at 0x%08llx '%s'\n", (long long)var,
					var->type_info->name);
#endif

			assert(var->type_info);

			type_kind_t type_kind = var->type_info->type_kind;
			if (type_kind == type_kind_tag) {
				/* tags don't have dependencies */
				return;
			} else if (var->type_info->type_kind == type_kind_use_offsets) {
				struct type_info_offsets_t *type_info_offsets = (struct type_info_offsets_t *)var->type_info;

				/* we may be holding on to child nodes, let's recurse. */
				int16_t refs_count = type_info_offsets->refs_count;

				for (int16_t j = 0; j < refs_count; ++j) {
					/* compute the offset to this referenced dimension */
					struct var_t *child = GET_CHILD_REF(var, j);
					mark_allocation(child);
				}
			} else if (var->type_info->type_kind == type_kind_use_mark_fn) {
				/* call the type's mark function to recurse */
				((struct type_info_mark_fn_t *)var->type_info)->mark_fn(var);
			} else {
				 assert(0 && "found a heap variable with an invalid type_kind");
			}
		}
	}
}

void clear_mark_bit(struct var_t *var) {
	/* this is highly inefficient due to cache non-locality, revisit later */
	var->mark = 0;
}

void free_unmarked(struct var_t *var) {
	assert(var != &head_var);
	if (var->mark == 0) {
		remove_node(var);
		mem_free(var, var->type_info->size);
	}
}

void gc() {
	visit_allocations(clear_mark_bit);
	visit_heap_roots(mark_allocation);
	visit_allocations(free_unmarked);
}

void print_var(struct var_t *node) {
	printf("heap variable is still allocated at 0x%08llx and is a '%s'\n", (long long)node,
			node->type_info->name);
}

extern void heap_dump() {
	visit_allocations(print_var);
}
