#include <ddekit/initcall.h>
#include <ddekit/printf.h>
#if (__GNUC__ == 3 && __GNUC_MINOR__ >= 3) || __GNUC__ >= 4
#define SECTION(x)	__attribute__((used, section( x )))
#else
#define SECTION(x)	__attribute__((section( x )))
#endif

#define BEG		{ (ctor_hook) ~1UL }
#define END		{ (ctor_hook)  0UL }

typedef void (*const ctor_hook)(void);

#ifdef __LINUX_SOURCE__
static ctor_hook const __DDEKIT_CTOR_BEG__[1] SECTION(".mark_beg_dde_ctors") = BEG;
static ctor_hook const __DDEKIT_CTOR_END__[1] SECTION(".mark_end_dde_ctors") = END;
#elif __DARWIN_SOURCE__
static ctor_hook const __DDEKIT_CTOR_BEG__[1] __attribute__ (( section("__DATA, .beg_dde_ctors") )) = BEG;
static ctor_hook const __DDEKIT_CTOR_END__[1] __attribute__ (( section("__DATA, .end_dde_ctors") )) = END;
#endif

#define DEBUG 1

static void run_hooks_forward(ctor_hook *list, const char *name)
{
#ifdef DEBUG
  ddekit_print("list (forward) ");
  ddekit_printf("%s", name);
  ddekit_print(" @ ");
  ddekit_printf(" %p ", (void *)list);
  ddekit_print("\n");
#endif
  list++;
  while (*list)
    {
#ifdef DEBUG
      ddekit_print("  calling ");
      ddekit_printf("%p", (void *)list);
      ddekit_print("\n");
#endif
      (**list)();
      list++;
    }
}


void ddekit_do_initcalls(void) 
{
	run_hooks_forward(__DDEKIT_CTOR_BEG__, "__DDEKIT_CTOR_BEG__");
}
