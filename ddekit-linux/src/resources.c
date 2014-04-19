#include <ddekit/resources.h>
#include <ddekit/printf.h>

#include <errno.h>
#include <string.h>
#include <sys/io.h>

extern int ddekit_pci_open_mem(ddekit_addr_t, ddekit_addr_t, ddekit_addr_t *);
extern void ddekit_pci_close_mem(ddekit_addr_t, ddekit_addr_t);
extern int ddekit_pci_open_io(ddekit_addr_t, ddekit_addr_t);
extern int ddekit_pci_close_io(ddekit_addr_t, ddekit_addr_t);

int ddekit_request_dma(int nr __attribute__((unused))) {
	return -1;
}

int ddekit_release_dma(int nr __attribute__((unused))) {
	return -1;
}

/** Request an IO region
 *
 * \return 0 	success
 * \return -1   error
 */
int ddekit_request_io(ddekit_addr_t start, ddekit_addr_t count) {
	int ret;
	ddekit_info("requesting region : %p (%d)\n", (void *)start, count);
	return __ddekit_pci_request_io(start, count);
}

/** Release an IO region.
 *
 * \return 0 	success
 * \return <0   error
 */
int ddekit_release_io(ddekit_addr_t start, ddekit_addr_t count) {
	int ret;
	ddekit_notify("releasing region : %p (%d)", (void *)start, count);
	ret = ioperm(start, count, 0);
	if(ret != 0)
		ddekit_urgent("%s: io release failed (%d) %s\n",__func__, errno, strerror(errno));
	return ret;
	//return ddekit_pci_close_io(start, count);
}

/** Request a memory region.
 *
 * \return vaddr virtual address of memory region
 * \return 0	 success
 * \return -1	 error
 */
int ddekit_request_mem(ddekit_addr_t start, ddekit_addr_t count, ddekit_addr_t *vaddr) {
	int i = ddekit_pci_open_mem(start, count, vaddr);
	ddekit_notify("requesting iomem : %p (%x) mapped at 0x%lx\n", (void *)start, count, *vaddr);
	return i;
}

/** Release memory region.
 *
 * \return 0 success
 * \return <0 error
 */
int ddekit_release_mem(ddekit_addr_t start, ddekit_addr_t count) {
	ddekit_notify("%s: releasing iomem 0x%lx (%x)\n", __FUNCTION__, start, count);
	ddekit_pci_close_mem(start, count);
	return 0;
}
