#include <linux/module.h>
#include <linux/init.h>
#include <linux/poll.h>
#include <linux/device.h>
#include <linux/slab.h>
#include <linux/mm.h>
#include <linux/idr.h>
#include <linux/sched.h>
#include <linux/string.h>
#include <linux/kobject.h>
#include <linux/uio_driver.h>
#include <linux/atomic.h>

#include <asm/io.h>
#include <linux/pci.h>
#include <linux/mman.h>
#include <linux/pagemap.h>
#include <linux/iommu.h>
#include <asm/page.h>
#include <linux/kernel.h>

#include "ddekit/dma.h"
#include "uio_dma.h"

#define DMA_MODE_BOUNCE 0
#define DMA_MODE_MAP    1

struct class *dma_class;
int dma_major;

static DEFINE_IDR(dma_idr);
static DEFINE_SPINLOCK(minor_lock);

int debug;

/**
 * This function switches between the two DMA modes
 * (IOMMU mapping and bounce buffers DMA).
 * ddev->domain is pre-allocated.
 */
static void
switch_dma_mode(struct uio_dma_device *ddev, int virtualize)
{
	int ret;

	/* we already are in the requested mode */
	if(ddev->virtualize == virtualize)
		return;

	ddev->virtualize = virtualize;

	if(virtualize) {
		if(iommu_found()) {
			printk("IOVA mappings enabled\n");
			if((ret = iommu_attach_device(ddev->domain, ddev->pdev))) {
				printk(KERN_ERR "IOMMU attach failed with %d\n", ret);
				printk(KERN_INFO "Falling back to bounce buffer DMA\n");
				goto bounce;
			}
			ddev->attached = 1;
			ddev->dma_ops = &uio_iommu_ops;
			return;
		} else {
			printk("IOVA mappings enabled, but no IOMMU available. Using bounce buffers.\n");
			goto bounce;
		}
	}

bounce:	
	printk("DMA using bounce buffers enabled\n");
	if(ddev->attached)
		iommu_detach_device(ddev->domain, ddev->pdev);
	ddev->dma_ops = &uio_bounce_ops;
}

static ssize_t
iommu_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	if(iommu_found())
		return sprintf(buf, "IOMMU found\n");
	else
		return sprintf(buf, "No IOMMU found\n");
}

static ssize_t
virtualize_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct uio_dma_device *ddev;

	ddev = dev_get_drvdata(dev);
	if(!ddev)
		return -EINVAL;

	if(ddev->virtualize)
		return sprintf(buf, "IOVA mappings enabled\n");
	else
		return sprintf(buf, "DMA bounce buffers enabled\n");
}

static ssize_t
virtualize_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t size)
{
	struct uio_dma_device *ddev;
	int val;

	ddev = dev_get_drvdata(dev);
	if(!ddev)
		return -EINVAL;

	if(atomic_read(&ddev->ref_cnt)) {
		printk("fds still open, cannot change DMA mode\n");
		return -EINVAL;
	}

	sscanf(buf, "%d", &val);
	if(val) {
		switch_dma_mode(ddev, DMA_MODE_MAP);
	} else {
		switch_dma_mode(ddev, DMA_MODE_BOUNCE);
	}

	return size;
}

static ssize_t debug_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t len)
{
	sscanf(buf, "%d", &debug);
	if(debug)
		printk("Debug ouput enabled at level %d\n", debug);
	else
		printk("Debug output disabled\n");
	return len;
}

static ssize_t debug_show(struct device * dev, struct device_attribute *attr, char *buf)
{
	if(debug)
		return sprintf(buf, "Debug ouput enabled at level %d\n", debug);
	else
		return sprintf(buf, "Debug output disabled\n");
}

static ssize_t ring_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t len)
{
	struct uio_dma_device *ddev;
	iova_page_t *page;
	void *addr;

	ddev = dev_get_drvdata(dev);

	list_for_each_entry(page, &ddev->cont_head, next) {
		addr = kmap(page->page);
		printk("Dumping page 0x%08x from %p with offset 0x%08x and size %d\n", page->pfn, (void*) addr, page->offset, page->size);
		print_hex_dump(KERN_DEBUG, "", DUMP_PREFIX_ADDRESS, 16, 1, addr+page->offset, page->size , false);
		kunmap(page->page);
	}

	return len;
}

