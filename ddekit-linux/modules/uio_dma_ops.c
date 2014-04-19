#include <linux/slab.h>
#include <linux/module.h>
#include <linux/pagemap.h>
#include <linux/iommu.h>
#include <linux/dma-mapping.h>
#include <linux/kernel.h>
#include <linux/pci.h>

#include "uio_dma.h"

#define DEBUG_MAP (1 << 1)
#define DEBUG_ERR (1 << 2)
#define MAX_MAP_PAGES 3

volatile unsigned int map_min = UINT_MAX, map_avg = 0, map_max = 0;
volatile unsigned int unmap_min = UINT_MAX, unmap_avg = 0, unmap_max = 0;

volatile u64 memcpy_duration[1024];
volatile unsigned int memcpy_index = 0;

/**
 * Basic sanity checks of paramters of the
 * buffer to be mapped
 */

static int
dma_map_check(struct dma_mapping *mapping)
{
	if((mapping->op.va + mapping->op.size) < mapping->op.va) {
		printk(KERN_ERR "buffer length overflow\n");
		return -EINVAL;
	}
	if(mapping->nr_pages == 0) {
		printk(KERN_ERR "number of pages is 0\n");
		return -EINVAL;
	}
	if(mapping->op.size == 0) {
		printk(KERN_ERR "size of mapping is 0\n");
		return -EINVAL;
	}
	if(mapping->nr_pages > MAX_MAP_PAGES) {
		printk(KERN_ERR "number of pages too large %d\n", mapping->nr_pages);
		return -ENOMEM;
	}

	return 0;
}

/**
 * Actual mapping of the buffer:
 * - find pages
 * - fault them into RAM, if they are swapped
 * - lock them in RAM
 */
static int
dma_map_buf(struct dma_mapping *mapping)
{
	int i;
	int nr_pages_mapped;

	//TODO set read/write according to dma direction
	nr_pages_mapped = get_user_pages_fast(mapping->op.va,
						mapping->nr_pages,
						1,
						mapping->page_list);

	if(nr_pages_mapped > MAX_MAP_PAGES) {
		printk(KERN_ERR "Error mapping pages.\n");
		for(i = 0; i < nr_pages_mapped; i++) {
			printk("page address 0x%08x\n", page_to_phys(mapping->page_list[i]));
			printk("page v_address %p\n", phys_to_virt(page_to_phys(mapping->page_list[i])));
			print_hex_dump(KERN_DEBUG, "page ", DUMP_PREFIX_ADDRESS, 32, 4, kmap(mapping->page_list[i]), PAGE_SIZE, false);
			page_cache_release(mapping->page_list[i]);
		}
		return -EIO;
	}
	
	return nr_pages_mapped;
}

/**
 * Turn a userspace buffer into a
 * list of struct page*
 */
static int
dma_map_pages(struct dma_mapping *mapping)
{
	int ret;
	
	//printk(KERN_INFO "pinning and mapping userspace buffer\n");
	mapping->offset = mapping->op.va & ~PAGE_MASK;

	if(debug & DEBUG_MAP)
		printk("%s: va: 0x%lx, mask: 0x%lx -> offset: 0x%x with size %ld\n",
			 __func__,
			mapping->op.va,
			(unsigned long) ~PAGE_MASK, 
			mapping->offset,
			mapping->op.size);

	mapping->nr_pages = (mapping->offset + mapping->op.size - 1 + ~PAGE_MASK) >> PAGE_SHIFT;

	if(dma_map_check(mapping)) {
		ret = -EINVAL;
		printk("dma_map_check failed.\n");
		goto err_check;
	}
	
	//TODO allocate nr_pages pages
	mapping->page_list = kzalloc(sizeof(*mapping->page_list) * MAX_MAP_PAGES, GFP_KERNEL);
	if(!mapping->page_list) {
		ret = -ENOMEM;
		goto err_alloc;
	}

	if((ret = dma_map_buf(mapping)) < 0) {
		ret = -EIO;
		printk("Error mapping pages\n");
		goto err_map;
	}

	return ret;

err_map:
	kfree(mapping->page_list);
err_alloc:
err_check:
	return ret;
}

static iova_page_t*
find_page(struct list_head *head, unsigned int pfn)
{
	iova_page_t *page;
	list_for_each_entry(page, head, next) {
		if(page->pfn == pfn) {
			return page;
		}
	}
	return NULL;
}

/** IOMMU mappings **/

/**
 * Maps a userspace buffer to IOVA
 */
