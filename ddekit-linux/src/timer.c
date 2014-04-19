#include <ddekit/timer.h>
#include <ddekit/thread.h>
#include <ddekit/printf.h>
#include <ddekit/panic.h>
#include <ddekit/assert.h>
#include <ddekit/memory.h>
#include <ddekit/semaphore.h>

#include <sys/time.h>
#include <pthread.h>
#include "internals.h"
#include <time.h>

#define	__DEBUG	0

#define DDEKIT_TIMER_MAGIC	0xABCD1234

/* Just to remind BjoernD of what this is:
 * HZ = clock ticks per second
 * jiffies = clock ticks counter.
 *
 * So, if someone schedules a timeout to expire in 2 seconds,
 * this expires date will be in jiffies + 2 * HZ.
 */
volatile unsigned long jiffies = 0;
// FIXME: get HZ value from somewhere else
unsigned long HZ = 100;

typedef struct _timer
{
	struct _timer      *next;
	void               (*fn)(void *);
	void               *args;
	unsigned long      expires;
	struct timeval    _expires;
	int                id;
} ddekit_timer_t;

static ddekit_timer_t *timer_list  = NULL;
static ddekit_sem_t   *timer_lock  = NULL;
static ddekit_thread_t *timer_thread_ddekit = NULL;
static ddekit_thread_t *jiffies_thread = NULL;
static ddekit_sem_t   *notify_semaphore = NULL;

static int timer_id_ctr = 0;

static void dump_list(char *msg __attribute__((unused)))
{
#if __DEBUG
	ddekit_timer_t *l = timer_list;

	ddekit_printf("-=-=-=-= %s =-=-=-\n", msg);
	while (l) {
		ddekit_printf("-> %d %p (%lld)\n", l->id, l->args, l->expires);
		l = l->next;
	}
	ddekit_printf("-> NULL\n");
	ddekit_printf("-=-=-=-=-=-=-=-\n");
#endif
}


/** Notify the timer thread there is a new timer at the beginning of the
 *  timer list.
 */
static inline void __notify_timer_thread(void)
{
	/* Do not notify if there is no timer thread.
	 * XXX: Perhaps we should better assert that there is a timer
	 *      thread before allowing users to add a timer.
	 */
	if(timer_thread_ddekit == NULL)
		return;

	ddekit_sem_up(notify_semaphore);
}



int ddekit_add_timer(void (*fn)(void *), void *args, unsigned long timeout)
{
	ddekit_timer_t *it;
	ddekit_timer_t *t = ddekit_simple_malloc(sizeof(ddekit_timer_t));

	Assert(t);

	/* fill in values */
	t->fn      = fn;
	t->args    = args;
	t->expires = timeout;
	t->next    = NULL;
	
	ddekit_sem_down(timer_lock);
	t->id         = timer_id_ctr++;

	/* the easy case... */
	//if (timer_list == NULL || timercmp(&timer_list->_expires, &t->_expires, >=) ) {
	if (timer_list == NULL || timer_list->expires >= t->expires) {
		t->next    = timer_list;
		timer_list = t;
	}
	else { /* find where to insert */
		it = timer_list;
		while (it->next && it->next->expires < t->expires)
		//while (it->next && timercmp(&it->next->_expires, &t->_expires, <) )
			it = it->next;

		if (it->next) { /* insert somewhere in the middle */
			t->next  = it->next;
			it->next = t;
		}
		else /* we append */
			it->next = t;
	}

	/* if we modified the first entry of the list, it is
	 * necessary to notify the timer thread.
	 */
	if (t == timer_list) {
		__notify_timer_thread();
	}

	ddekit_sem_up(timer_lock);

	dump_list("after add");
	
	return t->id;
}


int ddekit_del_timer(int timer)
{
	ddekit_timer_t *it, *it_next;
	int ret = -1;

	ddekit_sem_down(timer_lock);

	/* no timer? */
	if (!timer_list) {
		ret = -2;
		goto out;
	}

	/* removee is first item, simply delete it */
	if (timer_list->id == timer) {
		it = timer_list->next;
		ret = timer_list->id;
		ddekit_simple_free(timer_list);
		timer_list = it;

		/* XXX: Yes, we could notify the timer thread here, so that it can
		 *      recalculate its sleep to now. However, this will require an
		 *      unnecessary IPC here. The timer thread will wake up in any 
		 *      case, find out that there is no timer for now, and return
		 *      to sleep.
		 */
		goto out;
	}

	it = timer_list;
	it_next = it->next;

	/* more difficult if removee is somewhere in
	 * the middle of the list
	 */
	while (it_next) {
		if (it_next->id == timer) {
			it->next = it->next->next;
			ret = it_next->id;
			ddekit_simple_free(it_next);
			goto out;
		}
		it = it->next;
		it_next = it->next;
	}

out:
	ddekit_sem_up(timer_lock);

	dump_list("after del");
	
	return ret;
}


/** Check whether a timer with a given ID is still pending.
 *
 * \param timer Timer ID to check for.
 * \return 0 if not pending
 *         1 if timer is pending
 */
