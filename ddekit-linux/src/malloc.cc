/*
 * \brief   Simple allocator implementation
 * \author  Christian Helmuth
 * \author  Bjoern Doebel
 * \date    2008-08-26
 *
 * This simple allocator provides malloc() and free() using dm_mem dataspaces
 * as backing store. The actual list-based allocator implementation is from
 * l4util resp. Fiasco.
 *
 * For large allocations and slab-based OS-specific allocators
 * ddekit_large_malloc and ddekit_slab_*() should be used. The blocks
 * allocated via this allocator CANNOT be used for DMA or other device
 * operations, i.e., there exists no virt->phys mapping.
 *
 * (c) 2006-2008 Technische Universität Dresden
 * This file is part of TUD:OS, which is distributed under the terms of the
 * GNU General Public License 2. Please see the COPYING file for details.
 */

/*
 * FIXME check thread-safety and add locks where appropriate
 * FIXME is umem_alloc and umem_free thread-safe as umem slab implementation is?
 */

#include <ddekit/compiler.h>
#include <ddekit/printf.h>
#include <stdlib.h>


/**
 * Allocate memory block via simple allocator
 *
 * \param size  block size
 * \return pointer to new memory block
 *
 * The blocks allocated via this allocator CANNOT be used for DMA or other
 * device operations, i.e., there exists no virt->phys mapping.
 *
 * Each chunk stores its size in the first word for free() to work.
 */
EXTERN_C void * ddekit_simple_malloc(unsigned size)
{
	return malloc(size);
}


/**
 * Free memory block via simple allocator
 *
 * \param p  pointer to memory block
 */
EXTERN_C void ddekit_simple_free(void *p)
{
	free(p);

}
