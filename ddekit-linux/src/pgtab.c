/*
 * \brief   Virtual page-table facility
 * \author  Thomas Friebel <tf13@os.inf.tu-dresden.de>
 * \author  Christian Helmuth <ch12@os.inf.tu-dresden.de>
 * \date    2006-11-01
 *
 * This implementation uses l4rm (especially the AVL tree and userptr) to
 * manage virt->phys mappings. Each mapping region is represented by one
 * pgtab_object that is kept in the l4rm region userptr.
 *
 * For this to work, dataspaces must be attached to l4rm regions!
 */

#include <ddekit/pgtab.h>
#include <ddekit/memory.h>
#include <ddekit/panic.h>
#include <ddekit/printf.h>
#include <ddekit/semaphore.h>
#include <ddekit/types.h>

#define _XOPEN_SOURCE 500
#ifdef __OPTIMIZE__
#undef __OPTIMIZE__
#include <unistd.h>
#include <fcntl.h>
#define __OPTIMIZE__
#else
#include <fcntl.h>
#endif
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include <unistd.h>


/**
 * "Page-table" object
 */
struct pgtab_object
{
	ddekit_addr_t va;    /* virtual start address */
	ddekit_addr_t pa;    /* physical start address */
	ddekit_addr_t va_end;
	/* FIXME reconsider the following members */
	ssize_t size;
	unsigned  type;  /* pgtab region type */
	
	struct pgtab_object * next;
	struct pgtab_object * prev;
};

/**
 * pa_list_head of page-table object list (for get_virtaddr())
 */
static struct pgtab_object pa_list_head =
{
	.va = 0,
	.pa = 0,
	.va_end = 0,
	.size = 0,
	.type = 0,
	.next = &pa_list_head,
	.prev = &pa_list_head
};



#define DMA_CACHE 0


static void  __attribute__((used)) dump_pgtab_list(void)
{
	struct pgtab_object *p = pa_list_head.next;

	ddekit_printf("PA LIST DUMP\n");
	for ( ; p != &pa_list_head; p = p->next)
	{
		ddekit_printf("\t0x%08x -> 0x%08x (%d)\n", p->va, p->pa, p->size);
	}
	ddekit_printf("PA END DUMP\n");
}

static ddekit_sem_t *pa_list_lock;
static ddekit_sem_t *region_lock;

void ddekit_pgtab_init(void);
void ddekit_pgtab_init(void)
{
	pa_list_lock = ddekit_sem_init(1);
	region_lock = ddekit_sem_init(1);
}

static struct pgtab_object *__find(ddekit_addr_t virt)
{
	struct pgtab_object *p = NULL;

	for (p = pa_list_head.next; p != &pa_list_head; p = p->next)
	{
		if (virt >= p->va && virt < p->va_end)
			break;
	}

	return p == &pa_list_head ? NULL : p;
}

/*****************************
 ** Page-table facility API **
 *****************************/

/**
 * Get physical address for virtual address
 *
 * \param virtual  virtual address
 * \return physical address or 0
 */
ddekit_addr_t ddekit_pgtab_get_physaddr(const void *virt)
{
	/* find pgtab object */
	ddekit_sem_down(pa_list_lock);
	struct pgtab_object *p = __find((ddekit_addr_t)virt);
	ddekit_sem_up(pa_list_lock);
	if (!p) {
		/* if we can't translate it, return it - needed for DMA! */
		return (ddekit_addr_t)virt;
	}

	/* return virt->phys mapping */
	ssize_t offset = (ddekit_addr_t) virt - p->va;

	return p->pa + offset;
}

/**
 * Get virt address for physical address
 *
 * \param physical  physical address
 * \return virtual address or 0
 */
ddekit_addr_t ddekit_pgtab_get_virtaddr(const ddekit_addr_t physical)
{
	/* find pgtab object */
	struct pgtab_object *p;
	ddekit_addr_t retval = 0;
	
	/* find phys->virt mapping */
	ddekit_sem_down(pa_list_lock);
	for (p = pa_list_head.next ; p != &pa_list_head ; p = p->next) {
		if (p->pa <= (ddekit_addr_t)physical && 
		    (ddekit_addr_t)physical < p->pa + p->size) {
			ssize_t offset = (ddekit_addr_t) physical - p->pa;
			retval = p->va + offset;
			break;
		}
	}
	ddekit_sem_up(pa_list_lock);

	if (!retval)
		ddekit_debug("%s: no phys->virt mapping for physical address %p", __func__, (void*)physical);

	return retval;
}



