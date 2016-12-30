/* the zion garbage collector */
#include "zion_rt.h"

const type_id_t SENTINEL_TYPE_ID = -1;
const type_id_t STACK_REF_TYPE_ID = -2;

typedef atomic_uint_fast64_t atomic_version_t;
typedef uint_fast64_t version_t;

typedef void (*mark_fn_t)(void *object, version_t version);

void mark_fn_default(void *object, version_t version) {
	/* do nothing */
}

struct next_var_t {
	/* avoid the ABA problem */
	uintptr_t id;

	/* here is the actual link to the next item */
	struct var_t *var;
};

struct var_t {
	atomic_version_t version;
	int16_t size;
	const char *name;
	type_id_t type_id;
	mark_fn_t mark_fn;
	struct next_var_t next_var;

	//////////////////////////////////////
	// THE ACTUAL DATA IS APPENDED HERE //
	//////////////////////////////////////
};

struct tag_t {
	atomic_version_t version;
	int16_t size;
	const char *name;
	type_id_t type_id;
};


/* An example tag (for use in examining LLIR)
 * Note that tag's data structure is identical to var_t up to type_id */

struct tag_t __tag_Example = {
	.version = 0,
	.size = 0,
	.name = "True",
	.type_id = 42,
};

struct var_t *Example = (struct var_t *)&__tag_Example;


#define VAR_DATA_ADDR(var) (((char *)(var)) + sizeof(var))

struct stack_ref_t {
	/* stack refs are immutable, once the stack is done with them, it puts them
	 * unaltered on the allocations list. this allows the gc to still traverse
	 * safely and mark its vars */
	struct var_t *self_var;
	struct var_t *var;
	struct stack_ref_t *next_stack_ref;
};

struct zion_thread_t {
	/* each thread will have a simple description */
	const char *thread_type;

	/* the head is the most recent allocation in the innermost scope of the
	 * stack. */
	_Atomic(struct stack_ref_t *) head_stack_ref;

	/* the head of the allocations, this may be accessed or modified by multiple threads */
	_Atomic(struct next_var_t) head_next_var;

	/* we'll have a chain of threads */
	_Atomic(struct zion_thread_t *) next_thread;

	/* the thread sentinel is only in existence during a garbage collection */
	struct var_t *sentinel_var;
};

static _Atomic(struct zion_thread_t *) head_thread = ATOMIC_VAR_INIT(NULL);
static _Atomic size_t _bytes_allocated = 0;

static void *mem_alloc(size_t cb) {
	size_t previous_total = atomic_load(&_bytes_allocated);
	while (!atomic_compare_exchange_weak(
				&_bytes_allocated, &previous_total, previous_total + cb)) {
	}

	// fprintf(stdout, "total allocated %lu\n", previous_total + cb);
	return calloc(cb, 1);
}

void mem_free(void *p, size_t cb) {
	size_t previous_total = atomic_load(&_bytes_allocated);
	while (!atomic_compare_exchange_weak(
				&_bytes_allocated, &previous_total, previous_total - cb)) {
	}

	free(p);
}

static pthread_key_t key;
static pthread_once_t key_once = PTHREAD_ONCE_INIT;

static void make_thread_key() {
    (void)pthread_key_create(&key, NULL);
}


intptr_t _init_thread(struct zion_thread_t *zion_thread) {
	fprintf(stderr, "Initializing a " c_id("%s") " thread...\n",
			zion_thread->thread_type);

    if (pthread_once(&key_once, make_thread_key) != 0) {
		fprintf(stderr, "pthread_once failed\n");
		return 1;
	}

    if (pthread_getspecific(key) == NULL) {
        pthread_setspecific(key, (void *)zion_thread);
    }
	return 0;
}

static struct zion_thread_t *zion_thread_create(const char *thread_type) {
	struct zion_thread_t *thread = (struct zion_thread_t *)mem_alloc(sizeof(struct zion_thread_t));
	thread->next_thread = head_thread;
	thread->thread_type = thread_type;

	// TODO: make this an atomic cmpxchg
	head_thread = thread;

	if (_init_thread(thread) != 0) {
		fprintf(stderr, "failed to initialize thread");
		exit(-1);
	}

	return head_thread;
}

