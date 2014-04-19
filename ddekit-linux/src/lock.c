#include "internals.h"
#include <ddekit/lock.h>
#include <ddekit/memory.h>
#include <ddekit/panic.h>
#include <ddekit/assert.h>

#include <pthread.h>
#include <bits/pthreadtypes.h> // mutex->__m_owner

#define DDEKIT_DEBUG_LOCKS 1

void ddekit_lock_init    (ddekit_lock_t *mtx)
{
	*mtx = (ddekit_lock_t)ddekit_simple_malloc(sizeof(struct ddekit_lock));
        Assert(pthread_mutex_init(&(*mtx)->mtx, NULL) == 0);
}

void ddekit_lock_deinit  (ddekit_lock_t *mtx)
{
	pthread_mutex_destroy(&(*mtx)->mtx);
	ddekit_simple_free(*mtx);
}


void ddekit_lock_lock    (ddekit_lock_t *mtx)
{
	Assert(pthread_mutex_lock(&(*mtx)->mtx) == 0);
}


int  ddekit_lock_try_lock(ddekit_lock_t *mtx)
{
	return pthread_mutex_trylock(&(*mtx)->mtx);
}


void ddekit_lock_unlock  (ddekit_lock_t *mtx)
{
	Assert(pthread_mutex_unlock(&(*mtx)->mtx) == 0);
}


int ddekit_lock_owner(ddekit_lock_t *mtx)
{
	return (int)((*mtx)->mtx.__data.__owner);
}


