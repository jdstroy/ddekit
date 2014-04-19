#pragma once

#include <cstddef>

#include <ddekit/types.h>

namespace DDEKit
{
	class Pci_resource 
	{
		public:
			explicit Pci_resource(int num, unsigned long start, unsigned long end, unsigned long flags, const char * path);
			~Pci_resource();
			//void open(unsigned long start, unsigned long size);
			//int mmap(ddekit_addr_t *dest);
			void print(void);
			ddekit_addr_t mmap();
			Pci_resource * self();
			int request_ioport();
			int in_range(unsigned long start, unsigned long size);
		private:
			void close();
			void open();

			int _num;
			int _fd;
			char _path[64];
			unsigned long _start;
			unsigned long _end;
			unsigned long _size;
			unsigned long _flags;
			void *_va;
			int _ref_cnt;
	};
}