struct zion_thread_t *get_zion_thread() {
	return (struct zion_thread_t *)pthread_getspecific(key);
}

static atomic_version_t atomic_version = 1;

uint_fast64_t get_atomic_version() {
	uint_fast64_t current_version = atomic_load(&atomic_version);
	uint_fast64_t next_version;

	do {
		next_version = current_version + 1;
	} while (!atomic_compare_exchange_weak(&atomic_version, &current_version, next_version));
	return next_version;
}

void print_var(const char *msg, struct var_t *var) {
	// TODO: warn if we try to do this on a non-gc thread (or fix it)
	fprintf(stdout, "%s var '" c_var("%s") "' size: %d (version %lld)\n", msg, var->name, var->size, var->version);
}

void print_stack(struct zion_thread_t *thread) {
	// TODO: warn if we try to do this on a non-gc thread (or fix it)
	struct stack_ref_t *stack_ref = atomic_load(&thread->head_stack_ref);

	int32_t stack_depth = 1;
	/* once a stack ref is loaded either by the owning stack thread, or the gc
	 * thread, it is valid to walk since other free threads are not allowed to
	 * see it, and it should still be alive on the local thread */
	while (stack_ref != NULL) {
		fprintf(stdout, "depth %d\n", stack_depth++);
		print_var(c_good(":"), stack_ref->self_var);
		print_var(c_var("="), stack_ref->var);
		stack_ref = stack_ref->next_stack_ref;
	}

	fprintf(stdout, "allocated vars:\n");
	struct next_var_t next_var = atomic_load(&thread->head_next_var);
	while (next_var.var != NULL) {
		print_var(c_unchecked("\\"), next_var.var);
		next_var = next_var.var->next_var;
	}

	// size_t total = atomic_load(&_bytes_allocated);
	// fprintf(stdout, "total allocated %lu\n", total);
}

type_id_t get_var_type_id(struct var_t *var) {
	return var->type_id;
}

