#define _XOPEN_SOURCE 500
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/io.h>

#include <ddekit/printf.h>
#include <ddekit/types.h>

#include "Pci_resource.h"
using namespace DDEKit;

//TODO add close methods, reference counting is in place
Pci_resource::Pci_resource(int num, unsigned long start, unsigned long end, unsigned long flags, const char *path)
{
	snprintf(Pci_resource::_path, 64, "%s%d", path, num);
	_num = num;
	_start = start;
	_end = end;
	_flags = flags;
	_size = _end - _start + 1;
	_va = MAP_FAILED;
	_fd = 0;
	//TODO make access atomic
	_ref_cnt = 0;
}

Pci_resource::~Pci_resource()
{
	if(_va != MAP_FAILED) {
		munmap((void *) _va, _size);
		_va = MAP_FAILED;
	}
	if(_fd)
		::close(_fd);
}

void
Pci_resource::print(void)
{
	if(_start)
		ddekit_printf("\t0x%016lx 0x%016lx 0x%016lx\n", _start, _end, _flags);
}

Pci_resource *
Pci_resource::self()
{		
	return this;
}

int
Pci_resource::in_range(unsigned long start, unsigned long size)
{
	/* check for overflow */
	if((start + _size) < start)
		return 0;

	/* lower bound */
	if(start >= _start &&
		/* upper bound */
		(start + size) <= (_start + _size)) {
		ddekit_printf("%s: found possible mapping 0x%lx(0x%lx)\n", __FUNCTION__, start, size);
		return 1;
	}
	
	return 0;
}

void
Pci_resource::close()
{
	if(_fd)
		::close(_fd);
	_fd = 0;
}

void
Pci_resource::open()
{
	_fd = ::open(_path, O_RDWR);
	if(_fd < 0) {
		ddekit_printf("%s: error (%d) opening file %s for mmap()ing: %s\n", __FUNCTION__, errno, _path, strerror(errno));
		return;
	}
}

//TODO only one access function, overload according to type?
ddekit_addr_t
Pci_resource::mmap()
{
	/* weird 64-bit thingy with sysfs, or broken card? */
	if(_flags== 0)
		return 0xdeadbabe;

	/*ioport*/
	if(_flags & 0x100)
		return -1;
	if(!_fd)
		open();

	if(_va == MAP_FAILED) {
		_va = ::mmap(NULL, _size, PROT_READ | PROT_WRITE, MAP_SHARED, _fd, 0);

		if(_va == MAP_FAILED) {
			ddekit_printf("%s: error (%d) mmap()ing file %s: %s\n", __FUNCTION__, errno, _path, strerror(errno));
		}
		ddekit_printf("%s: mmap()ed resource no %d from 0x%lx to 0x%lx to va 0x%lx\n", __FUNCTION__, _num, _start, _end, (unsigned long)_va);
	}

	if(_va != MAP_FAILED)
		/* if we mapped successfully || if we already had the area mapped,
		 * increase ref_cnt, as someone else is holding a reference to the memory
		 */
		_ref_cnt++;

	close();
	
	return (_va != MAP_FAILED) ? (ddekit_addr_t)_va : 0;
}

int
Pci_resource::request_ioport()
{
	int ret;

	/* not a port I/O region */
	if(!(_flags & 0x100))
		return -1;
		
	ret = ioperm(_start, _size, 1);
	if(ret) {
		ddekit_notify("%s: ioperm request failed (%d) %s, trying iopl()\n",__func__, errno, strerror(errno));
		ret = ::iopl(3);
		if(ret)
			ddekit_notify("%s: iopl request failed (%d) %s\n",__func__, errno, strerror(errno));
	} else

	if(!ret)
		_ref_cnt++;

	return ret;	
}
