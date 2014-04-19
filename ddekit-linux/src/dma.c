#include <ddekit/panic.h>
#include <ddekit/printf.h>
#include <ddekit/types.h>
#include <ddekit/dma.h>

#define _XOPEN_SOURCE 500
#include <unistd.h>


#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <stdio.h>

void ddekit_dma_init(void);

ddekit_addr_t ddekit_dma_map(ddekit_addr_t, unsigned int, ddekit_dma_dir_t);
void ddekit_dma_unmap(ddekit_addr_t, unsigned int, ddekit_dma_dir_t);

static int dma_fd;

void
ddekit_dma_init()
{
	char buf[128];
	snprintf(buf, sizeof(buf), "/dev/uio%d-dma", ddekit_pci_bind_irq(0));

	dma_fd = open(buf, O_RDWR);
	if(dma_fd < 0) {
		ddekit_printf("%s: Error opening DMA mapping device %s (%d): %s\n", __FUNCTION__, buf, errno, strerror(errno));
	}
}

void
ddekit_dma_unmap_single(ddekit_addr_t pa, unsigned int size, ddekit_dma_dir_t direction)
{
	int ret;
	struct dma_op dma;

	dma.iova = (unsigned long)pa;
	dma.size = (unsigned long)size;
	dma.direction = direction;
	
	ret = ioctl(dma_fd, DMA_UNMAP, (unsigned long)&dma);
	if(ret)
		ddekit_fatal("%s: ioctl returned (%d): %s\n", __FUNCTION__, errno, strerror(errno));
}

ddekit_addr_t
ddekit_dma_map_single(ddekit_addr_t virt, unsigned int size, ddekit_dma_dir_t direction)
{
	int ret;
	struct dma_op dma;

	dma.direction = direction;
	dma.size = (unsigned long)size;
	dma.va = (unsigned long)virt;
	dma.iova = (unsigned long)0;

	ret = ioctl(dma_fd, DMA_MAP, (unsigned long)&dma);
	
	if(ret)
		ddekit_fatal("%s: ioctl returned (%d): %s\n", __FUNCTION__, errno, strerror(errno));
	
	if(!dma.iova)
		ddekit_fatal("%s: mapping failed\n");

	return (ddekit_addr_t)dma.iova;
}

