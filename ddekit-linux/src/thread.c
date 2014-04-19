#include <ddekit/thread.h>
#include <ddekit/condvar.h>
#include <ddekit/panic.h>
#include <ddekit/assert.h>
#include <ddekit/memory.h>
#include <ddekit/printf.h>

//#include <l4/dde/dde.h>

#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include <inttypes.h>

#define DDEKIT_THREAD_STACK_SIZE 0x4000 /* 16 KB */

#define WARN_UNIMPL         ddekit_printf("unimplemented: %s\n", __FUNCTION__)

static struct ddekit_slab *ddekit_stack_slab = NULL;

struct ddekit_thread {
	pthread_t pthread;
	void *data;
	void *stack;
	ddekit_condvar_t *sleep_cv;
	const char *name;
};

/**
 * The thread-local-storage key for the thread struct.
 */
static pthread_key_t tlskey_thread;

struct startup_args {
	void (*fun)(void *); ///< real function to run
	void *arg;           ///< startup arguments to fun
	const char *name;    ///< thread name
	pthread_mutex_t mtx; ///< mutex to sync starter with new thread
	ddekit_thread_t *td; ///< thread data return value
	void * stack;        ///< stack adress for cleanup function
};


void __ddekit_thread_cleanup(void *arg)
{
	ddekit_thread_t *td = (ddekit_thread_t *) arg;
	//ddekit_printf("thread cleanup %s: %p, stack: %p\n", td->name, (void*)td, td->stack);
	ddekit_slab_free(ddekit_stack_slab ,td->stack);
	td->stack = NULL;
	ddekit_simple_free(td);
	td = NULL;
}

ddekit_thread_t *ddekit_thread_setup_myself(const char *name) {
	ddekit_thread_t *td;
	int namelen = strlen(name);
	char *pname;
	
	td = ddekit_simple_malloc(sizeof(*td) + (namelen+1));
	pname = (char *) td + sizeof(*td);

	td->data=NULL;
	td->sleep_cv = ddekit_condvar_init();
	td->pthread = pthread_self();
	td->name = pname;

	strncpy(pname, name, namelen+1);

	pthread_setspecific(tlskey_thread, td);

	return td;
}


/* 
 * Thread startup function.
 *
 * In the beginning of starting a thread, we need to setup some additional
 * data structures, so we do reroute call flow through this function. It 
 * performs setup, then notifies the caller of its success and thereafter
 * runs the real thread function.
 */
static void *ddekit_thread_startup(void *arg) {
	ddekit_thread_t *thread_ptr;
	struct startup_args *su = (struct startup_args*)arg;
	/*
	 * Copy function pointer and arg ptr from su, so that
	 * ddekit_thread_create() can later on free this temporary
	 * object.
	 */
	void (*_fn)(void *) = su->fun;
	void *_arg = su->arg;

	/* init dde thread structure */
	su->td = ddekit_thread_setup_myself(su->name);
	thread_ptr = su->td;
	/* startup ready, notify parent */
	//ddekit_printf("thread startup %s: %p, stack: %p\n", su->name, (void*)su->td, su->stack);
	pthread_mutex_unlock(&su->mtx);

	/* Call thread routine */
	pthread_cleanup_push(__ddekit_thread_cleanup, (void*) thread_ptr);
	_fn(_arg);
	pthread_cleanup_pop(1);

	return NULL;
}


/*
 * Create a new DDEKit thread (using pthreads).
 */
ddekit_thread_t *ddekit_thread_create(void (*fun)(void *), void *arg, const char *name,
                                      unsigned prio __attribute__((unused)))
{
	struct startup_args su;      // startup args we (temporarily) share with the
	                             // new thread
	ddekit_thread_t *td;         // thread descriptor
	pthread_t l4td;              // pthread
	pthread_attr_t thread_attr;  // pthread attributes -> we actually set our
	                             // our own stack
	int err;
	void *stack;                 // thread stack
	
	/*
	 * Things we need to tell our new thread:
	 */
	su.fun  = fun;
	su.arg  = arg;
	su.name = name;

	/*
	 * Initialize the handshake mutex. We lock it and then wait for it being
	 * unlocked. The latter is done by the newly created thread after it has
	 * successfully set up everything.
	 */
	if ((err = pthread_mutex_init(&su.mtx, NULL)) != 0)
		ddekit_panic("Error initializing pthread_mutex");
	pthread_mutex_lock(&su.mtx);

	/*
	 * Allocate a stack
	 */
	stack  = ddekit_slab_alloc(ddekit_stack_slab);
	if (stack == NULL)
		ddekit_panic("Cannot allocate stack for new thread.");
	su.stack = stack;

	/*
	 * Setup new thread's attributes, namely stack address and stack size.
	 *
	 * NOTE: attr_setstack requires the _beginning_ of the stack area as
	 * parameter, not the initial stack pointer!
	 */
	if ((err = pthread_attr_init(&thread_attr)) != 0)
		ddekit_panic("error initializing pthread attr: %d", err);
	if ((err = pthread_attr_setstack(&thread_attr, stack, DDEKIT_THREAD_STACK_SIZE))
	        != 0)
		ddekit_panic("error setting pthread stack: %d", err);

	/*
	 * Create thread
	 */
	err = pthread_create(&l4td, &thread_attr, ddekit_thread_startup, &su);
	if (err != 0)
		ddekit_panic("error creating thread (%d): %s", err, strerror(err));

	/*
	 * Wait for child to finish startup, then destroy the handshake mtx.
	 */
	pthread_mutex_lock(&su.mtx);
	pthread_mutex_destroy(&su.mtx);

	/*
	 * Store stack address in thread descriptor and return it
	 */
	td = su.td;
	td->stack = stack;
	return td;
}