static ssize_t desc_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t len)
{
	unsigned int addr;
	unsigned int n;
	unsigned char *data;
	int i;

	sscanf(buf, "%x %d\n", &addr, &n);

	data = (unsigned char*) kmalloc(n, GFP_KERNEL);
	if(!data)
		goto out;

	for(i = 0; i < len; i++)
		data[i] = readb((const volatile void*)addr + i);

	printk("Dumping %p (%d):\n", (void*)addr, n);
	print_hex_dump(KERN_DEBUG, "", DUMP_PREFIX_ADDRESS, 16, 1, data, n , false);

out:
	return len;
}

extern volatile unsigned int map_min, map_avg, map_max;
extern volatile unsigned int unmap_min, unmap_avg, unmap_max;
extern volatile u64 memcpy_duration[1024];
extern volatile int memcpy_index;

static ssize_t stats_show(struct device * dev, struct device_attribute *attr, char *buf)
{
	u32 min = UINT_MAX, max = 0, avg = 0, duration, size;
	int i;
	int n = 0;
	n += snprintf(buf+n, PAGE_SIZE - n, "Stat  min avg max\n");
	n += snprintf(buf+n, PAGE_SIZE - n, "-----------------\n");
	//n += snprintf(buf+n, PAGE_SIZE - n, "Map   %3d %3d %3d\n", map_min, map_avg, map_max);
	n += snprintf(buf+n, PAGE_SIZE - n, "Unmap %3d %3d %3d\n", unmap_min, unmap_avg, unmap_max);
	unmap_min = UINT_MAX;
	unmap_max = 0;
	for(i = 0; i < memcpy_index; i++) {
		duration = memcpy_duration[i] & 0xffff;
		size = memcpy_duration[i] >> 32;
		printk("%d %d\n", duration, size);
		if(duration < min)
			min = duration;
		if(duration > max)
			max = duration;
		avg += duration;
	}
	printk("\n");
	if(!avg)
		avg = 1;
	printk("min: %d, max: %d: avg: %d, entries: %d\n", min, max, avg/memcpy_index, memcpy_index);
	return n;
}


static DEVICE_ATTR(debug, S_IRUGO | S_IWUGO, debug_show, debug_store);
static DEVICE_ATTR(iommu, S_IRUGO, iommu_show, NULL);
static DEVICE_ATTR(virtualize, S_IRUGO | S_IWUGO, virtualize_show, virtualize_store);
static DEVICE_ATTR(ring, S_IWUGO, NULL, ring_store);
static DEVICE_ATTR(desc, S_IWUGO, NULL, desc_store);
static DEVICE_ATTR(stats, S_IRUGO, stats_show, NULL);

static struct attribute *attrs[] = {
	&dev_attr_iommu.attr,
	&dev_attr_virtualize.attr,
	&dev_attr_debug.attr,
	&dev_attr_ring.attr,
	&dev_attr_desc.attr,
	&dev_attr_stats.attr,
	NULL,
};
 
static struct attribute_group attr_grp = {
	.attrs = attrs,
};

static int
dma_open(struct inode *inode, struct file *fp)
{
	struct uio_dma_device *ddev;

	spin_lock(&minor_lock);
	ddev = idr_find(&dma_idr, iminor(inode));
	spin_unlock(&minor_lock);

	atomic_inc(&ddev->ref_cnt);

	fp->private_data = ddev;

	return 0;
}