int ddekit_timer_pending(int timer)
{
	ddekit_timer_t *t = NULL;
	int r = 0;

	ddekit_sem_down(timer_lock);

	t = timer_list;
	while (t) {
		if (t->id == timer) {
			r = 1;
			break;
		}
		t = t->next;
	}

	ddekit_sem_up(timer_lock);

	return r;
}


/** Get the next timer function to run.
 *
 * \return NULL     if no timer is to be run now
 *         != NULL  next timer to execute
 *
 * This function must be called with the timer_lock held.
 */
static ddekit_timer_t *get_next_timer(void)
{
	ddekit_timer_t *t = NULL;
	struct timeval now;
	gettimeofday(&now, NULL);
	if (timer_list &&
	   (timer_list->expires <= jiffies)) {
	//if (timer_list &&
	//   timercmp(&timer_list->_expires, &now, <=) ) {
		t = timer_list;
		//ddekit_sem_down(timer_lock);
		timer_list = timer_list->next;
		//ddekit_sem_up(timer_lock);
	}

	return t;
}

enum
{
	DDEKIT_TIMEOUT_NEVER = 0xFFFFFFFF,
};

/** Let the timer thread sleep for a while. 
 *
 * \param to  timeout in ms
 *
 * \return  1 if timed out
 *          0 if notification received -> recalc timeout
 */
extern int errno;
static inline int __timer_sleep(unsigned to)
{
	int err = 0;
	ddekit_sem_up(timer_lock);

	if (to == DDEKIT_TIMEOUT_NEVER) {
		ddekit_sem_down(notify_semaphore);
	}
	else {
		err = ddekit_sem_down_timed(notify_semaphore, to);
	}

	ddekit_sem_down(timer_lock);

	return (err ? 1 : 0);
}

/*
 * Number of jiffies the timer thread counts before waking up and
 * updating the jiffies variable.
 * XXX: Check for a value that poses not too high load on the
 *      system. 1 might be too busy.
 */

enum {
	JIFFIES_COUNT = 10,
};


static inline unsigned int jiffies_to_ms(unsigned int j) { return (j * 1000) / HZ; }

static void jiffies_thread_fn(void *arg __attribute__((unused)))
{
	int ret;
	struct timespec rqtp, rmtp;

	for (;;) { /* ever */
		//ddekit_printf("jiffies: %d\n", jiffies);
		rqtp.tv_sec = 0;
		rqtp.tv_nsec = (1000 * 1000 * 1000 / HZ) * JIFFIES_COUNT;
		
		do {
			ret = nanosleep(&rqtp, &rmtp);
			if(ret != 0)
				rqtp.tv_nsec = rmtp.tv_nsec;
		} while (ret != 0);

		jiffies += JIFFIES_COUNT;
	}
}


static void ddekit_timer_thread(void *arg __attribute__((unused)))
{
	ddekit_sem_down(timer_lock);
	int jdiff;

	notify_semaphore = ddekit_sem_init(0);

	while (1) {
		ddekit_timer_t *timer = NULL;
		unsigned long   to;

		/*
		 * While there are timers pending, we timedly wait on the notification
		 * semaphore. This may either
		 *   - time out : in this case we reached the expiration of the next timer
		 *                and go to execute its timer fn, or
		 *   - be interrupted by a new notification:
		 *                in this case we recalculate the new expiration interval
		 *                and go back to sleep
		 */
		do {
			if (timer_list) {
				jdiff = timer_list->expires - jiffies;
				if(jdiff < 0)
					jdiff = 0;
				to = jiffies_to_ms(jdiff);
			}
			else {
				to = DDEKIT_TIMEOUT_NEVER;
			}

#if 0
			ddekit_urgent("scheduling new timeout.");
			ddekit_urgent("\tdiff = %d ms\n", to);
#endif
		} while (__timer_sleep(to) == 0);

		while ((timer = get_next_timer()) != NULL) {
			ddekit_sem_up(timer_lock);
//			ddekit_printf("doing timer fn @ %p\n", timer->fn);
			if(timer->fn)
				timer->fn(timer->args);
			ddekit_sem_down(timer_lock);
			ddekit_simple_free(timer);
		}
	}
}

ddekit_thread_t *ddekit_get_timer_thread()
{
	return timer_thread_ddekit;
}


void ddekit_init_timers(void)
{
	/*
	 * Init timer list lock
	 */
	timer_lock = ddekit_sem_init(1);

	jiffies_thread = ddekit_thread_create(jiffies_thread_fn, NULL, "ddekit.jiffies", 0);
	Assert(jiffies_thread);

	/*
	 * Start the timer thread
	 */
	timer_thread_ddekit = ddekit_thread_create(ddekit_timer_thread, NULL,
											   "ddekit.timer", 0);
	Assert(timer_thread_ddekit);
	//timer_cap = pthread_getl4cap((pthread_t)ddekit_thread_get_id(timer_thread_ddekit));
}
