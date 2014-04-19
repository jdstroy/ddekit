#pragma once

#ifdef __cplusplus
extern "C" {
#endif
#include <pci/pci.h>
#ifdef __cplusplus
}
#endif

#include <list>
#include <cstddef>
#include <ddekit/types.h>
#include "Pci_device.h"

/*
static inline void unpack_devfn(char devfn, unsigned *dev, unsigned *fn)
{
	Assert(dev && fn);

	*dev = devfn >> 3;
	*fn  = devfn & 0x3;
}
*/

namespace DDEKit
{
	class Pci_bus
	{
		public:
			explicit Pci_bus();
			~Pci_bus();

			// XXX: operator[]
			//ddekit_pci_dev_t* const  get(unsigned idx) const;
			ddekit_pci_dev const * find(ddekit_pci_dev const * const start = 0,
						   unsigned short bus = ~0,
			                           unsigned short slot = ~0,
			                           unsigned short func = ~0);
			int mmap_resource(ddekit_addr_t, ddekit_addr_t, ddekit_addr_t *va);
			int munmap_resource(ddekit_addr_t, ddekit_addr_t);
			char * nameLookup(int device_id, int vendor_id, char * dest, int len);
			int bind_irq(int);
			void unbind_irq(int);
			int pcie_register(unsigned, unsigned, unsigned);
			const ddekit_pci_dev_t* to_dev(unsigned, unsigned, unsigned);
			int request_ioport(ddekit_addr_t , ddekit_addr_t);
			//void port_io(int port, int *val, int len, bool write);
		private:
			//Pci_bus(Pci_bus const &) { }
			//Pci_bus& operator=(Pci_bus const &) { return *this; }
#if 1
			std::list<Pci_device> __devices;
			void dump_devs() ;
#endif
	};

}