ddekit_thread_t *ddekit_thread_myself(void) {
	ddekit_thread_t *ret = (ddekit_thread_t *)pthread_getspecific(tlskey_thread);
	Assert(ret);
	return ret;
}

void ddekit_thread_set_data(ddekit_thread_t *thread, void *data) {
	Assert(thread);
	thread->data = data;
}

void ddekit_thread_set_my_data(void *data) {
	ddekit_thread_set_data(ddekit_thread_myself(), data);
}

void *ddekit_thread_get_data(ddekit_thread_t *thread) {
	return thread->data;
}

void *ddekit_thread_get_my_data() {
	return ddekit_thread_get_data(ddekit_thread_myself());
}

void ddekit_thread_msleep(unsigned long msecs) {
	int rc;

	struct timespec rqtp;
	struct timespec rmtp;

	rqtp.tv_sec = msecs / 1000;
	rqtp.tv_nsec = (msecs % 1000) * 1000 * 1000;

	do {
		if((rc = nanosleep(&rqtp, &rmtp)) != 0) {
			rqtp.tv_sec = rmtp.tv_sec;
			rqtp.tv_nsec = rmtp.tv_nsec;
		}
	} while(rc != 0);
}

void ddekit_thread_usleep(unsigned long usecs) {
	int rc;

	struct timespec rqtp;
	struct timespec rmtp;

	rqtp.tv_sec = usecs / 1000000;
	rqtp.tv_nsec = (usecs % 1000000) * 1000;

	do {
		if((rc = nanosleep(&rqtp, &rmtp)) != 0) {
			rqtp.tv_sec = rmtp.tv_sec;
			rqtp.tv_nsec = rmtp.tv_nsec;
		}
	} while(rc != 0);
}


void ddekit_thread_nsleep(unsigned long nsecs) {
	int rc;
	struct timespec rqtp;
	struct timespec rmtp;

	rqtp.tv_sec = nsecs / 1000000000;
	rqtp.tv_nsec = nsecs % 1000000000;

	do {
		if((rc = nanosleep(&rqtp, &rmtp)) != 0) {
			rqtp.tv_sec = rmtp.tv_sec;
			rqtp.tv_nsec = rmtp.tv_nsec;
		}
	} while(rc != 0);
}

void ddekit_thread_sleep(ddekit_lock_t *lock) {
	ddekit_thread_t *td;

	td = ddekit_thread_myself();

	ddekit_condvar_wait(td->sleep_cv, lock);
}

void  ddekit_thread_wakeup(ddekit_thread_t *td) {
	ddekit_condvar_signal(td->sleep_cv);
}

void  ddekit_thread_exit() {
	ddekit_thread_t *td;

	td = ddekit_thread_myself();
	
	pthread_exit(0);
}

int ddekit_thread_terminate(ddekit_thread_t *t)
{
	return pthread_cancel(t->pthread);
}

const char *ddekit_thread_get_name(ddekit_thread_t *thread) {
	return thread->name;
}

int ddekit_thread_get_id(ddekit_thread_t *t)
{
	return (int)(int64_t *)t->pthread;
}

void ddekit_thread_schedule(void)
{
	/* pthread_yield(); calls sched_yield() anyway */
	sched_yield();
}

void ddekit_yield(void)
{
	/* pthread_yield(); calls sched_yield() anyway */
	sched_yield();
}

void ddekit_init_threads() {
	/* register TLS key for pointer to dde thread structure */
	int err = pthread_key_create(&tlskey_thread, NULL);
	if (err != 0)
		ddekit_panic("pthread_key_create()");
	
	/* setup dde part of thread data */
	ddekit_thread_setup_myself("main");
	
 	/* create slab for stacks */
	ddekit_stack_slab = ddekit_slab_init(DDEKIT_THREAD_STACK_SIZE, 1);
}
