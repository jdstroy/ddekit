#include <iostream>
#include <fstream>

#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <dirent.h>

#include <ddekit/printf.h>
#include <ddekit/memory.h>

#include "Pci_device.h"
#include "Pci_resource.h"
#include "Pci_bus.hh"

#define MSI 0
using namespace DDEKit;

static const char * base_path = "/sys/bus/pci/devices";

char *Pci_device::new_id_msi = "/sys/bus/pci/drivers/uio_pcie_generic/new_id";
char *Pci_device::new_id = "/sys/bus/pci/drivers/uio_pci_generic/new_id";

Pci_device::Pci_device(Pci_bus *bus, struct pci_dev * dev, int num)
{
	//assign pci_device
	device = dev;
	/* pci device human readable name */	
	device_name = (char *)ddekit_simple_malloc(128);
	bus->nameLookup(device->device_id, device->vendor_id, device_name, 128);
	
	//read device name
	/* size of fully qualified pci name + '\0' */
	pci_name = (char *) ddekit_simple_malloc(sizeof(char) * 13);
	snprintf(pci_name, 13, "0000:%02x:%02x.%d", device->bus, device->dev, device->func);
	
	//construct sysfs path
	/* create sysfs basepath for device access */
	int path_len = strlen(base_path) + strlen(pci_name) + 3;
	sysfs_path = (char *) ddekit_simple_malloc(sizeof(char) * path_len); 
	snprintf(sysfs_path, path_len, "%s/%s", base_path, pci_name);

	//read resources
	init_resources();

	uio_register(0);

	ref = (ddekit_pci_dev_t *) ddekit_simple_malloc(sizeof(*ref));
	ref->num = num;
	ref->this_dev = this;
}

Pci_device::~Pci_device()
{
	int fd, ret=0;
	ddekit_printf("destructing %s\n", device_name);
	//if(uio_fd)
	{
		#if MSI
		fd = open("/sys/bus/pci/drivers/uio_pcie_generic/unbind", O_WRONLY);
		#else
		fd = open("/sys/bus/pci/drivers/uio_pci_generic/unbind", O_WRONLY);
		#endif
		if(fd < 0) {
			ddekit_printf("%s: error opening bind %d: (%d): %s\n", __FUNCTION__, fd, errno, strerror(errno));
			goto out;
		}
		//ret = pwrite(fd, pci_name, 12, 0);
		if(ret < 0) {
			ddekit_printf("%s: error writing bind %d: (%d): %s (%s)\n", __FUNCTION__, ret, errno, strerror(errno), pci_name);
		}
		close(fd);
	}
out:
	ddekit_simple_free(device_name);
	ddekit_simple_free(pci_name);
	ddekit_simple_free(sysfs_path);
	ddekit_simple_free(ref);
}

int
Pci_device::get_uio_id()
{
	return uio_id;
}

void
Pci_device::init_resources()
{
	unsigned long start, end, flags;
	int i = 0;
	char path[64];
	Pci_resource *res; 
	snprintf(path, 64, "/sys/bus/pci/devices/%s/resource", pci_name);
	std::ifstream resources(path, std::ios::in);
	if(resources.fail()) {
		ddekit_printf("%s: Error openening file %s for reading\n", __func__, path);
		return;
	}
	
	while(!resources.eof())	{
		resources >> std::hex >> start >> std::hex >> end >> std::hex >> flags;
		/* MEM == 0x200, IO == 0x100 */
		if((flags & 0x300)) {
			res = new Pci_resource(i, start, end, flags, path);
			resource.push_back(*res);
		}
		i++;
	}
}

Pci_resource *
Pci_device::get_resource(ddekit_addr_t start, ddekit_addr_t len)
{
	std::list<Pci_resource>::iterator r;
	for(r = resource.begin(); r != resource.end(); r++) {
		if(r->in_range(start, len))
			/* wtf, c++ ?! */
			return &*r;
	}
	return NULL;
}

ddekit_addr_t 
Pci_device::mmap(ddekit_addr_t addr, ddekit_addr_t len)
{
	Pci_resource *resource = get_resource(addr, len);
	if(resource)
		return resource->mmap();
	return NULL;
}

int
Pci_device::request_ioport(ddekit_addr_t start, ddekit_addr_t len)
{
	Pci_resource *resource = get_resource(start, len);
	if(resource)
		return resource->request_ioport();
	return -1;
/*
	ddekit_addr_t ret = 1;
	std::list<Pci_resource>::iterator r;
	for(r = resource.begin(); r != resource.end(); r++) {
		ret = (*r).is_portio_range(start, len);
		if(!ret)
			break;
	}
	return ret;
*/
}

