#include <ddekit/assert.h>
#include <ddekit/pci.h>
#include <ddekit/memory.h>
#include <ddekit/printf.h>
#include <ddekit/panic.h>
#include <ddekit/interrupt.h>

#include <cstdlib>
#include <list>

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <linux/pci.h>
#include <sys/mman.h>

#ifdef __cplusplus
extern "C" {
#endif
#include <pci/pci.h>
#ifdef __cplusplus
}
#endif

#include <stdlib.h>
#include <signal.h>
#include <errno.h>

#include "Pci_bus.hh"
//#include "Pci_device.h"

#define DDE_DEVICE_ID 0x10f0

struct ddekit_pci_spec {unsigned int bus, dev, func; };
struct ddekit_pci_spec *ddekit_pci_devices = NULL;
int ddekit_pci_nr_devices;

static const char * base_path = "/sys/bus/pci/devices";
static struct pci_access *paccess;

DDEKit::Pci_bus *ddekit_pci_bus;

void parse_ddekit_pci_devices(int argc, char **argv, char **envp)
{
	char c;
	int i;
	unsigned int bus, dev, func;
	int params = 0;

	while((c = getopt(argc, argv, "d:h")) != -1) {
		if(c == 'd') {
			params++;
		}
	}

	if(!params)
		goto out;

	ddekit_pci_devices = (struct ddekit_pci_spec *) malloc(params * sizeof(struct ddekit_pci_spec));
	params = 0;
	optind = 1;

	while((c = getopt(argc, argv, "d:h")) != -1) {
		switch(c) {
		case 'h':
			fprintf(stdout, "Specify PCI device with -d bus:device.function\n");
			break;
		case 'd':
			i = sscanf(optarg, "%u:%u.%u", &bus, &dev, &func);
			if(i ==	3) {
				fprintf(stdout, "PCI device %d:%d.%d specified\n", bus, dev, func);
				ddekit_pci_devices[params].bus = bus;
				ddekit_pci_devices[params].dev = dev;
				ddekit_pci_devices[params].func = func;
				params++;
			} else {
				fprintf(stdout, "Specify PCI device with -d bus:device.function\n");
			}
			break;
		case ':':
			fprintf(stderr, "Missing argument for option -%c\n", optopt);
			break;
		case '?':
			fprintf(stderr, "Option -%c not regcognized\n", optopt);
		}
		
	}

out:
	ddekit_pci_nr_devices = params;
	optind = 1;
}
__attribute__((section(".preinit_array"))) typeof(parse_ddekit_pci_devices) *__preinit = parse_ddekit_pci_devices;

DDEKit::Pci_bus::Pci_bus()
{
	ddekit_printf("pci bus constructor\n");
	int ret;
	int i = 0;
	int dev_idx = 0;
	char *driver_path;
	struct pci_dev *device;

	DDEKit::Pci_device *pdev;

	paccess = pci_alloc();
	pci_init(paccess);
	pci_scan_bus(paccess);
	
	//_devices = (ddekit_pci_dev_t**)ddekit_simple_malloc(sizeof(ddekit_pci_dev_t**));
	for(device = paccess->devices; device; device = device->next) {
		pci_fill_info(device, PCI_FILL_IDENT | PCI_FILL_BASES | PCI_FILL_CLASS | PCI_FILL_IRQ);

		/* check if device is already bound to a driver */
		driver_path = (char *) malloc(sizeof(char) * 64);
		snprintf(driver_path, 64, "%s/0000:%02x:%02x.%d/driver", base_path, device->bus, device->dev, device->func);
		struct stat buf;
		ret = stat(driver_path, &buf);
		
		//TODO rework, so only on device is supported
		//check if device is requested

		if(ret) {
			for(dev_idx = 0; dev_idx < ddekit_pci_nr_devices; dev_idx++) {
				ddekit_printf("comparing %x:%x.%x with %x:%x.%x\n", ddekit_pci_devices[dev_idx].bus, ddekit_pci_devices[dev_idx].dev, ddekit_pci_devices[dev_idx].func, device->bus, device->dev, device->func);
				if(ddekit_pci_devices[dev_idx].bus == device->bus &&
				   ddekit_pci_devices[dev_idx].dev == device->dev &&
				   ddekit_pci_devices[dev_idx].func == device->func) {
					pdev = new Pci_device(this, device, i);	
					__devices.push_back(*pdev);
					i++;
				}
			}
		}
	}
	dump_devs();
	return;
}

char *
DDEKit::Pci_bus::nameLookup(int device_id, int vendor_id, char * dest, int len)
{
	return pci_lookup_name(paccess, dest, len,
	  PCI_LOOKUP_VENDOR | PCI_LOOKUP_DEVICE,
	  vendor_id, device_id);
}

DDEKit::Pci_bus::~Pci_bus()
{
	std::list<Pci_device>::iterator i;
	for(i = __devices.begin(); i != __devices.end(); ++i) {
		(*i).disable();
	}
	//free(driver_path);
	pci_cleanup(paccess);
}

