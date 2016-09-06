/* BUILTINS
 * zion_list_t functions
 * */
#include "zion_rt.h"

struct builtin_list_t {
	builtin_list_t *next;

	//////////////////////////////////////
	// THE ACTUAL DATA IS APPENDED HERE //
	//////////////////////////////////////
};

struct var_t *__create_builtin_list(
		const char *name,
		size_t object_size)
