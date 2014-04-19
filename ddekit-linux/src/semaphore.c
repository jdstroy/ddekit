#include <ddekit/semaphore.h>
#include <ddekit/memory.h>
#include <ddekit/panic.h>
#include <ddekit/assert.h>

#include "internals.h"

#include <pthread.h>
#include <stdlib.h>
#include <semaphore.h>
#include <time.h>


struct ddekit_sem {
	sem_t sem;
};


ddekit_sem_t *ddekit_sem_init(int value)
{
	int r;
	ddekit_sem_t *s = ddekit_simple_malloc(sizeof(ddekit_sem_t));
	r = sem_init(&(s->sem), 0, value);
	Assert(r == 0);

	return s;
}


void ddekit_sem_deinit(ddekit_sem_t *sem) 
{
	Assert(sem_destroy(&sem->sem) == 0);
	ddekit_simple_free(sem);
}

void ddekit_sem_down(ddekit_sem_t *sem)
{
	Assert(sem_wait(&sem->sem) == 0);
}


/* returns 0 on success, != 0 when it would block */
int  ddekit_sem_down_try(ddekit_sem_t *sem)
{
	return sem_trywait(&sem->sem);
}


/* returns 0 on success, != 0 on timeout */
int  ddekit_sem_down_timed(ddekit_sem_t *sem, int to)
{
	/*
	 * PThread semaphores want absolute timeouts, to is relative though.
	 */
	struct timespec wait_time = ddekit_abs_to_from_rel_ms(to);
	return sem_timedwait(&sem->sem, &wait_time);
}

void ddekit_sem_up(ddekit_sem_t *sem)
{
	Assert(sem_post(&sem->sem) == 0);
}
