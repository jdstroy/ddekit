/*
 * \brief   Hardware-interrupt subsystem
 * \author  Thomas Friebel <tf13@os.inf.tu-dresden.de>
 * \author  Christian Helmuth <ch12@os.inf.tu-dresden.de>
 * \date    2007-01-22
 *
 * FIXME could intloop_param freed after startup?
 * FIXME use consume flag to indicate IRQ was handled
 */
#ifdef __OPTIMIZE__
//#undef __OPTIMIZE__
#endif
#include <ddekit/interrupt.h>
#include <ddekit/semaphore.h>
#include <ddekit/thread.h>
#include <ddekit/memory.h>
#include <ddekit/panic.h>
#include <ddekit/printf.h>

#define _XOPEN_SOURCE 500

#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <pthread.h>

#define MAX_INTERRUPTS   128

#define DEBUG_INTERRUPTS  1
/*
 * Internal type for interrupt loop parameters
 */
struct intloop_params
{
	unsigned          irq;       /* irq number */
	int               shared;    /* irq sharing supported? */
	void(*thread_init)(void *);  /* thread initialization */
	void(*handler)(void *);      /* IRQ handler function */
	void             *priv;      /* private token */ 
	ddekit_sem_t     *started;
	int               running;   /* intloop is running */ 
	int               start_err;
};

static struct
{
	int               handle_irq; /* nested irq disable count */
	ddekit_sem_t     *irqsem;     /* synch semaphore */
	ddekit_sem_t     *stopsem;    /* stop semaphore */
	ddekit_thread_t  *irq_thread; /* thread ID for detaching from IRQ later on */
	unsigned          trigger;    /* trigger mode control */
	struct intloop_params   *params;
} ddekit_irq_ctrl[MAX_INTERRUPTS];

__thread int uiofd;
__thread int configfd;
__thread unsigned char command;
static void ddekit_irq_exit_fn(void *data)
{
}

static int irq_cnt = 0;

static int do_irq_attach(int irq)
{
	int err;
	char uio_path[16];
	char config_path[48];
	//bind device to uio
	int uio = ddekit_pci_bind_irq(irq);
	//get uio number
	snprintf(uio_path, sizeof(uio_path), "/dev/uio%d", ddekit_pci_bind_irq(irq));
	snprintf(config_path, sizeof(config_path), "/sys/class/uio/uio%d/device/config", ddekit_pci_bind_irq(irq));

	uiofd = open(uio_path, O_RDWR);
	configfd = open(config_path, O_RDWR);
	if (configfd < 0) {
		ddekit_fatal("%s: configfd %d (%d) %s", __func__, configfd, errno, strerror(errno));
		goto err;
	}
	if(uiofd < 0) {
		ddekit_fatal("%s: uiofd %d (%d) %s", __func__, uiofd, errno, strerror(errno));
		goto err;
	}

	err = pread(configfd, &command, 1, 5);
	if (err != 1) {
		ddekit_notify("%s: read command %d (%d) %s", __func__, err, errno, strerror(errno));
		goto err;
	}
	command &= ~0x4;
	ddekit_info("%s: interrupt attached", __func__);
	return uiofd;
err:
	return -1;
}

static void do_irq_detach(void *irq_ptr)
{
	int ret;
	int irq = *(int*)irq_ptr;
	ret = close(uiofd);
	ddekit_pci_unbind_irq(irq);
	if(ret)
		ddekit_printf("%s: error closing uiofd (%d) %s\n", __func__, errno, strerror(errno));

	ret = close(configfd);
	if(ret)
		ddekit_printf("%s: error closing uiofd (%d) %s\n", __func__, errno, strerror(errno));
}

static int do_irq_wait(int irq)
{
	static int icount = 0;
	int err;

	err = pread(configfd, &command, 1, 5);
	if (err != 1) {
		ddekit_fatal("%s: read command %d (%d) %s", __func__, err, errno, strerror(errno));
		goto err;
	}
	
	command &= ~0x4;
	err = pwrite(configfd, &command, 1, 5);
	if (err != 1) {
		ddekit_fatal("%s: write command %d (%d) %s", __func__, err, errno, strerror(errno));
		goto err;
	}

	err = read(uiofd, &icount, 4);
	if((icount - irq_cnt) > 1) {
		ddekit_info("old irq: %d new irq: %d diff: %d\n", irq_cnt, icount, icount-irq_cnt);
	}
	irq_cnt = icount;

	if (err != 4) {
		ddekit_fatal("%s: error reading interrupts (%d) %s", __func__, errno, strerror(errno));
		goto err;
	}
	return icount;
err:
	return 0;
}

