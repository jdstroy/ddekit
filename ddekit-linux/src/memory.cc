/*
 * \brief   Memory subsystem
 * \author  Thomas Friebel <tf13@os.inf.tu-dresden.de>
 * \author  Christian Helmuth <ch12@os.inf.tu-dresden.de>
 * \author  Bjoern Doebel <doebel@tudos.org>
 * \date    2006-11-03
 *
 * The memory subsystem provides the backing store for DMA-able memory via
 * large malloc and slabs.
 *
 * FIXME check thread-safety and add locks where appropriate
 */

#include <ddekit/memory.h>
#include <ddekit/panic.h>
#include <ddekit/printf.h>
#include <ddekit/pgtab.h>
#include <ddekit/dma.h>
#include <ddekit/pci.h>

#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>
#include <pthread.h>
#include <inttypes.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>

#define DEBUG 0

/****************
 ** Page cache **
 ****************/

/* FIXME revisit two-linked-lists approach - we could get rid of the
 * pcache_free lists, because these descriptors can also be allocated
 * whenever the need arises. (XXX: Check how this hurts performance.)
 */

/*****************************************************************************
  DDEKit maintains a list of free pages that can be used for growing
  existing DDEKit slabs. We distinguish between 2 types of pages: the ones
  that have been allocated from physically contiguous dataspaces and the the
  ones that have not.

  For each type of cache, we maintain two lists: a list of free pages that
  can be used to grow a cache (pcache_used) and a list of free pcache entries
  that can be used to store pages when they get freed (pcache_free). The
  reason for this is that by using these two lists, cache operations can be
  done atomically using cmpxchg.

  The function ddekit_slab_setup_page_cache() is used to tune the number of
  cached pages.
 ******************************************************************************/

/* page cache to minimize allocations from external servers */
struct ddekit_pcache
{
	struct ddekit_pcache *next;
	void *page;
	int contig;
};


/*******************************
 ** Slab cache implementation **
 *******************************/

/* ddekit slab facilitates l4slabs */
typedef struct ddekit_slab
{
	unsigned long size;
	void * ptr;
	void * data;
	/* 
	 * Lock to prevent concurrent access to the slab's grow() and
	 * shrink() functions.
	 * We should not need it, because umem is MT-safe.
	 */
	pthread_mutex_t	lock;
	int            contiguous;
}ddekit_slab_t;

#define UMEM 0

/**
 * Allocate object in slab
 */
EXTERN_C void *ddekit_slab_alloc(ddekit_slab_t * slab)
{
	return malloc(slab->size);
}


/**
 * Free object in slab
 */
EXTERN_C void  ddekit_slab_free(ddekit_slab_t * slab, void *objp)
{
	free(objp);
}


/**
 * Store user pointer in slab cache
 */
EXTERN_C void  ddekit_slab_set_data(ddekit_slab_t * slab, void *data)
{
	pthread_mutex_lock(&slab->lock);
	slab->data = data;
	pthread_mutex_unlock(&slab->lock);
}


/**
 * Read user pointer from slab cache
 */
EXTERN_C void *ddekit_slab_get_data(ddekit_slab_t * slab)
{
	return slab->data;
}


/**
 * Destroy slab cache
 *
 * \param slab  pointer to slab cache structure
 */
EXTERN_C void  ddekit_slab_destroy (ddekit_slab_t * slab)
{
	ddekit_simple_free(slab);
}

/**
 * Initialize slab cache
 *
 * \param size          size of cache objects
 * \param contiguous    make this slab use physically contiguous memory
 *
 * \return pointer to new slab cache or 0 on error
 */
EXTERN_C ddekit_slab_t * ddekit_slab_init(unsigned size, int contiguous)
{
	ddekit_slab_t * slab;

	/* maybe use ddekit_slab_t instead of *slab? */
	slab = (ddekit_slab_t *) ddekit_simple_malloc(sizeof(*slab));
	pthread_mutex_init(&slab->lock, NULL);
	slab->size = size;
	slab->contiguous = contiguous;
	
	return slab;
}


/**********************************
 ** Large block memory allocator **
 **********************************/

pthread_mutex_t large_alloc_mtx = PTHREAD_MUTEX_INITIALIZER;

/**
 * Free large block of memory
 *
 * This is no useful for allocation < page size.
 */
EXTERN_C void ddekit_large_free(void *objp)
{
	free(objp);
}


/**
 * Allocate large block of memory
 * DMA
 * This is not useful for allocation < page size.
 */