int ddekit_pgtab_get_type(const void *virt)
{
	/* find pgtab object */
	ddekit_sem_down(pa_list_lock);
	struct pgtab_object *p = __find((ddekit_addr_t)virt);
	ddekit_sem_up(pa_list_lock);
	if (!p) {
		/* XXX this is verbose */
		ddekit_debug("%s: no virt->phys mapping for %p", __func__, virt);
		return -1;
	}

	return p->type;
}


int ddekit_pgtab_get_size(const void *virt)
{
	/* find pgtab object */
	ddekit_sem_down(pa_list_lock);
	struct pgtab_object *p = __find((ddekit_addr_t)virt);
	ddekit_sem_up(pa_list_lock);
	if (!p) {
		/* XXX this is verbose */
		ddekit_debug("%s: no virt->phys mapping for %p", __func__, virt);
		return -1;
	}

	return p->size;
}


/**
 * Clear virtual->physical mapping for VM region
 *
 * \param virtual   virtual start address for region
 * \param type      pgtab type for region
 */
void ddekit_pgtab_clear_region(void *virt, int type __attribute__((unused)))
{

	ddekit_sem_down(region_lock);
	ddekit_sem_down(pa_list_lock);
	struct pgtab_object *p = __find((ddekit_addr_t)virt);
	if (!p) {
		/* XXX this is verbose */
		ddekit_urgent("%s: no virt->phys mapping for %p\n", __func__, virt);
		goto out;
	}


	/* remove pgtab object from list */
	p->next->prev= p->prev;
	p->prev->next= p->next;
	ddekit_sem_up(pa_list_lock);
	//ddekit_printf("removed %p 0x%08x from pgtab\n", virt, p->pa);
	ddekit_sem_up(region_lock);
	
	
	
	/* free pgtab object */
	ddekit_simple_free(p);
	return;

out:
	ddekit_sem_up(pa_list_lock);
	ddekit_sem_up(region_lock);

}


/**
 * Set virtual->physical mapping for VM region
 *
 * \param virtual   virtual start address for region
 * \param physical  physical start address for region
 * \param pages     number of pages in region
 * \param type      pgtab type for region
 */
void ddekit_pgtab_set_region(void *virt, ddekit_addr_t phys, int pages, int type)
{
	/* allocate pgtab object */
	ddekit_sem_down(region_lock);
	struct pgtab_object *dp, *p = ddekit_simple_malloc(sizeof(*p));
	if (!p) {
		ddekit_printf("ddekit heap exhausted\n");
		goto out;
	}
	/* initialize pgtab object */
	p->va   = (ddekit_addr_t)virt;
	p->pa   = phys;
	p->size = pages;
	p->va_end = p->va + p->size;
	p->type = type;

	ddekit_sem_down(pa_list_lock);
	p->next = &pa_list_head;
	p->prev = pa_list_head.prev;
	p->prev->next = p;
	pa_list_head.prev = p;
	
	ddekit_sem_up(pa_list_lock);
//	ddekit_printf("added %p 0x%08x to pgtab\n", virt, phys);
out:
	ddekit_sem_up(region_lock);
}

#define L4_PAGESHIFT (12)
#define L4_PAGESIZE  (1 << L4_PAGESHIFT)
int l4_round_page(int size)
{
	return (size & ~(L4_PAGESIZE-1)) + (L4_PAGESIZE - 1);
}
void ddekit_pgtab_set_region_with_size(void *virt, ddekit_addr_t phys, int size, int type)
{
	int p = l4_round_page(size);
	p >>= L4_PAGESHIFT;
//	ddekit_printf("%s: virt %p, phys %p, pages %d\n", __func__, virt, phys, p);
	ddekit_pgtab_set_region(virt, phys, p, type);
}