struct var_t *create_var(
		const char *name,
		mark_fn_t mark_fn,
		type_id_t type_id,
		size_t object_size)
{
	/*compute the size of the allocation we'll want to do */

	// TODO: validate that this math isn't super sketchy and unaligned
	size_t size = sizeof(struct var_t) + object_size;

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

void push_stack_var(struct var_t *var) {
	/* the goal of this function is to push a variable onto our per-thread
	 * stack variables list */
	struct zion_thread_t *thread = get_zion_thread();

	/* first let's create our new stack ref object */

	/* the crux here is that we need to do this just like we'd allocate any
	 * other variable object. however, until the stack ref object is popped, we
	 * don't put it on the allocations list. this has a couple effects. 
	 * 1. it will not get deleted by the gc.
	 * 2. when we manipulate (pop stack refs) from the stack, we will not be
	 * immediately freeing that memory. instead, we'll just put those stack
	 * refs on the allocations list. then, after the gc is done traversing the
	 * (possibly stale) stack refs, in a later generation it will recycle their
	 * memory. */

	struct stack_ref_t stack_ref = {
		/* eventually we could get rid of this and just do pointer math, but
		 * for now i'm lazy */
		.self_var = NULL,

		/* have our stack ref point to the var we'd like to keep track of */
		.var = var,
		.next_stack_ref = thread->head_stack_ref,
	};

	// TODO: get rid of stack_ref var object, and just reuse the next_var space
	// on the actual var_t to serve two roles, depending on the status of the
	// object.
	struct var_t *stack_ref_var = create_var("stack ref", NULL /*mark_fn*/,
			STACK_REF_TYPE_ID, sizeof(struct stack_ref_t));
	memcpy(VAR_DATA_ADDR(stack_ref_var), &stack_ref, sizeof(struct stack_ref_t));


	/* hack this in here */
	((struct stack_ref_t *)VAR_DATA_ADDR(stack_ref_var))->self_var = stack_ref_var;

	/* there should be no one besides our current thread writing to this
	 * location, but the gc may be attempting to read from it */
	atomic_store(&thread->head_stack_ref, (void *)VAR_DATA_ADDR(stack_ref_var));

	if (atomic_load(&var->version) != 0) {
		fprintf(stderr, "var %s version should not be set to non-zero value "
				"prior to pushing the var on the stack.\n", var->name);
		exit(-1);
	}
	atomic_store(&var->version, get_atomic_version());
}

void add_to_thread_allocations(struct zion_thread_t *thread, struct var_t *var) {
	/* adding to the thread's allocations needs to be done with thread safety
	 * in mind because the garbage collector may come along and yank out
	 * variables at any moment */

	/* get the current head of the allocations list */
	struct next_var_t current_head_next_var = atomic_load(&thread->head_next_var);

	/* make space to prepare a new version of the allocations */
	struct next_var_t next_var;

	do {
		/* set up the new head */

		/* start by getting a unique value to avoid the ABA problem */
		next_var.id = get_atomic_version();

		/* have the new head point to our new allocation */
		next_var.var = var;

		/* have our new allocation link to the existing head */
		var->next_var = current_head_next_var;

	} while (!atomic_compare_exchange_weak(
				&thread->head_next_var,
			   	&current_head_next_var,
			   	next_var));
}

void pop_stack_var(struct var_t *var) {
	/* the goal of this function is to move a variable from being referenced on
	 * the per-thread stack to only being referenced by the allocations list */
	struct zion_thread_t *thread = get_zion_thread();

	/* get the handle to the top of the stack on this thread */
	_Atomic(struct stack_ref_t *) *head_stack_ref_handle = &thread->head_stack_ref;

	/* the goal of this function is to remove the var from the head of the list
	 * and add it to the allocations list so it can be tracked by the garbage
	 * collector */
	struct stack_ref_t *head_stack_ref = atomic_load(head_stack_ref_handle);
	if (head_stack_ref == NULL) {
		fprintf(stderr, "head of stack is NULL, instead of '%s'. FAIL.", var->name);
		exit(-1);
	}

	if (head_stack_ref->var != var) {
		fprintf(stderr, "head of stack should have been var '%s'. it is '%s'. "
				"FAIL.", var->name, head_stack_ref->var->name);
		exit(-1);
	}

	/* here we are certain that the top of the stack refers to "var." there
	 * should be no one besides our current thread writing to this location,
	 * but the gc may be attempting to read from it */
	atomic_store(head_stack_ref_handle, head_stack_ref->next_stack_ref);

	/* now that we've removed the stack ref from the list, we can go ahead and
	 * throw the stack ref itself onto the allocations list */
	add_to_thread_allocations(thread, head_stack_ref->self_var);

	/* currently no one can reference these stack references after they are
	 * popped so let's just use a really low version to ensure they get cleaned
	 * up post-haste */
	head_stack_ref->self_var->version = 1;

	/* now that we've got no stack object tracking this var we'll need to track
	 * it in the allocations list. note that the variable might still be
	 * referenced by some other data-structure somewhere, so we do not touch
	 * the variable's version. */
	add_to_thread_allocations(thread, head_stack_ref->var);
}

void mark_stack_var(uint_fast64_t version, struct stack_ref_t *stack_ref) {
	// TODO: never try to do this on a non-gc thread
	struct var_t *var = stack_ref->var;

	if (var->version == 0) {
		fprintf(stderr, "the gc should never see unversioned variables in the stack");
		exit(-1);
	}

	/* update the version of this object */
	var->version = version;

	if (var->mark_fn != NULL) {
		/* if this object has children, let's mark them */
		(*var->mark_fn)(VAR_DATA_ADDR(var), version);
	}

	print_var(c_var("marked"), var);
}

void *gc(void *context) {
	int32_t freed_objects = 0;

	/* name this thread as the gc thread */
	pthread_setname_np("gc");

	/* begin by getting a base version, after which everything created or marked
	 * is safe from deletion */
	atomic_version_t gc_version = get_atomic_version();

	/* get the current head thread */
	struct zion_thread_t *gc_head_thread = atomic_load(&head_thread);
	struct zion_thread_t *thread = gc_head_thread;

	fprintf(stdout, "gc - generation " c_id("%lld") "\n", gc_version);
	/* walk over all threads */
	while (thread != NULL) {
		/* create a sentinel variable, and add it to the allocations list. use
		 * that as a starting place for the subsequent mark and sweep */
		struct var_t *var = create_var(c_internal("sentinel"), NULL /*mark_fn*/,
			   	SENTINEL_TYPE_ID, 0 /*size*/);

		/* make sure the sentinel has a version number */
		var->version = gc_version;

		/* add the sentinel to the head of the allocations. we'll use it as a
		 * placeholder in the sweep loop. */
		add_to_thread_allocations(thread, var);

		if (thread->sentinel_var != NULL) {
			fprintf(stderr, "we should not have a known sentinel at the beginning of the gc\n");
			exit(-1);
		}

		/* set this sentinel var up as this thread's current sentinel */
		thread->sentinel_var = var;

		/* walk over the stack of the thread under inspection, starting at the top */
		struct stack_ref_t *stack_ref = atomic_load(&thread->head_stack_ref);
		while (stack_ref != NULL) {
			mark_stack_var(gc_version, stack_ref);
			stack_ref = stack_ref->next_stack_ref;
		}
		thread = thread->next_thread;
	}

	/* start over at the first thread that existed when we began gc */
	thread = gc_head_thread;

	/* walk again over all threads' allocations list, freeing all the
	 * unreferenced stuff */
	while (thread != NULL) {
		/* get the location of the sentinel's next_var pointer */
		struct next_var_t *next_var_handle = &thread->sentinel_var->next_var;

		/* loop over all of the allocations, deciding whether to free them */
		while (next_var_handle->var != NULL) {
			version_t version = atomic_load(&next_var_handle->var->version);

			if (version == 0) {
				/* items with a zero version should not be placed on the
				 * allocations list, this would indicate a bug */
				print_var(c_error("zero version") " found in the allocations list", next_var_handle->var);
				exit(-1);
			} else if (version < gc_version) {
				/* this object is old and stale despite our mark pass above */
				print_var(c_internal("freeing"), next_var_handle->var);

				/* stash the var that were going to free */
				struct var_t *var_to_free = next_var_handle->var;

				/* skip the var to free by removing it from the list */
				*next_var_handle = var_to_free->next_var;

				// TODO: probably we don't need this?
				free((void *)var_to_free->name);

				/* actually free the memory */
				mem_free(var_to_free, var_to_free->size);

				freed_objects += 1;

				/* the loop was advanced by removing the current var */
			} else {
				print_var(c_good("survived"), next_var_handle->var);

				/* advance the loop */
				next_var_handle = &next_var_handle->var->next_var;
			}

		}

		/* the sentinel should get garbage collected on the next pass */
		thread->sentinel_var = NULL;

		/* advance to the next thread */
		thread = thread->next_thread;
	}

	fprintf(stdout, "gc done.\nfreed %d objects.\n", freed_objects);
	return NULL;
}

#ifdef RT_GC_TEST
int32_t main(int32_t argc, char *argv[]) {
	/* name this thread as the gc thread */
	pthread_setname_np("main");

	/* set up this thread */
	struct zion_thread_t *main_thread = zion_thread_create("main");

	/* emulate a variable coming into scope */

	char *line = NULL;
	size_t linecap = 0;
	ssize_t linelen = 0;

	struct var_t *var = NULL;

	// new x := 5
	while (1) {
		fprintf(stdout, "> ");
		linelen = getline(&line, &linecap, stdin);
		if (linelen >= 0) {
			if (strlen(line) >= 1) {
				line[strlen(line) - 1] = 0;
			}

			if (strcmp(line, "pop") == 0) {
				struct stack_ref_t *head_stack_ref = atomic_load(&main_thread->head_stack_ref);
				if (head_stack_ref != NULL) {
					pop_stack_var(head_stack_ref->var);
				}
			} else if (strcmp(line, "gc") == 0) {
				pthread_t thread = {0};

				pthread_create(&thread, NULL, &gc, NULL); 
			} else {
				/* create the variable */
				zion_int_t value = get_atomic_version();
				var = create_var(line, NULL /*mark_fn*/, 1 /* type_id (fake) */,
					   	sizeof(zion_int_t));

				/* put the actual value into our newly minted variable */
				memcpy(VAR_DATA_ADDR(var), &value, sizeof(zion_int_t));

				/* track it as existing on the stack */
				push_stack_var(var);
			}

			print_stack(main_thread);
		} else {
			break;
		}
	}

	if (line) {
		/* not really necessary, but just for cleanliness */
		free(line);
		line = NULL;
		fprintf(stdout, "\n");
	}

	return EXIT_SUCCESS;
}
#endif