void DDEKit::Pci_bus::dump_devs()
{
	std::list<Pci_device>::iterator i;
	for(i = __devices.begin(); i != __devices.end(); ++i) {
		(*i).name();
		(*i).dump_resources();
	}
}

int
DDEKit::Pci_bus::mmap_resource(ddekit_addr_t addr, ddekit_addr_t len, ddekit_addr_t *va)
{
	std::list<Pci_device>::iterator d;
	for(d = __devices.begin(); d != __devices.end(); ++d) {
		if((*va = (*d).mmap(addr, len)))
			return 0;
	}
	return 1;
}

EXTERN_C int
ddekit_pci_open_mem(ddekit_addr_t start, ddekit_addr_t count, ddekit_addr_t *vaddr)
{
	return ddekit_pci_bus->mmap_resource(start, count, vaddr);
}

EXTERN_C int
ddekit_pci_close_mem(ddekit_addr_t start, ddekit_addr_t count)
{
	return 0;
}

int
DDEKit::Pci_bus::request_ioport(ddekit_addr_t start, ddekit_addr_t len)
{
	ddekit_addr_t ret = 1;
	std::list<Pci_device>::iterator i;
	for(i = __devices.begin(); i != __devices.end(); i++) {
		ret = i->request_ioport(start, len);
		if(!ret)
			break;
	}
	return ret;
}

EXTERN_C int
__ddekit_pci_request_io(ddekit_addr_t start, ddekit_addr_t len)
{
	return ddekit_pci_bus->request_ioport(start, len);
}

const ddekit_pci_dev_t * 
DDEKit::Pci_bus::find(ddekit_pci_dev const * const start,
			   unsigned short bus,
                           unsigned short slot,
                           unsigned short func) 
{
	if(!start && (
                 (bus == DDEKIT_PCI_ANY_ID || 0 == bus) &&
                 (slot == DDEKIT_PCI_ANY_ID || __devices.begin()->ref->num == slot) &&
                 (func == DDEKIT_PCI_ANY_ID || 0 == func))) {
		return (*__devices.begin()).ref;
	}
	return NULL;
#if 0
	int num = (start == NULL) ? -1 : start->num;
	std::list<Pci_device>::iterator i = __devices.begin();

	if(num >= 0) {
		for(; i != __devices.end(); ++i) {
			if((*i).ref == start) {
				++i;
				break;
			}
		}
	}

	for(; i != __devices.end(); ++i) {
                if( (bus == DDEKIT_PCI_ANY_ID || 0 == bus) &&
                    (slot == DDEKIT_PCI_ANY_ID || i->ref->num == slot) &&
                    (func == DDEKIT_PCI_ANY_ID || 0 == func)) {
                        return (*i).ref;
                }

	}
	return NULL;
#endif
}
/*
const ddekit_pci_dev_t *
DDEKit::Pci_bus::to_dev(unsigned short bus, unsigned short slot, unsigned short func)
{
	std::list<Pci_device>::iterator i;
	for(i = __devices.begin(); i != __devices.end(); i++) {
    		if( (bus == i->device->bus) &&
                    (slot == i->device->dev) &&
                    (func == i->device->func)) {
                        return (*i).ref;
                }

	}
	return NULL;

}
*/

int
DDEKit::Pci_bus::pcie_register(unsigned bus, unsigned slot, unsigned func)
{
	std::list<Pci_device>::iterator i;
	for(i = __devices.begin(); i != __devices.end(); i++) {
    		if( (bus == i->device->bus) &&
                    (slot == i->device->dev) &&
                    (func == i->device->func)) {
			return i->get_uio_id();
                }

	}
	return -1;

}

int
DDEKit::Pci_bus::bind_irq(int irq)
{
	int uio;
	std::list<Pci_device>::iterator i;

	for(i = __devices.begin(); i != __devices.end(); ++i) {
		/*
		if((uio = (*i).bind_irq(irq, 0)) >= 0) {
			return uio;
		}
		*/
		return (*i).get_uio_id();
	}
	return -1;
}

void
DDEKit::Pci_bus::unbind_irq(int irq)
{
	std::list<Pci_device>::iterator i;

	for(i = __devices.begin(); i != __devices.end(); i++) {
		//(*i).uio_release(irq, 0);
	}
}

EXTERN_C const struct ddekit_pci_dev *
ddekit_pci_find_device(unsigned int *bus, unsigned int *slot, unsigned int *func,
                                         const ddekit_pci_dev_t *start)
{
	const struct ddekit_pci_dev *dev = NULL; 
        Assert(bus);
        Assert(slot);
        Assert(func);
#if 1 
        ddekit_printf("%s: start %p (slot %d)\n", __FUNCTION__, start, start ? start->num : -1);
#endif
	dev = ddekit_pci_bus->find(start, *bus, *slot, *func);
	ddekit_printf("%p\n", (void*)dev);
        if(dev) {
		*bus = 0;
		*func = 0;
		*slot = dev->num;
	}

        return dev;
}

