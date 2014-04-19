#pragma once

#include <ddekit/assert.h>

#include <semaphore.h>
#include <time.h>

#include <pthread.h>

struct ddekit_lock 
{
	pthread_mutex_t mtx;
};

static inline pthread_mutex_t *__ddekit_lock_to_pthread(struct ddekit_lock *);
static inline pthread_mutex_t *__ddekit_lock_to_pthread(struct ddekit_lock *l)
{
	return (pthread_mutex_t *)&l->mtx;
}

enum
{
	one_thousand = 1000,
	one_million  = 1000000,
	one_billion  = 1000000000,
};


/*
 * The DDEKit interface specifies some functions with relative timeouts
 * that need to be converted to absolute ones to be used with libpthread
 * stuff.
 */
static inline struct timespec ddekit_abs_to_from_rel_ms(int ms)
{
	/*
	 * struct timeval:
	 *    tv_sec
	 *    tv_usec
	 *
	 * struct timespec
	 *    tv_sec
	 *    tv_nsec
	 *
	 * Assumption: to parameter is in msec
	 */
	struct timespec abs_to;
	int r = clock_gettime(CLOCK_REALTIME, &abs_to);
	Assert(r == 0);

	abs_to.tv_sec += (ms / one_thousand);
	abs_to.tv_nsec += ((ms % one_thousand) * 1000*1000);

	/*
	 * Adjust nsec value. Pthreads requires it to be < 1.000.000.000
	 */
	if (abs_to.tv_nsec > one_billion) {
		abs_to.tv_nsec -= one_billion;
		abs_to.tv_sec += 1;
	}

	return abs_to;
}