int
Pci_device::uio_register(int msi)
{
	int fd, ret;
	char id_string[16];
	char uio_path[128];
	struct stat buf;
	
	DIR *dp;
	struct dirent *ep;     
	/* bind uio driver to device */
	//echo "8086 10f5" > /

	snprintf(id_string, 11, "%04x %04x\n", device->vendor_id, device->device_id);
	if(msi)
		fd = open(new_id_msi, O_WRONLY);
	else
		fd = open(new_id, O_WRONLY);

	if(fd < 0) {
		ddekit_printf("error opening new_id %d: (%d): %s\n", fd, errno, strerror(errno));
		goto out;
	}
	ret = write(fd, id_string, 10);
	if(ret < 0) {
		ddekit_printf("error writing new_id %d: (%d): %s\n", ret, errno, strerror(errno));
		goto out1;
	}
	close(fd);
	//FIXME: it seems, new_id does bind the device ...
	/* look, if uio subdir in device sysfs-dir exists
	 * if not, bind this device, else skip
	 * finally, read the uio device number and return it.
	 */
	snprintf(uio_path, 128, "%s/uio", sysfs_path);
	ret = stat(uio_path, &buf);
	
	ddekit_printf("%s: stat %s return %d (%d): %s\n", __FUNCTION__, uio_path, ret, errno, strerror(errno));

	if(ret) {
		if(msi)
			fd = open("/sys/bus/pci/drivers/uio_pcie_generic/bind", O_WRONLY);
		else
			fd = open("/sys/bus/pci/drivers/uio_pci_generic/bind", O_WRONLY);
		
		if(fd < 0) {
			ddekit_printf("error opening bind %d: (%d): %s\n", fd, errno, strerror(errno));
			goto out;
		}
		ret = pwrite(fd, pci_name, 12, 0);
		if(ret < 0) {
			ddekit_printf("error writing bind %d: (%d): %s\n", ret, errno, strerror(errno));
			goto out1;
		}
		close(fd);
		//echo -n 0000:00:19.0 > 
	}
	bound = 1;
	if(msi)
		this->msi = 1;
	ddekit_info("%s: %s (%s) registered with %s", __FUNCTION__, device_name, id_string, (msi) ? "uio_pcie_generic" : "uio_pci_generic");
	
	snprintf(uio_path, sizeof(uio_path), "%s/uio", sysfs_path);
	dp = opendir(uio_path);

	if(dp != NULL) {
		while((ep = readdir(dp))) {
			if(!(strncmp("uio", ep->d_name, 3)))
				sscanf(ep->d_name, "uio%d", &uio_id);
		}
	} else {
		ddekit_info("uio registering failed. no uio directory found.\n");
	}
	
	return uio_id;
out1:
	close(fd);
	ddekit_printf("Device %s is probably not PCI 2.3 compliant.\n", device_name);
out:
	return -1;
}

void
Pci_device::disable()
{
	uint16_t val;
	//if((device->device_id == DDE_DEVICE_ID))
	{
		val = pci_read_word(device, 4);
		ddekit_printf("Setting Command register of %x %x (%s) from %x to", device->vendor_id, device->device_id, device_name, val);
		val &= ~(1 << 2);
		ddekit_printf(" %x\n", val);
		pci_write_word(device, 4, val);
	}

}

int
Pci_device::bind_irq(int irq, int msi)
{
//	if(irq != device->irq)
	//if(device->device_id != DDE_DEVICE_ID || device->func != 0)
	//	return -1;
	#if 0
	if(bound)
		/* return uio id */
		return 0;
	#endif
	ddekit_printf("%s: binding requested irq %d to device irq %d\n", __FUNCTION__, irq, device->irq);
	return uio_id;
}

void
Pci_device::uio_release(int irq, int msi)
{
	int fd, ret;
	if(bound) {
		if(msi)
			fd = open("/sys/bus/pci/drivers/uio_pcie_generic/unbind", O_WRONLY);
		else
			fd = open("/sys/bus/pci/drivers/uio_pci_generic/unbind", O_WRONLY);
		
		if(fd < 0) {
			ddekit_printf("%s: error opening bind %d: (%d): %s\n", __FUNCTION__, fd, errno, strerror(errno));
			return;
		}
		ret = pwrite(fd, pci_name, 12, 0);
		if(ret < 0) {
			ddekit_printf("%s: error writing bind %d: (%d): %s (%s)\n", __FUNCTION__, ret, errno, strerror(errno), pci_name);
		}
		close(fd);
		bound = 0;
		this->msi = 0;
	}
}

void
Pci_device::name()
{
	ddekit_printf("%s\n", device_name);
	ddekit_printf("on bus: %s, on virtual bus: %d\n", pci_name, ref->num);
}

void
Pci_device::dump_resources()
{
	std::list<Pci_resource>::iterator i;
	for(i = resource.begin(); i != resource.end(); i++) {
		(*i).print();
	}
}

/*
Pci_device&
Pci_device::operator=(Pci_device &rhs)
{
	device = rhs.device;
	return *this;
}*/
