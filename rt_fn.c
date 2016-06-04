#include "zion_rt.h"

void *call_fn(void *(foo)(void *)) {
	return foo(NULL);
}
