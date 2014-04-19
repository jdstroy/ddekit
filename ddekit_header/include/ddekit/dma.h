#pragma once

#include <ddekit/types.h>
#include <ddekit/compiler.h>
#include <linux/ioctl.h>

#define DMA_MAGIC 0xaf

#define DDEKIT_DMA_FROMDEVICE    1
#define DDEKIT_DMA_TODEVICE      2
#define DDEKIT_DMA_BIDIRECTIONAL 3
#define DDEKIT_DMA_NONE          4

#define DMA_MAP         _IOWR(DMA_MAGIC, 1, struct dma_op)
#define DMA_UNMAP       _IOWR(DMA_MAGIC, 2, struct dma_op)
#define DMA_TRANSLATE   _IOWR(DMA_MAGIC, 3, struct dma_op)
#define DMA_FREE        _IOWR(DMA_MAGIC, 4, struct dma_op)
#define DMA_IOMMU_MAP   _IOWR(DMA_MAGIC, 5, struct dma_op)
#define DMA_IOMMU_UNMAP _IOWR(DMA_MAGIC, 6, struct dma_op)

typedef unsigned int ddekit_dma_dir_t;

/*enum ddekit_dma_dir_t {
	DDEKIT_DMA_FROMDEVICE,
	DDEKIT_DMA_TODEVICE
};
*/
struct dma_op {
	unsigned long va;
	unsigned long iova;
	unsigned long size;
	unsigned int  direction;
};

ddekit_addr_t ddekit_dma_map_single(ddekit_addr_t, unsigned int, ddekit_dma_dir_t);
void ddekit_dma_unmap_single(ddekit_addr_t, unsigned int, ddekit_dma_dir_t);
EXTERN_C void * ddekit_dma_alloc_coherent(int, ddekit_addr_t *);
EXTERN_C void ddekit_dma_free_coherent(void *, int, ddekit_addr_t);
