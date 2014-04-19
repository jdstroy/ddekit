#include <ddekit/panic.h>
#include <ddekit/printf.h>


#define _GNU_SOURCE
#include <dlfcn.h>

#include <execinfo.h>
#include <stdarg.h>
#include <stdlib.h>

void ddekit_panic(const char *fmt, ...) {
	va_list va;

	va_start(va, fmt);
	ddekit_vprintf(fmt, va);
	va_end(va);
	ddekit_printf("\n");

	ddekit_backtrace();

	ddekit_printf("ddekit_panic()\n");
	exit(1);
		//enter_kdebug();
}

void ddekit_debug(const char *fmt, ...) {
	va_list va;

	va_start(va, fmt);
	ddekit_vprintf(fmt, va);
	va_end(va);
//	ddekit_printf("\n");

//	ddekit_backtrace();
//	enter_kdebug("ddekit_debug()");
}

void ddekit_backtrace()
{
	int len = 16;
	void *array[len];
	char **symbols;
	unsigned i, ret;

	ret = backtrace(array, len);
	symbols = backtrace_symbols(array, ret);

	ddekit_printf("backtrace:\n");
	if(symbols) {
		for (i = 0; i < ret; ++i) {
			ddekit_printf("\t%s\n", symbols[i]);
		}
	}
}

void
ddekit_fun_to_name(void * const fun)
{
	Dl_info *info = NULL;

	if(dladdr(fun, info)) {
		ddekit_printf("symbol %p is from:\n", fun);
		ddekit_printf("file: %s loaded @%p\n", info->dli_fname, info->dli_fbase);
		ddekit_printf("symbol: %s loaded @%p\n", info->dli_sname, info->dli_saddr);
	} else {
		ddekit_printf("Error resolving symbol %p\n", fun);
	}
}
