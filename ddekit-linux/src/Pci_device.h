#pragma once
#include <list>
#include <ddekit/compiler.h>

EXTERN_C_BEGIN
#include <pci/pci.h>
EXTERN_C_END

#include "Pci_resource.h"

typedef struct ddekit_pci_dev ddekit_pci_dev_t;

namespace DDEKit
{
	class Pci_bus;
	class Pci_device /*: public DDEKitObject*/
	{
			static char *new_id_msi;
			static char *new_id;
		public:
			explicit Pci_device(Pci_bus *bus, struct pci_dev * dev, int num);
			~Pci_device();
		//	Pci_device& operator=(Pci_device &rhs);
			void name();
			int get_uio_id();
			void dump_resources();
			ddekit_addr_t  mmap(ddekit_addr_t, ddekit_addr_t);
			ddekit_pci_dev_t *ref;
			struct pci_dev * device;
			void disable(void);
			Pci_resource * get_resource(ddekit_addr_t, ddekit_addr_t);
			int request_ioport(ddekit_addr_t, ddekit_addr_t);
		private:
			int msi;
			int bound;
			int bus;
			int slot;
			int fun;
			int uio_id;
			char * sysfs_path;
			char * device_name;
			char * pci_name;
			std::list<Pci_resource> resource;
			void init_resources(void);
			int uio_register(int);
			void uio_release(int, int);
			int bind_irq(int irq, int);
	};
	

}

typedef struct ddekit_pci_dev {
	int num;
	DDEKit::Pci_device *this_dev;
} ddekit_pci_dev_t;