static long
dma_iommu_map(struct dma_op_list *request, struct uio_dma_device *ddev)
{
	int nr_of_pages;
	int page = 0;
	int pfn;
	
	long ret = 0;

	unsigned long virt_page_addr;

	struct dma_mapping *mapping;
	struct list_head *list;
	iova_page_t *pte;

	mapping = kzalloc(sizeof(*mapping), GFP_KERNEL);
	if(!mapping) {
		ret = -ENOMEM;
		goto err;
	}

	memcpy(mapping, request, sizeof(struct dma_op));

	nr_of_pages = dma_map_pages(mapping);
	if(nr_of_pages < 0) {
		printk("Mapping failed\n");
		ret = -ENXIO;
		/* map->page_list is already freed here */
		goto err_map_free;
	}
	
	for(page = 0; page < nr_of_pages; page++) {
		virt_page_addr = (unsigned long)((mapping->op.va & PAGE_MASK) + (page * PAGE_SIZE));
		pfn = (mapping->op.va >> PAGE_SHIFT) + page;
		
		if(debug & DEBUG_MAP)
			printk("%s: mapping page %d va 0x%lx (0x%lx) of size %ld from page 0x%lx\n", 
				__func__,
				page,
				virt_page_addr,
				mapping->op.va & PAGE_MASK,
				mapping->op.size,
				(unsigned long) page_to_phys(mapping->page_list[page]));
	
		if (iommu_iova_to_phys(ddev->domain, virt_page_addr)) {
			if(debug & DEBUG_MAP)
				printk("%s: 0x%lx already mapped to 0x%lx\n", __func__, virt_page_addr,
					(unsigned long) iommu_iova_to_phys(ddev->domain, virt_page_addr));

			/* increment ref count */
			pte = find_page(&ddev->iova_page_list, pfn);
			if(pte) {
				atomic_inc(&pte->ref_count);
			} else {
				if(debug & DEBUG_MAP)
					printk("Oops. page 0x%x already mapped, but pte not found.\n", pfn);
			}
			continue;
		}
		
		pte = kmalloc(sizeof(iova_page_t), GFP_KERNEL);
		if(!pte) {
			ret = -ENOMEM;
			goto err_alloc_pte;
		}

		ret = iommu_map(ddev->domain, virt_page_addr,
				(phys_addr_t) page_to_phys(mapping->page_list[page]), 
				get_order(PAGE_SIZE),
				IOMMU_READ | IOMMU_WRITE | ddev->iommu_flags);
		if(ret) {
			if(debug & DEBUG_ERR)
				printk("%s: return code %ld by mapping page %d\n", __func__, ret, page);
			ret = -ENXIO;
			goto err_map;
		}

		pte->pfn = pfn;
		atomic_set(&pte->ref_count, 1);
		pte->page = mapping->page_list[page];
		list_add_tail(&pte->next, &ddev->iova_page_list);
	
		if(request->op.size > 4095) {
			printk("Mapping pfn 0x%x\n", pfn);
		}
/*	
		if(request->op.size > 4095) {
			iova_page_t *p;
			printk("Adding 0x%08x to cont_head\n", pfn);
			p = kmalloc(sizeof(iova_page_t), GFP_KERNEL);
			if(!p)
				break;
			memcpy(p, pte, sizeof(iova_page_t));
			p->offset = (page) ? 0: mapping->offset;
			p->size = (page == nr_of_pages-1) ? (mapping->offset) : (PAGE_SIZE - p->offset);
			list_add_tail(&p->next, &ddev->cont_head);
		}
*/
	}

	request->op.iova = request->op.va;

	goto out;

err_map:
	/* unmap last page and free pte, fall through to unmap already mapped pages */
	iommu_unmap(ddev->domain, (mapping->op.va & PAGE_MASK) + (page * PAGE_SIZE), get_order(PAGE_SIZE));
	kfree(pte);
err_alloc_pte:
	/* unmap all other pages */
	while(page--) {
		list = ddev->iova_page_list.prev;
		printk("Error: unmapping 0x%x\n", pte->pfn);
		iommu_unmap(ddev->domain, pte->pfn << PAGE_SHIFT, get_order(PAGE_SIZE));
		list_del(list);
		kfree(pte);
	}
	/* release all pinned pages */
	for(page = 0; page < nr_of_pages; page++)
		page_cache_release(mapping->page_list[page]);
out:
	kfree(mapping->page_list);
err_map_free:
	kfree(mapping);
err:
	//kfree(request);
	return ret;

}

/**
 * Unmap userspace buffer from IOVA
 */