static void
__intloop_cleanup(void* arg)
{
	int irq = *(int*)arg;
	ddekit_simple_free(ddekit_irq_ctrl[irq].params);
	
	ddekit_irq_ctrl[irq].irq_thread = 0;
	ddekit_irq_ctrl[irq].handle_irq = 0;
	ddekit_sem_deinit(ddekit_irq_ctrl[irq].irqsem);
	ddekit_sem_up(ddekit_irq_ctrl[irq].stopsem);
}

/**
 * Interrupt service loop
 *
 */
static void intloop(void *arg)
{
	struct intloop_params *params = arg;

	ddekit_printf("Thread 0x%lx for IRQ %d with function %p\n", (long)pthread_self(), params->irq, (void *)params->handler);

	/* stucture already filled, just add thread id */
	ddekit_irq_ctrl[params->irq].irq_thread = ddekit_thread_myself();
	int my_index = params->irq;
	
	pthread_cleanup_push(__intloop_cleanup, (void*) &params->irq);
	pthread_cleanup_push(do_irq_detach, (void *) &params->irq);

	/* allocate irq */
	int ret = do_irq_attach(my_index);
	if (ret < 0) {
		/* inform thread creator of error */
		/* XXX does error code have any meaning to DDEKit users? */
		params->start_err = ret;
		ddekit_sem_up(params->started);
		return;
	}

	/* after successful initialization call thread_init() before doing anything
	 * else here */
	if (params->thread_init) params->thread_init(params->priv);

	/* save handle + inform thread creator of success */
	params->start_err = 0;
	ddekit_sem_up(params->started);


	while (params->running) {
		short label;

		/* wait for int */
		label = do_irq_wait(my_index);

		/* if label == 0, than the interrupt should be disabled */
		//if (!label) {
		//    break;
		//}
#if DEBUG_INTERRUPTS
		//ddekit_printf("received irq 0x%X\n", params->irq);
#endif
		/* only call registered handler function, if IRQ is not disabled */
		/* XXX why do we need a semaphore ?! */
		ddekit_sem_down(ddekit_irq_ctrl[my_index].irqsem);
		if (ddekit_irq_ctrl[my_index].handle_irq > 0) {
#if DEBUG_INTERRUPTS
			//ddekit_printf("handling IRQ 0x%X\n", params->irq);
#endif
			if(params->handler)
				params->handler(params->priv);
		} else {
#if DEBUG_INTERRUPTS
			ddekit_printf("not handling IRQ %x, because it is disabled (%d).\n",
			              my_index, ddekit_irq_ctrl[my_index].handle_irq);
#endif
		}
		ddekit_sem_up(ddekit_irq_ctrl[my_index].irqsem);
	}
	
	pthread_cleanup_pop(1);
	pthread_cleanup_pop(1);
}


/**
 * Attach to hardware interrupt
 *
 * \param irq          IRQ number to attach to
 * \param shared       set to 1 if interrupt sharing is supported; set to 0
 *                     otherwise
 * \param thread_init  called just after DDEKit internal init and before any
 *                     other function
 * \param handler      IRQ handler for interrupt irq
 * \param priv         private token (argument for thread_init and handler)
 *
 * \return pointer to interrupt thread created
 */
ddekit_thread_t *ddekit_interrupt_attach(int irq, int shared,
                                         void(*thread_init)(void *),
                                         void(*handler)(void *), void *priv)
{
	struct intloop_params *params;
	ddekit_thread_t *thread;
	char thread_name[10];

	ddekit_printf("registering IRQ no %d\n", irq);

	if (irq >= MAX_INTERRUPTS)
	  {
#if DEBUG_INTERRUPTS
	    ddekit_printf("IRQ: Interrupt number out of range\n");
#endif
	    return NULL;
	  }

	/* initialize info structure for interrupt loop */
	params = ddekit_simple_malloc(sizeof(*params));
	if (!params) return NULL;

	params->irq         = irq;
	params->thread_init = thread_init;
	params->handler     = handler;
	params->priv        = priv;
	params->started     = ddekit_sem_init(0);
	params->start_err   = 0;
	params->shared      = shared;
	params->running     = 1;

	ddekit_irq_ctrl[irq].handle_irq = 1; /* IRQ nesting level is initially 1 */
	//ddekit_irq_ctrl[irq].irq_thread = thread;
	ddekit_irq_ctrl[irq].irqsem     = ddekit_sem_init(1);
	ddekit_irq_ctrl[irq].stopsem	= ddekit_sem_init(0);
	ddekit_irq_ctrl[irq].params	= params;
	
	/* construct name */
	snprintf(thread_name, 10, "irq%02X", irq);

	/* create interrupt loop thread */
	thread = ddekit_thread_create(intloop, params, thread_name, DDEKIT_IRQ_PRIO);
	if (!thread) {
		ddekit_simple_free(params);
		return NULL;
	}
	
	/* wait for intloop initialization result */
	ddekit_sem_down(params->started);
	ddekit_sem_deinit(params->started);
	if (params->start_err) {
		ddekit_simple_free(params);
		return NULL;
	}

	return thread;
}

