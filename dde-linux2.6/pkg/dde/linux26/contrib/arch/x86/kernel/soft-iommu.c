/* Fallback functions when the main IOMMU code is not compiled in. This
   code is roughly equivalent to i386. */
#include <linux/mm.h>
#include <linux/init.h>
#include <linux/pci.h>
#include <linux/string.h>
#include <linux/dma-mapping.h>
#include <linux/scatterlist.h>

#include <asm/iommu.h>
#include <asm/processor.h>
#include <asm/dma.h>

#include <ddekit/dma.h>

static int
check_addr(char *name, struct device *hwdev, dma_addr_t bus, size_t size)
{
	if (hwdev && !is_buffer_dma_capable(*hwdev->dma_mask, bus, size)) {
		if (*hwdev->dma_mask >= DMA_32BIT_MASK)
			printk(KERN_ERR
			    "soft_iommu_%s: overflow %Lx+%zu of device mask %Lx\n",
				name, (long long)bus, size,
				(long long)*hwdev->dma_mask);
		return 0;
	}
	return 1;
}

static dma_addr_t
soft_iommu_map_single(struct device *hwdev, phys_addr_t paddr, size_t size,
	       int direction)
{
	dma_addr_t bus = paddr;
	ddekit_dma_dir_t dir;

	WARN_ON(size == 0);
	if (!check_addr("map_single", hwdev, bus, size))
				return bad_dma_address;
	flush_write_buffers();

	switch(direction) {
		case DMA_TO_DEVICE: dir = DDEKIT_DMA_TODEVICE; break;
		case DMA_FROM_DEVICE: dir = DDEKIT_DMA_FROMDEVICE; break;
		case DMA_BIDIRECTIONAL: dir = DDEKIT_DMA_BIDIRECTIONAL; break;
		case DMA_NONE: dir = DDEKIT_DMA_NONE; break;
		default: dir = DDEKIT_DMA_NONE;
	}

	bus = (dma_addr_t) ddekit_dma_map_single((ddekit_addr_t) paddr,
		        (unsigned) size, dir);

	return bus;
}

static void
soft_iommu_unmap_single(struct device *dev, dma_addr_t addr,size_t size,
	        int direction)
{
	ddekit_dma_dir_t dir;

	switch(direction) {
		case DMA_TO_DEVICE: dir = DDEKIT_DMA_TODEVICE; break;
		case DMA_FROM_DEVICE: dir = DDEKIT_DMA_FROMDEVICE; break;
		case DMA_BIDIRECTIONAL: dir = DDEKIT_DMA_BIDIRECTIONAL; break;
		case DMA_NONE: dir = DDEKIT_DMA_NONE; break;
		default: dir = DDEKIT_DMA_NONE;
	}

	return ddekit_dma_unmap_single((ddekit_addr_t) addr, (unsigned) size,
		        dir);
}

/* Map a set of buffers described by scatterlist in streaming
 * mode for DMA.  This is the scatter-gather version of the
 * above pci_map_single interface.  Here the scatter gather list
 * elements are each tagged with the appropriate dma address
 * and length.  They are obtained via sg_dma_{address,length}(SG).
 *
 * NOTE: An implementation may be able to use a smaller number of
 *       DMA address/length pairs than there are SG table elements.
 *       (for example via virtual mapping capabilities)
 *       The routine returns the number of addr/length pairs actually
 *       used, at most nents.
 *
 * Device ownership issues as mentioned above for pci_map_single are
 * the same here.
 */
static int soft_iommu_map_sg(struct device *hwdev, struct scatterlist *sg,
	       int nents, int direction)
{
	struct scatterlist *s;
	int i;

	WARN_ON(nents == 0 || sg[0].length == 0);

	for_each_sg(sg, s, nents, i) {
		BUG_ON(!sg_page(s));
		s->dma_address = sg_phys(s);
		if (!check_addr("map_sg", hwdev, s->dma_address, s->length))
			return 0;
		s->dma_length = s->length;
	}
	flush_write_buffers();
	return nents;
}

static void *
soft_iommu_alloc_coherent(struct device *dev, size_t size,
	        dma_addr_t *dma_handle, gfp_t gfp)
{
	return ddekit_dma_alloc_coherent((int) size, (ddekit_addr_t *) dma_handle);
}

static void soft_iommu_free_coherent(struct device *dev, size_t size, void *vaddr,
				dma_addr_t dma_addr)
{
	return ddekit_dma_free_coherent(vaddr, size, (ddekit_addr_t)dma_addr);
}

struct dma_mapping_ops soft_iommu_dma_ops = {
	.alloc_coherent = soft_iommu_alloc_coherent,
	.free_coherent = soft_iommu_free_coherent,
	.map_single = soft_iommu_map_single,
	.unmap_single = soft_iommu_unmap_single,
	.map_sg = soft_iommu_map_sg,
	.is_phys = 1,
};

void __init soft_iommu_init(void)
{
	if (dma_ops)
		return;

	force_iommu = 0; /* no HW IOMMU */
	dma_ops = &soft_iommu_dma_ops;
}
