#include <sys/time.h>

#include "vm.h"
#include "private.h"

static int rtlb_c99_microtime(SpnValue *ret, int argc, SpnValue *argv, void *ctx)
{
	struct timeval tv;
	gettimeofday(&tv, NULL);
	*ret = makeint(1000000 * tv.tv_sec + tv.tv_usec);

	return 0;
}

#define SPN_LIBSIZE_C99 1
const SpnExtFunc spn_libc99[SPN_LIBSIZE_C99] = {
	{ "microtime",	rtlb_c99_microtime	},
};

void spn_libc99_load(SpnVMachine *vm)
{
	spn_vm_addlib_cfuncs(vm, NULL, spn_libc99, SPN_LIBSIZE_C99);
}

__attribute__((constructor)) void spn_libc99_c()
{
	/* Tell Sparkling to call spn_libc99_load when the standard library is loaded */
	spn_add_libld(spn_libc99_load);
}