static int
dma_release(struct inode *inode, struct file *fp)
{
	struct uio_dma_device *ddev = fp->private_data;
	iova_page_t *page, *n;
	struct dma_op_list *entry, *next_entry;
	/**
	 * decrement reference count and free all resources, if this was
	 * the last open file descriptor to this device
	 */
	if(atomic_dec_and_test(&ddev->ref_cnt)) {
		spin_lock(&ddev->cont_lock);
		list_for_each_entry_safe(entry, next_entry, &ddev->cont_head, next) {
			list_del(&entry->next);
			dma_free_coherent(ddev->pdev, entry->op.size, entry->kva, (dma_addr_t)entry->op.iova);
			kfree(entry);
		}
		spin_unlock(&ddev->cont_lock);

		spin_lock(&ddev->bounce_lock);
		list_for_each_entry_safe(entry, next_entry, &ddev->bounce_head, next) {
			list_del(&entry->next);
			
			if(entry->op.direction == DDEKIT_DMA_FROMDEVICE) {
				copy_to_user((void*)entry->op.va, (const void*)entry->kva, entry->op.size);
			}
			dma_free_coherent(ddev->pdev, entry->op.size, entry->kva, (dma_addr_t)entry->op.iova);
			entry->op.iova = 0;
			kfree(entry);
		
		}
		spin_unlock(&ddev->bounce_lock);
	
		list_for_each_entry_safe(page, n, &ddev->iova_page_list, next) {
			list_del(&page->next);
			printk("Removing page mapping 0x%x\n", page->pfn);
			if(iommu_iova_to_phys(ddev->domain, page->pfn << PAGE_SHIFT))
				iommu_unmap(ddev->domain, page->pfn << PAGE_SHIFT, get_order(PAGE_SIZE));
			else
				printk("Page 0x%x already unmapped\n", page->pfn);
			kfree(page);
		}

	}
	printk("%s: closing fd, ref_cnt now: %d\n", __func__, atomic_read(&ddev->ref_cnt));
	wake_up_interruptible(&ddev->close);
	return 0;
}


/*
static const struct vm_operations_struct uio_vm_ops = {
	.open = uio_vma_open,
	.close = uio_vma_close,
};
*/
static int
dma_mmap_coherent(struct vm_area_struct *vma, struct dma_mapping *mapping/*void *vaddr, size_t size*/)
{
	unsigned long size;
	unsigned long offset = vma->vm_pgoff, usize;
	printk("%s\n", __func__);	

	size = mapping->op.size;

	mapping->nr_pages = PAGE_ALIGN(size) >> PAGE_SHIFT;
	printk("%s: size: %lu\n", __func__, size);
	usize = (vma->vm_end - vma->vm_start) >> PAGE_SHIFT;
	printk("%s: pages: %lu\n", __func__, usize);
	printk("%s: offset: %lu\n", __func__, offset);

	if (offset >= size || usize > (size - offset)) {
		return -ENXIO;
	}

	return remap_pfn_range(vma, vma->vm_start,
		(__pa(mapping->kva) >> PAGE_SHIFT) + offset,
		usize << PAGE_SHIFT, vma->vm_page_prot);
}

static int
iommu_mmap(struct file *fp, struct vm_area_struct *vma)
{
	int ret;
	struct dma_mapping *mapping;

	struct uio_dma_device *ddev = fp->private_data;

	printk("%s\n", __func__);	
	if (vma->vm_end < vma->vm_start)
		return -EINVAL;

	if(!(mapping = kzalloc(sizeof(*mapping), GFP_KERNEL))) {
		ret = -ENOMEM;
		goto err;
	}
	
	mapping->op.size = (vma->vm_end - vma->vm_start);
	mapping->op.va = vma->vm_start;	

	if(!(mapping->kva = dma_alloc_coherent(ddev->pdev, mapping->op.size, (dma_addr_t *)&mapping->op.iova, GFP_KERNEL))) {
		ret = -ENOMEM;
		goto err_dma_alloc;
	}
	
	if((ret = dma_mmap_coherent(vma, mapping))) {
		printk("%s: dma_mmap_coherent: %d\n", __func__, ret);
		goto err_remap;
	}

	if(mapping->op.size >= 4096)
		printk("%s: 0x%lx %p 0x%lx 0x%0lx\n", __func__, mapping->op.iova, mapping->kva, mapping->op.va, __pa(mapping->kva));
	vma->vm_flags |= VM_RESERVED;
	spin_lock(&ddev->cont_lock);
	list_add(&mapping->next, &ddev->cont_head);
	spin_unlock(&ddev->cont_lock);
	
	return 0;

err_remap:
	dma_free_coherent(ddev->pdev, mapping->op.size, mapping->kva, mapping->op.iova);
err_dma_alloc:
	kfree(mapping);
err:
	return ret;
}