/**
 * Detach from interrupt by disabling it and then shutting down the IRQ
 * thread.
 */
void ddekit_interrupt_detach(int irq)
{
	int ret;
#if DEBUG_INTERRUPTS
	ddekit_printf("disabling interrupt %d\n", irq);
#endif
	ddekit_interrupt_disable(irq);
	ddekit_irq_ctrl[irq].params->running = 0;
#if DEBUG_INTERRUPTS
	ddekit_printf("waiting for stopped intloop thread no %d\n", irq);
#endif

	ddekit_thread_terminate(ddekit_irq_ctrl[irq].irq_thread);
	ddekit_sem_down(ddekit_irq_ctrl[irq].stopsem);
#if DEBUG_INTERRUPTS
	ddekit_printf("intloop thread %d terminated\n", irq);
#endif
	ddekit_sem_deinit(ddekit_irq_ctrl[irq].stopsem);

}


void ddekit_interrupt_disable(int irq)
{
	ddekit_sem_down(ddekit_irq_ctrl[irq].irqsem);
	--ddekit_irq_ctrl[irq].handle_irq;
	ddekit_sem_up(ddekit_irq_ctrl[irq].irqsem);
}


void ddekit_interrupt_enable(int irq)
{
	ddekit_sem_down(ddekit_irq_ctrl[irq].irqsem);
	++ddekit_irq_ctrl[irq].handle_irq;
	ddekit_sem_up(ddekit_irq_ctrl[irq].irqsem);
}

enum L4_irq_flow_type
{
  L4_IRQ_F_NONE       = 0,     /**< None */
  L4_IRQ_F_LEVEL      = 0x2,   /**< Level triggered */
  L4_IRQ_F_EDGE       = 0x0,   /**< Edge triggered */
  L4_IRQ_F_POS        = 0x0,   /**< Positive trigger */
  L4_IRQ_F_NEG        = 0x4,   /**< Negative trigger */
  L4_IRQ_F_LEVEL_HIGH = 0x3,   /**< Level high trigger */
  L4_IRQ_F_LEVEL_LOW  = 0x7,   /**< Level low trigger */
  L4_IRQ_F_POS_EDGE   = 0x1,   /**< Positive edge trigger */
  L4_IRQ_F_NEG_EDGE   = 0x5,   /**< Negative edge trigger */
  L4_IRQ_F_MASK       = 0x7,   /**< Mask */
};

int ddekit_irq_set_type(int irq, unsigned type)
{
	if (irq < MAX_INTERRUPTS) {
		ddekit_printf("IRQ: set irq type of %d to %x (%x %x %x %x %x): %x\n", irq, type,
		              IRQF_TRIGGER_RISING,
		              IRQF_TRIGGER_FALLING,
		              IRQF_TRIGGER_HIGH,
		              IRQF_TRIGGER_LOW,
			      IRQF_TRIGGER_MASK,
			      (type & IRQF_TRIGGER_MASK));
		switch (type & IRQF_TRIGGER_MASK) {
			case IRQF_TRIGGER_RISING:
			  ddekit_irq_ctrl[irq].trigger = L4_IRQ_F_POS_EDGE;
			  break;
			case IRQF_TRIGGER_FALLING:
			  ddekit_irq_ctrl[irq].trigger = L4_IRQ_F_NEG_EDGE;
			  break;
			case IRQF_TRIGGER_HIGH:
			  ddekit_irq_ctrl[irq].trigger = L4_IRQ_F_LEVEL_HIGH;
			  break;
			case IRQF_TRIGGER_LOW:
			  ddekit_irq_ctrl[irq].trigger = L4_IRQ_F_LEVEL_LOW;
			  break;
			default: ddekit_irq_ctrl[irq].trigger = 0; break;
		}
		ddekit_printf("trigger: %x\n", ddekit_irq_ctrl[irq].trigger);

		return 0;
	}

	return -1;
}

void ddekit_init_irqs(void)
{
	int i;
	for (i = 0; i < MAX_INTERRUPTS; i++) {
		ddekit_irq_ctrl[i].trigger = 0;
	}
}

