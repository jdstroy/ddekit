/**
 * Unchecked (no BSD invariants) condition variable implementation for
 * dde-internal use. Written from scratch.
 *
 * \author Thomas Friebel <tf13@os.inf.tu-dresden.de>
 * \author Bjoern Doebel <doebel@os.inf.tu-dresden.de>
 */
#include <ddekit/condvar.h>
#include <ddekit/memory.h>
#include <ddekit/assert.h>

#include "internals.h"
#include <pthread.h>

struct ddekit_condvar {
	pthread_cond_t cond;
};

ddekit_condvar_t *ddekit_condvar_init()
{
	ddekit_condvar_t *c = ddekit_simple_malloc(sizeof(ddekit_condvar_t));
	int r = pthread_cond_init(&c->cond, NULL);
	Assert(r == 0);
	return c;
}


void ddekit_condvar_wait(ddekit_condvar_t *cvp, ddekit_lock_t *mp)
{
	int r = pthread_cond_wait(&cvp->cond, __ddekit_lock_to_pthread(*mp));
	Assert(r == 0);
}


int ddekit_condvar_wait_timed(ddekit_condvar_t *cvp, ddekit_lock_t *mp, int timo)
{
	const struct timespec abs_to = ddekit_abs_to_from_rel_ms(timo);
	return pthread_cond_timedwait(&cvp->cond,
	                              __ddekit_lock_to_pthread(*mp),
	                              &abs_to);
}


void ddekit_condvar_signal(ddekit_condvar_t *cvp)
{
	int r = pthread_cond_signal(&cvp->cond);
	Assert(r == 0);
}


void ddekit_condvar_broadcast(ddekit_condvar_t *cvp)
{
	int r = pthread_cond_broadcast(&cvp->cond);
	Assert(r == 0);
}