static long 
dma_ioctl(struct file * fp, unsigned int cmd, unsigned long arg)
{
	long ret = 0;
	
	struct dma_op_list *op;
	struct uio_dma_device *ddev = fp->private_data;
	
	if(_IOC_TYPE(cmd) != DMA_MAGIC)
		return -ENOTTY;
	
	if(_IOC_DIR(cmd) & _IOC_READ)
		ret = !access_ok(VERIFY_WRITE, (void __user *)arg, _IOC_SIZE(cmd));
	else if(_IOC_DIR(cmd) & _IOC_WRITE)
		ret = !access_ok(VERIFY_READ, (void __user *)arg, _IOC_SIZE(cmd));

	if(ret)
		return -EFAULT;

	op = (struct dma_op_list *) kzalloc(sizeof(*op), GFP_KERNEL);
	if(!op)
		return -ENOMEM;

	copy_from_user(op, (const void*)arg, sizeof(struct dma_op));
	
	switch(cmd) {
		case (DMA_MAP): {
			ret = ddev->dma_ops->map(op, ddev);
			break;
		}
		case (DMA_UNMAP): {
			ret = ddev->dma_ops->unmap(op, ddev);
			break;
		}
		case (DMA_TRANSLATE): {
			ret = ddev->dma_ops->translate(op, ddev);
			break;
		}
		case (DMA_FREE): {
			ret = ddev->dma_ops->free(op, ddev);
			break;
		}
		default: ret = -ENOTTY;
	}
	
	/**
	 * write dma information to user, if no error occured.
	 * otherwise, op is freed already
	 */
	if(!ret)
		copy_to_user((void*)arg, (const void*)op, sizeof(struct dma_op));
	
		kfree(op);

	return ret;
}

static const struct file_operations dma_fops = {
	.owner		= THIS_MODULE,
	.open		= dma_open,
	.release	= dma_release,
	.mmap		= iommu_mmap,
	.unlocked_ioctl = dma_ioctl,
};

static int
dma_minor_get(struct uio_dma_device *ddev)
{
	int ret, id;

again:
	if (idr_pre_get(&dma_idr, GFP_KERNEL) == 0)
		return -ENOMEM;
	
	spin_lock(&minor_lock);
	ret = idr_get_new(&dma_idr, ddev, &id);
	spin_unlock(&minor_lock);

	if (ret == -EAGAIN) {
		printk("Error allocating IDR, retrying ...\n");
		goto again;
	}

	ddev->minor = id & MAX_ID_MASK;

	return ret;
}

static void
dma_minor_put(struct uio_dma_device *ddev)
{
	spin_lock(&minor_lock);
	idr_remove(&dma_idr, ddev->minor);
	spin_unlock(&minor_lock);
}

static int
uio_dma_class_init(void)
{
	if(dma_class)
		return 0;

	dma_class = class_create(THIS_MODULE, "uio-dma");
	if(IS_ERR(dma_class)) {
		printk("Failed to create class uio-dma\n");
		return IS_ERR(dma_class);
	}

	return 0;
}

static void
uio_dma_class_destroy(void)
{
	if(dma_class)
		class_destroy(dma_class);
}

static int
uio_dma_iommu_init(struct uio_dma_device *ddev)
{
	if(iommu_found()) {
		printk("Initializing domain\n");
		ddev->domain = iommu_domain_alloc();
		if(!ddev->domain)
			return -ENOMEM;

		if(iommu_domain_has_cap(ddev->domain, IOMMU_CAP_CACHE_COHERENCY)) {
			printk("Cache coherency supported\n");
			ddev->iommu_flags = IOMMU_CACHE;
		} else {
			printk("Cache coherency not supported\n");
			ddev->iommu_flags = 0;
		}

		ddev->dma_ops = &uio_iommu_ops;
	} else {
		printk("No IOMMU found, using bounce buffers\n");
		ddev->dma_ops = &uio_bounce_ops;
	}

	return 0;
}

static void
uio_dma_iommu_destroy(struct uio_dma_device *ddev)
{
	if(iommu_found()) {
		if(ddev->attached)
			iommu_detach_device(ddev->domain, ddev->pdev);
		iommu_domain_free(ddev->domain);
	}
}