static long
dma_iommu_unmap(struct dma_op_list *request, struct uio_dma_device *ddev)
{
	int page;
	int nr_of_pages;

	iova_page_t *pte;

	nr_of_pages = ((request->op.iova & ~PAGE_MASK) + request->op.size - 1 + ~PAGE_MASK) >> PAGE_SHIFT;
	//for each page in the request buffer
	for(page = 0; page < nr_of_pages; page++) {
		pte = find_page(&ddev->iova_page_list, (request->op.iova >> PAGE_SHIFT) + page);
		//if there is a page entry in the list
		if(pte) {
			//printk("%s found page with pfn 0x%lx\n", __func__, (dma_req->dma_addr >> PAGE_SHIFT) + page);
			//and if the ref count is zero
			if(atomic_dec_and_test(&pte->ref_count)) {
				//printk("Unmap page 0x%x\n", pte->pfn);
				//unmap
				iommu_unmap(ddev->domain, pte->pfn << PAGE_SHIFT, get_order(PAGE_SIZE));
				//printk("%s: unmapping page 0x%x with address %p (0x%lx)\n", __func__, page_to_phys(pte->page), page_address(pte->page), dma_req->dma_addr);
				//and unpin the page
				page_cache_release(pte->page);
				//also free buffers
				list_del(&pte->next);
				kfree(pte);
				// we can't clear this here, because there might be more pages to this request.
				//request->op.iova = 0;
			}
		} else {
			//if(debug & DEBUG_ERR)
				printk("%s pte with pfn 0x%lx not found.\n", __func__, (request->op.iova >> PAGE_SHIFT) + page);
		}
	}
	return 0;
}

struct uio_dma_ops uio_iommu_ops = {
	.map = dma_iommu_map,
	.unmap = dma_iommu_unmap,
	.translate = dma_iommu_map,
	.free = dma_iommu_unmap,
};

/** DMA with bounce buffers **/

/**
 * Assigns a DMA address to userspace buffer
 * using a bounce buffer. A DMAable buffer
 * is allocated and data is copied, depending
 * on the DMA direction.
 */
static long 
dma_mem_map(struct dma_op_list *request, struct uio_dma_device *ddev)
{
	int direction;
	long ret;
	u64 start;
	u64 duration;

	struct dma_op_list *entry;

	#if 1
	/* alloc memory for resource tracking */
	if(!(entry = kzalloc(sizeof(struct dma_op_list), GFP_KERNEL))) {
		ret = -ENOMEM;
		goto err;
	}

	memcpy(entry, request, sizeof(struct dma_op_list));
	#endif

#if 0
	/* allocate dma-able memory */
	if(!(entry->kva = dma_alloc_coherent(ddev->pdev, entry->op.size, (dma_addr_t*)&entry->op.iova, GFP_KERNEL))) {
		ret = -ENOMEM;
		goto err_dma_alloc;
	}

	/* and copy the user space data to it */
	if(entry->op.direction == DDEKIT_DMA_TODEVICE) {
		start = native_read_tsc();
		copy_from_user(entry->kva, (const void*)entry->op.va, entry->op.size);
		//printk("Overwriting memory from %p to %p\n", entry->kva, entry->kva + entry->op.size);
		duration = native_read_tsc() - start;
		if(memcpy_index < 1024)
			memcpy_duration[memcpy_index++] = (duration & 0xffff) | ((u64)entry->op.size << 32);
		else
			memcpy_index = 0;
		//print_hex_dump(KERN_ERR, "sbuf ", DUMP_PREFIX_OFFSET, 16, 2, (void*)addr, dma_req->size, 0);
	}
#else
	/* this is faster.
	 * for the lulz of it, test if it is really faster or measurement bias
	 */

	/* allocate memory */
	if(!(entry->kva = kmalloc(entry->op.size, GFP_KERNEL))) {
		ret = -ENOMEM;
		// TODO: rename.
		goto err_dma_alloc;
	}

	/* set up Linux direction variable */
	if(entry->op.direction == DDEKIT_DMA_TODEVICE) {
		/* conditionally copy data to buffer */
		copy_from_user(entry->kva, (const void*)entry->op.va, entry->op.size);
		direction = PCI_DMA_TODEVICE;
	} else if(entry->op.direction == DDEKIT_DMA_FROMDEVICE) {
		direction = PCI_DMA_FROMDEVICE;
	} else {
		//TODO error handling
		direction = 0;
	}

	/* map it to IOVA space */
	entry->op.iova = pci_map_single(ddev->pci_dev, entry->kva, entry->op.size, direction);
#endif
	/* return the DMA-address to user space */
	request->op.iova = entry->op.iova;

	spin_lock(&ddev->bounce_lock);
	list_add(&entry->next, &ddev->bounce_head);
	spin_unlock(&ddev->bounce_lock);

	if(debug & DEBUG_MAP)
		printk("%s: mapping at 0x%lx (%lu)\n", __func__, request->op.iova, request->op.size);
	return 0;

err_dma_alloc:
	kfree(entry);
err:
	return ret;
}