EXTERN_C int
ddekit_pci_real_device(unsigned int *bus, unsigned int *slot, unsigned int *func)
{
	const struct ddekit_pci_dev *dev = NULL; 
        Assert(bus);
        Assert(slot);
        Assert(func);
	
	dev = ddekit_pci_bus->find(NULL, *bus, *slot, *func);
        if(dev) {
		*bus = dev->this_dev->device->bus;
		*slot = dev->this_dev->device->dev;
		*func = dev->this_dev->device->func;
	}

        return dev ? 1 : 0;
}

EXTERN_C int
ddekit_uio_register(unsigned bus, unsigned slot, unsigned func, int msi)
{
	return ddekit_pci_bus->pcie_register(bus, slot, func);
}

EXTERN_C int
ddekit_pci_bind_irq(int irq)
{
	return ddekit_pci_bus->bind_irq(irq);
}

EXTERN_C void
ddekit_pci_unbind_irq(int irq)
{
	ddekit_pci_bus->unbind_irq(irq);
}

#define PCI_INTERRUPT_LINE    0x3c
#define PCI_INTERRUPT_PIN     0x3d
#define PCI_INTERRUPT_MAX_LAT 0x3f

#define check_align(pos, len)                                                                  \
	if((pos) & ((len) - 1))                                                                \
		ddekit_printf("Unaligned PCI config space access at 0x%08x (%d)\n", pos, len); 

EXTERN_C int
ddekit_pci_read(int bus, int slot, int func, int pos, int len, ddekit_uint32_t *val)
{
	const ddekit_pci_dev_t *dev = ddekit_pci_bus->find(NULL, bus, slot, func);
	if(!dev)
		return -1;

	check_align(pos, len);
	
	
	//ddekit_printf("reading from %s (%d) position %d, length %d\n", dev->this_dev->name(), slot, pos, len);
	switch(len) {
		case 1: *val = pci_read_byte(dev->this_dev->device, pos);
			break;
		case 2: *val = pci_read_word(dev->this_dev->device, pos);
			break;
		case 4: *val = pci_read_long(dev->this_dev->device, pos);
			break;
		default:
			return -1;
	}
	if(pos == PCI_INTERRUPT_LINE) {
		ddekit_printf("Setting IRQ from %d to virtual IRQ %d\n", *val&0xff, slot&0xff);
		/* clear LSByte */
		*val &= 0xffffff00;
		/* set (virtual) IRQ */
		*val |= (slot & 0xff);
	}

	return 0;
}

EXTERN_C int ddekit_pci_write(int bus, int slot, int func, int pos, int len, ddekit_uint32_t val)
{
	const ddekit_pci_dev_t *dev = ddekit_pci_bus->find(NULL, bus, slot, func);
	if(!dev)
		return -1;

	check_align(pos, len);
	//ddekit_printf("writing to %s (%d) position %d, length %d, data %08x\n", dev->this_dev->name(), slot, pos, len, val);
	switch(len) {
		case 1: return pci_write_byte(dev->this_dev->device, pos, (ddekit_uint8_t) val);
		case 2: return pci_write_word(dev->this_dev->device, pos, (ddekit_uint16_t) val);
		case 4: return pci_write_long(dev->this_dev->device, pos, val);
	}
	return -1;
}

EXTERN_C int ddekit_pci_irq_enable(int bus, int slot, int func, int pin, int *irq)
{
  	unsigned char trigger;
	unsigned char polarity;
  	unsigned flags = 0;
	ddekit_printf("interrupt quark\n");
//	l4_uint32_t devfn = (slot << 16) | func;

//	DEBUG_MSG("devfn %lx, pin %lx", devfn, pin);
//	*irq = l4vbus_pci_irq_enable(_vbus, _root_bridge, bus, devfn, pin, &trigger, &polarity);
//	DEBUG_MSG("l4vbus_pci_irq_enable() = %d", *irq);
	
	if (*irq < 0) {
		return -1;
	}

	switch ((!!trigger) | ((!!polarity) << 1)) {
		case 0: flags = IRQF_TRIGGER_HIGH; break;
		case 1: flags = IRQF_TRIGGER_RISING; break;
		case 2: flags = IRQF_TRIGGER_LOW; break;
		case 3: flags = IRQF_TRIGGER_FALLING; break;
		default: flags = 0; break;
	}
	
	// register the trigger type in the interrupt sysbsystem
	return ddekit_irq_set_type(*irq, flags);
}

void
__ddekit_pci_deinit()
{
	ddekit_pci_bus->~Pci_bus();		
}

// TODO exit handlers already exist in ddekit. so use them
void
__ddekit_exit(int signum)
{
	exit(2);
}

EXTERN_C void ddekit_pci_init()
{
	struct sigaction action;
	action.sa_handler = __ddekit_exit;
	action.sa_flags = 0;
	sigemptyset(&action.sa_mask);

	ddekit_printf("%s\n", __func__);
	ddekit_pci_bus = new DDEKit::Pci_bus();

	atexit(__ddekit_pci_deinit);
	sigaction(SIGINT, &action, NULL);
//	exit(1);
}
