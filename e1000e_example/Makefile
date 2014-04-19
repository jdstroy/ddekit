#path to ddekit-linux directory
DDEKIT_DIR=../ddekit-linux

#path to dde-linux2.6 directory
DDEDIR=../dde-linux2.6
#path to dde-linux2.6 build directory
DDE_BUILD=../build
#dde-linux2.6 CPU configuration string
DDE_CPU=OBJ-x86_586-l4f


DDEOBJ_DIR=$(DDE_BUILD)/pkg/dde/linux26/lib/src/$(DDE_CPU)
GENDEPFLAGS = -MD -MP -MF .dep/$(@F).d

DDE_INC=$(DDE_BUILD)/include

#dde-linux2.6 includes
INCDIRS=-I$(DDE_INC) \
	-I$(DDE_INC)/x86/l4/dde/linux26 \
	-I$(DDE_INC)/l4/dde/linux26 \
	-I$(DDE_INC)/l4/dde/linux26/linux-headers \
	-I$(DDE_INC)/l4/dde/linux26/linux-headers/asm/mach-default \

LDDIRS= -L$(DDEKIT_DIR) \
	-L$(DDEOBJ_DIR)

INCFILES= -include linux/autoconf.h
KBUILD_DEFINES = -D"KBUILD_STR(s)=\#s" \
                 -D"KBUILD_BASENAME=KBUILD_STR($(patsubst %.o,%,$(notdir $@)))" \
                 -D"KBUILD_MODNAME=KBUILD_STR($(patsubst %.o,%,$@))"

DEFS=-D__KERNEL__ -DDDE_LINUX $(KBUILD_DEFINES) -DARCH_x86 

LIBS= -Wl,-Bdynamic -lpci -lpthread -lrt -ldl -luuid
SYMS= -Wl,--defsym=__l4sys_invoke_indirect=.

LDLIBS=-Wl,--start-group \
	-Wl,-Bstatic \
	-ldde-linux26 \
	-ldde-linux26_net \
	-lddekit \
	$(LIBS) \
	-Wl,--end-group

SRC_C = main.c \
	netdev.c \
	ethtool.c \
	param.c \
	82571.c \
	ich8lan.c \
	80003es2lan.c \
	mac.c\
	nvm.c\
	phy.c \
	manage.c\
	kcompat.c\
	handle.c

STD_C = tap.c

OBJ_C = $(SRC_C:.c=.o)

DEPDIRS= $(DDEKIT_DIR) $(DDEDIR)

.PHONY : clean subdirs $(DEPDIRS) all dependencies depclean e1000.elf

all: dependencies e1000.elf

dependencies:
	@for i in $(DEPDIRS); do \
		echo "make in $$i ..."; \
        	(cd $$i; make O=$(DDE_BUILD) all); \
	done

run: e1000.elf
	./e1000.elf

lls: e1000.elf
	objdump -C -h -S $@ > $@.lss

pp:
	gcc -E -P -x c $(DEFS) main.c $(INCDIRS) $(INCFILES) $(LIBDIRS) > e1000.i

e1000.elf: $(OBJ_C) dependencies
	gcc -c -Wall -g tap.c -o tap.o
	g++ -Tddekit.ld $(OBJ_C) tap.o -o $@ $(GENDEPFLAGS) -ggdb -g3 -Wl,-gc-sections $(DDEO) $(LDDIRS) $(LDLIBS) $(SYMS)

%.o : %.c
	gcc -c $< -ggdb -g3  -ffunction-sections $(GENDEPFLAGS) -D__OPTIMIZE__ $(DEFS) $(INCDIRS) $(INCFILES) -o $@

clean:
	rm -rf *.o
	rm -rf e1000.elf
	rm -rf .dep/*

depclean:
	@for i in $(DEPDIRS); do \
		echo "make in $$i..."; \
        	(cd $$i; make O=/home/hannes/$(DDE_BUILD) clean); \
	done 

# Include the dependency files.
-include $(shell mkdir .dep 2>/dev/null) $(wildcard .dep/*)

