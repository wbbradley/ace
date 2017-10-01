#pragma once

/* for now let's go big and not worry about it */
typedef double zion_float_t;

#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdatomic.h>
#include <pthread.h>
#include <assert.h>
#include <signal.h>
#include "colors.h"
#include "type_kind.h"

typedef int64_t zion_int_t;
typedef int64_t zion_bool_t;
typedef int32_t type_id_t;
typedef int32_t type_kind_t;

#define int do_not_use_int
#define float do_not_use_float
#define double do_not_use_double

struct var_t;
struct type_info_t;
struct type_info_offsets_t;
struct type_info_mark_fn_t;

typedef void (*dtor_fn_t)(struct var_t *var);
typedef void (*mark_fn_t)(struct var_t *var);
typedef void (*void_fn_t)(void *var);

#define TYPE_INFO_HEADER \
	/* the id for the type - a unique number */ \
	type_id_t type_id; \
	/* the size of this managed heap allocation */ \
	int64_t size; \
	/* discern how memory management should perform marks */ \
	type_kind_t type_kind;  \
	/* a helpful name for this type */ \
	const char *name;

#define VAR_CONTENTS \
	/* each runtime variable has a pointer to its type info */ \
	struct type_info_t *type_info; \
	/* and a ref-count of its own */ \
	int32_t ref_count : 31; \
	int32_t mark      :  1; \
	struct var_t *next; \
	struct var_t *prev; \
	int64_t allocation

extern zion_bool_t __debug_zion_runtime;
#define dbg_zrt(x) if (__debug_zion_runtime) { (x); } else {}