int
__uio_dma_register(struct module *owner, struct device *parent, struct uio_device *idev, struct uio_dma_device **ddev_ptr)
{
	int ret = 0;
	struct uio_dma_device *ddev;

	if(!owner || !parent || !idev)
		return -EINVAL;

	printk("Registering parent %p and uio_device %p with new dma device\n", (void*) parent, (void*) idev);
	printk("pci_dev: %p\n", to_pci_dev(parent));

	ddev = kzalloc(sizeof(*ddev), GFP_KERNEL);
	if(!ddev)
		return -ENOMEM;
	printk("ddev: %p\n", (void*)ddev);

	ret = dma_minor_get(ddev);
	if(ret)
		goto err_minor;
	printk("uio dma minor: %d\n", ddev->minor);

	init_waitqueue_head(&ddev->close);

	ddev->dev = device_create(dma_class, parent,
				  MKDEV(dma_major, ddev->minor), ddev,
				  "uio%d-dma", ddev->minor);
	if (IS_ERR(ddev->dev)) {
		printk(KERN_ERR "UIO: dma device register failed\n");
		ret = PTR_ERR(ddev->dev);
		goto err_dev_create;
	}

	ret = uio_dma_iommu_init(ddev);
	if(ret)
		goto err_iommu;

	ddev->pdev = parent;
	ddev->pci_dev = to_pci_dev(parent);
	// switch_dma_mode is a nop if ddev->virtualize equals the requested mode
	// to really init, ddev->virtualize has to be different, so the requested mode
	// really gets set
	ddev->virtualize = DMA_MODE_BOUNCE;
	switch_dma_mode(ddev, DMA_MODE_MAP);

	ret = sysfs_create_group(&ddev->dev->kobj, &attr_grp);
	if(ret)
		goto err_sysfs;

	ddev->uio_dev = idev;

	atomic_set(&ddev->ref_cnt, 0);
	
	spin_lock_init(&ddev->bounce_lock);
	spin_lock_init(&ddev->cont_lock);
	
	INIT_LIST_HEAD(&ddev->bounce_head);
	INIT_LIST_HEAD(&ddev->cont_head);
	INIT_LIST_HEAD(&ddev->iova_page_list);
	
	*ddev_ptr = ddev;

	return 0;

err_sysfs:
	uio_dma_iommu_destroy(ddev);
err_iommu:
//err_drvdata:
	device_destroy(dma_class, MKDEV(dma_major, ddev->minor));
err_dev_create:
	dma_minor_put(ddev);
err_minor:
	kfree(ddev);
	*ddev_ptr = NULL;
	return ret;	
}
EXPORT_SYMBOL_GPL(__uio_dma_register);

void
uio_dma_unregister(struct uio_dma_device *ddev)
{
	DECLARE_WAITQUEUE(wait, current);
	
	if(!ddev)
		return;

	printk("%s: ref_cnt: %d\n", __func__, atomic_read(&ddev->ref_cnt));	

	/* wait for all fd's closed */
	/* we need to lock refcnt operation, so we are sleeping
	 * before someone wakes us up
	 */
	//if(atomic_read(&ddev->ref_cnt)) {
		add_wait_queue(&ddev->close, &wait);
		do {
			set_current_state(TASK_INTERRUPTIBLE);
			printk("scheduling %s\n", __func__);
			schedule();
		} while (atomic_read(&ddev->ref_cnt));
		__set_current_state(TASK_RUNNING);
		remove_wait_queue(&ddev->close, &wait);
	//}

	printk("%s: iommu_destroy\n", __func__);
	uio_dma_iommu_destroy(ddev);
	printk("%s: device_destroy\n", __func__);
	device_destroy(dma_class, MKDEV(dma_major, ddev->minor));
	printk("%s: minor_put\n", __func__);
	dma_minor_put(ddev);
	printk("%s: kfree\n", __func__);
	kfree(ddev);
	printk("%s: return\n", __func__);

}
EXPORT_SYMBOL_GPL(uio_dma_unregister);

static int __init init(void)
{
	int ret;
	
	dma_major = register_chrdev(0, "uio-dma", &dma_fops);
	if(dma_major < 0) {
		ret = dma_major;
		goto err_chrdev;
	}
	printk("uio dma major: %d\n", dma_major);

	ret = uio_dma_class_init();
	if(ret)
		goto err_class;
	
	return 0;
err_class:
	unregister_chrdev(dma_major, "uio-dma");	
err_chrdev:
	return ret;
}

static void __exit cleanup(void)
{
	unregister_chrdev(dma_major, "uio-dma");
	uio_dma_class_destroy();
}

module_init(init);
module_exit(cleanup);

MODULE_VERSION("1");
MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Hannes Weisbach");
MODULE_DESCRIPTION("DMA extension for UIO");