EXTERN_C void *ddekit_large_malloc(int size)
{
	return malloc(size);
}

//TODO remove define and determine operationg mode at runtime
//#define IOMMU

static int fd = 0;

EXTERN_C void *ddekit_dma_alloc_coherent(int size, ddekit_addr_t *dma_addr)
{
	int ret;
	void *ptr = NULL;
	struct dma_op dma_req;

#ifdef IOMMU
	//ptr = malloc(size);
	ptr = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
	if(MAP_FAILED == ptr)
		ddekit_panic("%s: mmap() failed (%d) %s\n", __func__, errno, strerror(errno));

	dma_req.size = size;
	dma_req.va = (unsigned long)ptr;
	dma_req.iova = 0;

	ddekit_printf("%s: translating %d allocated bytes from %p\n", __func__, size, ptr);
	
	ret = ioctl(fd, DMA_MAP, &dma_req);
	if(ret < 0)
		ddekit_panic("%s: error reading (%d) %s\n", __func__, errno, strerror(errno));
	
	if(!dma_req.iova)
		ddekit_panic("%s: error mapping %p (%d)\n", __func__, ptr, size);

	if(dma_req.iova != dma_req.va)
		ddekit_info("%s: error mapping %p from 0x%lx to 0x%lx (%d)\n", __func__, ptr, dma_req.va, dma_req.iova, dma_req.size);

#else
	
	ptr = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	if(ptr == MAP_FAILED)
		ddekit_panic("%s: mmap() failed (%d) %s\n", __func__, errno, strerror(errno));

	ddekit_printf("%s: translating %d allocated bytes from %p\n", __func__, size, ptr);
	dma_req.size = size;
	dma_req.va = (unsigned long)ptr;
	dma_req.iova= 0;

	ret = ioctl(fd, DMA_TRANSLATE, &dma_req);
	if(ret < 0)
		ddekit_panic("%s: error reading (%d) %s\n", __func__, errno, strerror(errno));
#endif
	*dma_addr = dma_req.iova;

	if(!ptr)
		ddekit_panic("%d: %s\n", errno, strerror(errno));

	return ptr;
}

EXTERN_C void ddekit_dma_free_coherent(void *objp, int size, ddekit_addr_t dma)
{
#ifdef IOMMU
	int ret;
	struct dma_op dma_req;

	dma_req.size = size;
	dma_req.iova = (unsigned long)dma;

	ddekit_printf("%s: unmapping %d allocated bytes from %p\n", __func__, size, objp);
	
	ret = ioctl(fd, DMA_UNMAP, &dma_req);
	if(ret < 0)
		ddekit_panic("%s: error reading (%d) %s\n", __func__, errno, strerror(errno));
	
//	free(objp);
	munmap(objp, size);
#else
	int ret;
	struct dma_op dma_req;
	
	if(size < 0) {
		ddekit_printf("%s: error freeing %p\n", __func__, objp);
		return;
	}
		
	dma_req.size = size;
	dma_req.va = (unsigned long int)objp;
	dma_req.iova = dma;

	ret = munmap(objp, size);
	
	if(ret) 
		ddekit_printf("%s: error unmapping %p (%d): %s\n", __func__, objp, errno, strerror(errno));

	ret = ioctl(fd, DMA_FREE, &dma_req);	
	if(ret != 0)
		ddekit_printf("%s: ret: %d (%d) %s\n", __func__, ret, errno, strerror(errno));
#endif
}

/**
 * Allocate large block of memory (special interface)
 *
 * This is no useful for allocation < page size.
 *
 * FIXME implementation missing...
 */
EXTERN_C void *ddekit_contig_malloc(unsigned long size __attribute__((unused)),
                           unsigned long low __attribute__((unused)),
                           unsigned long high __attribute__((unused)),
                           unsigned long alignment __attribute__((unused)),
                           unsigned long boundary __attribute__((unused)))
{
	ddekit_debug("%s: not implemented\n", __func__);
	return 0;
}

EXTERN_C int ddekit_pci_bind_irq(int);

EXTERN_C void ddekit_mem_init()
{
	char buf[128];
	ddekit_printf("dma-iommu memory init\n");
	snprintf(buf, sizeof(buf), "/dev/uio%d-dma", ddekit_pci_bind_irq(0));
	fd = open(buf, O_RDWR);
	if(fd < 0) {
		ddekit_panic("%s: error (%d) %s (%s)\n", __func__, errno, strerror(errno), buf);	
	}
}
