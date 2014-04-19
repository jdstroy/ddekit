#pragma once
#include "ddekit/dma.h"

struct uio_device;
struct uio_dma_device;

/*
struct kmem_page_mem {
	unsigned long kva;
	unsigned long uva;
	unsigned long pa;	
	unsigned long size;
	struct list_head next;
};
*/

struct dma_op_list {
	struct dma_op    op;
	void             *kva;
	struct list_head next;
};

struct dma_mapping {
	struct dma_op    op;
	void             *kva;
	struct list_head next;
	int              offset;
	int              nr_pages;
	struct page      **page_list;
};

typedef struct iova_page {
	unsigned int pfn;
	unsigned int offset;
	unsigned int size;
	atomic_t ref_count;
	struct page *page;
	struct list_head next;
} iova_page_t;

struct uio_dma_ops {
	long (*map)(struct dma_op_list *request, struct uio_dma_device *);
	long (*unmap)(struct dma_op_list *request, struct uio_dma_device *);
	long (*translate)(struct dma_op_list *request, struct uio_dma_device *);
	long (*free)(struct dma_op_list *request, struct uio_dma_device *);
};

struct uio_dma_device {
	struct uio_device       *uio_dev;
	struct device           *dev;
	struct device           *pdev;
	struct pci_dev          *pci_dev;
	struct uio_dma_ops      *dma_ops;
	struct iommu_domain     *domain;
	int                     iommu_flags;
	int                     minor;
	int                     virtualize;
	int                     attached;
	atomic_t                ref_cnt;
	wait_queue_head_t       close;

	struct list_head        iova_page_list;
	struct list_head        bounce_head;
	spinlock_t              bounce_lock;
	struct list_head        cont_head;
	spinlock_t              cont_lock;
};

extern int __must_check
	__uio_dma_register(struct module *,
	                   struct device *,
	                   struct uio_device *,
	                   struct uio_dma_device **);

static inline int __must_check
uio_dma_register(struct device *dev, struct uio_device *idev, struct uio_dma_device **ddev)
{
	return __uio_dma_register(THIS_MODULE, dev, idev, ddev);
}

extern void uio_dma_unregister(struct uio_dma_device *);

extern struct uio_dma_ops uio_iommu_ops;
extern struct uio_dma_ops uio_bounce_ops;

extern int debug;


