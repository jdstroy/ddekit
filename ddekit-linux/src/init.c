/**
 * The functions regarding DDE/BSD initialization are found here.
 *
 * \author Thomas Friebel <tf13@os.inf.tu-dresden.de>
 */
#include <ddekit/panic.h>
#include <ddekit/thread.h>
#include <ddekit/memory.h>
#include <ddekit/interrupt.h>
#include <ddekit/printf.h>
//#include <dde.h>

#include <stdlib.h>

#if 0
/* FIXME this must be initialized explicitly as some users may not need l4io,
 * e.g., l4io's own pcilib. */
static void ddekit_init_l4io(void)
{
	int err;
	l4io_info_t *ioip = NULL;

	LOGd(0, "mapping io info page to %p", ioip);
	err = l4io_init(&ioip, L4IO_DRV_INVALID);
	if ( err | !ioip ) {
		LOG("error initializing io lib: %s (err=%d, ioip=%p)", l4env_errstr(err), err, ioip);
		ddekit_panic("fatal error");
	}
}
#endif

extern void ddekit_mem_init(void);
extern void ddekit_init_threads(void);
extern void ddekit_init_irqs(void);
extern void ddekit_pgtab_init(void);
extern void ddekit_pci_init(void);
extern void ddekit_init_timers(void);

extern void ddekit_dma_init(void);

void (*handler)(void*) = (void*)0;
void *argument = (void *)0;

void ddekit_deinit(void)
{
	ddekit_printf("calling handler\n");
	if(handler != (void*)0)
		handler(argument);
	ddekit_printf("handler done\n");
}

void ddekit_register_exit_handler(void (*fun)(void*), void *arg)
{
	handler = fun;
	argument = arg;
}

void ddekit_init(void)
{
	atexit(ddekit_deinit);
	ddekit_pci_init();
	ddekit_mem_init();
	ddekit_pgtab_init();
	ddekit_init_threads();
	ddekit_dma_init();
	ddekit_init_irqs();
	ddekit_init_timers();
}

