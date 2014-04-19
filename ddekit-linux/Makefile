# configure
DDEKIT_INCLUDE=-I../ddekit_header/include

CC=gcc
CPP=g++
OS = __LINUX_SOURCE__
DEFINES = -D_POSIX_C_SOURCE=200112L -D__OPTIMIZE__
LFLAGS = -shared -Wl,-soname,libddekit.so
GENDEPFLAGS = -MD -MP -MF .dep/$(@F).d
CFLAGS = $(GENDEPFLAGS) -Wall -pedantic -std=c99 -O1 -gdwarf-2 -fPIC -DCONSOLE_DEBUG -D$(OS) $(DEFINES)
CPPFLAGS = $(GENDEPFLAGS) -Wall -Wno-write-strings -std=gnu++0x -O1 -pedantic -g -fPIC -DCONSOLE_DEBUG -D$(OS)
LIBS = -lrt -lpthread -L/usr/local/lib -lpci -lresolv -ldl
vpath %.c src
vpath %.cc src

STATIC = libddekit.a
DYNAMIC = libddekit.so

SRC_C  = condvar.c 
SRC_C += init.c 
SRC_C += initcall.c
SRC_C += interrupt.c
SRC_C += lock.c
SRC_C += panic.c 
SRC_C += printf.c 
SRC_C += resources.c
SRC_C += semaphore.c 
SRC_C += thread.c
SRC_C += timer.c
SRC_C += dma.c
SRC_C += pgtab.c 

SRC_CC =  malloc.cc
SRC_CC += memory.cc
SRC_CC += Pci_resource.cc
SRC_CC += Pci_device.cc
SRC_CC += pci.cc


OBJ_O = $(SRC_C:.c=.o)
OBJ_O += $(SRC_CC:.cc=.o)

all: $(STATIC)

$(STATIC) : $(OBJ_O)
	ar rcs $@ $(OBJ_O) 

$(DYNAMIC) : $(OBJ_O)
	echo $(SRC_C);
	echo $(SRC_CC);
	$(CPP) $(CFLAGS) $(OBJ_O) $(LIBS) $(LFLAGS)  -o $@ 

%.o : %.cc
	$(CPP) $(CPPFLAGS) $(DEF) $(DDEKIT_INCLUDE) -c -o $@ $<

%.o : %.c
	$(CC) $(CFLAGS) $(DEF) $(DDEKIT_INCLUDE) -c -o $@ $<

clean:
	rm -rf $(OBJ_O) $(DYNAMIC) $(STATIC) *.i *.s *.e *.ii
	rm -rf .dep/*

# Include the dependency files.
-include $(shell mkdir .dep 2>/dev/null) $(wildcard .dep/*)

