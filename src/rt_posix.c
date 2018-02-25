#include <errno.h>
#include "zion_rt.h"

zion_int_t __posix_errno() {
	return errno;
}