/**
 * Frees DMA-able buffer and copies data to the
 * associated userspace buffer, depending on the
 * direction of the DMA transfer
 */
static long
dma_mem_unmap(struct dma_op_list *request, struct uio_dma_device *ddev)
{
	int loop_cnt = 0;
	struct dma_op_list *entry, *next_entry;
	
	/* are values valid */
	spin_lock(&ddev->bounce_lock);
	list_for_each_entry_safe_reverse(entry, next_entry, &ddev->bounce_head, next) {
		loop_cnt++;
		if((entry->op.iova == request->op.iova) && (entry->op.size == request->op.size)) {
			list_del(&entry->next);
			spin_unlock(&ddev->bounce_lock);
			if(entry->op.direction == DDEKIT_DMA_FROMDEVICE) {
				pci_unmap_single(ddev->pci_dev, (dma_addr_t)entry->op.iova, entry->op.size, PCI_DMA_FROMDEVICE);
				copy_to_user((void*)entry->op.va, (const void *)entry->kva, entry->op.size);
				//printk("Overwriting memory from 0x%08x to 0x%08x\n", entry->op.va, entry->op.va + entry->op.size);
			} else if (entry->op.direction == DDEKIT_DMA_TODEVICE) {
				pci_unmap_single(ddev->pci_dev, (dma_addr_t)entry->op.iova, entry->op.size, PCI_DMA_TODEVICE);
			} else {
				//TODO error handling; but this should never happen, i think ;)
			}
			kfree(entry->kva);
			//dma_free_coherent(ddev->pdev, entry->op.size, entry->kva, (dma_addr_t)entry->op.iova);
			request->op.iova = 0;
			kfree(entry);
			/* stats, maybe use systemtap? */
			if(loop_cnt < unmap_min)
				unmap_min = loop_cnt;
			if(loop_cnt > unmap_max)
				unmap_max = loop_cnt;
					return 0;
		} else if((entry->op.iova == request->op.iova) && (entry->op.size != request->op.size)) {
			printk("%s Different size: 0x%08lx %ld %ld\n", __func__, entry->op.iova, entry->op.size, request->op.size);
		}
	}

	spin_unlock(&ddev->bounce_lock);

	printk("%s: entry not found: 0x%lx (%ld)\n", __func__, request->op.iova, request->op.size);
	return -EINVAL;
}

/**
 * Returns for a contiguous memory region acquired
 * with mmap() a DMA-address.
 */
static long
dma_translate_contiguous(struct dma_op_list *request, struct uio_dma_device *ddev)
{
	struct dma_op_list *entry;
	unsigned long uva;

	uva = request->op.va;

	spin_lock(&ddev->cont_lock);
	list_for_each_entry(entry, &ddev->cont_head, next) {
		if(entry->op.va == uva) {
#if 1
			if(entry->op.size >= 4096) {
				printk(" iova: 0x%lx\n", entry->op.iova);
			}
#endif	
			request->op.iova = entry->op.iova;
			spin_unlock(&ddev->cont_lock);
			return 0;
		}
	}
	spin_unlock(&ddev->cont_lock);
	return -1;
}

/**
 * Free DMA-able memory allocated via mmap()
 */
static long
dma_free_contiguous(struct dma_op_list *request, struct uio_dma_device *ddev)
{
	unsigned long uva;
	struct dma_op_list *entry, *next_entry;
	uva = request->op.va;

	spin_lock(&ddev->cont_lock);
	list_for_each_entry_safe(entry, next_entry, &ddev->cont_head, next) {
		if(entry->op.va == uva) {
			list_del(&entry->next);
			spin_unlock(&ddev->cont_lock);
			/*
			printk("%s: unmapping 0x%lx size 0x%lx from kva 0x%lx and uva 0x%lx\n", __func__,
			   elem->pa, elem->size, elem->kva, elem->uva);
			*/
			dma_free_coherent(ddev->pdev, entry->op.size, entry->kva, (dma_addr_t)entry->op.iova);
			kfree(entry);
			request->op.va = 0;
			request->op.iova = 0;
			request->op.size = 0;
			return 0;
		}
	}
	printk("%s: could not free memory at 0x%lx\n", __func__, request->op.iova);
	spin_unlock(&ddev->cont_lock);
	return -1;

}

struct uio_dma_ops uio_bounce_ops = {
	.map = dma_mem_map,
	.unmap = dma_mem_unmap,
	.translate = dma_translate_contiguous,
	.free = dma_free_contiguous,
};

