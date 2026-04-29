#
# Makefile.am - build a kernel+filesystem image for stand-alone Linux
# booting
#
# Copyright (C) 2012 ARM Limited. All rights reserved.
#
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE.txt file.

SCRIPT_DIR	:= $(top_srcdir)/scripts

# test-cmd cmd,val-if-success,val-if-success,val-if-failure
define test-cmd
$(if $(shell { $(1); } >/dev/null 2>&1 && echo "success"),$(2),$(3))
endef

# Run dtc with an given command line option to check support for it.
define test-dtc-option
$(call test-cmd,echo "/dts-v1/;/{};" | $(DTC) $(1) -o /dev/null,$(1),)
endef

define test-ld-option
$(call test-cmd,$(LD) $(1) --help,$(1),)
endef

# VE
PHYS_OFFSET	:= $(shell perl -I $(SCRIPT_DIR) $(SCRIPT_DIR)/findmem.pl $(KERNEL_DTB))
UART_BASE	:= $(shell perl -I $(SCRIPT_DIR) $(SCRIPT_DIR)/findbase.pl $(KERNEL_DTB) 0 'arm,pl011')
SYSREGS_BASE	:= $(shell perl -I $(SCRIPT_DIR) $(SCRIPT_DIR)/findbase.pl $(KERNEL_DTB) 0 'arm,vexpress-sysreg' 2> /dev/null)
COUNTER_FREQ	:= 100000000

CPU_IDS		:= $(shell perl -I $(SCRIPT_DIR) $(SCRIPT_DIR)/findcpuids.pl $(KERNEL_DTB))
NR_CPUS         := $(shell echo $(CPU_IDS) | tr ',' ' ' | wc -w)

DEFINES		= -DCOUNTER_FREQ=$(COUNTER_FREQ)
DEFINES		+= -DCPU_IDS=$(CPU_IDS)
DEFINES		+= -DNR_CPUS=$(NR_CPUS)
DEFINES		+= $(if $(SYSREGS_BASE), -DSYSREGS_BASE=$(SYSREGS_BASE), )
DEFINES		+= -DUART_BASE=$(UART_BASE)
DEFINES		+= -DSTACK_SIZE=256

if BOOTWRAPPER_64R
DEFINES		+= -DBOOTWRAPPER_64R
endif

if KERNEL_32
DEFINES		+= -DKERNEL_32
PSCI_CPU_ON	:= 0x84000003
else
PSCI_CPU_ON	:= 0xc4000003
endif
PSCI_CPU_OFF	:= 0x84000002

COMMON_SRC	:= common/
COMMON_OBJ	:= boot.o bakery_lock.o platform.o lib.o init.o

ARCH_OBJ	:= boot.o stack.o utils.o init.o

if BOOTWRAPPER_32
CPPFLAGS	+= -DBOOTWRAPPER_32
CFLAGS		+= -marm -mno-unaligned-access
ARCH_SRC	:= arch/aarch32/
else
CFLAGS		+= -mgeneral-regs-only -mstrict-align
ARCH_SRC	:= arch/aarch64/
endif

if PSCI
DEFINES		+= -DPSCI
ARCH_OBJ	+= psci.o
COMMON_OBJ	+= psci.o
PSCI_NODE	:= psci {				\
			compatible = \"arm,psci\";	\
			method = \"$(PSCI_METHOD)\";	\
			cpu_on = <$(PSCI_CPU_ON)>;	\
			cpu_off = <$(PSCI_CPU_OFF)>;	\
		   };
CPU_NODES	:= $(shell perl -I $(SCRIPT_DIR) $(SCRIPT_DIR)/addpsci.pl $(KERNEL_DTB))
else
ARCH_OBJ	+= spin.o
PSCI_NODE	:=
CPU_NODES	:=
endif

if GICV3
GIC_DIST_BASE	:= $(shell perl -I $(SCRIPT_DIR) $(SCRIPT_DIR)/findbase.pl $(KERNEL_DTB) 0 'arm,gic-v3')
GIC_RDIST_BASE	:= $(shell perl -I $(SCRIPT_DIR) $(SCRIPT_DIR)/findbase.pl $(KERNEL_DTB) 1 'arm,gic-v3')
DEFINES		+= -DGIC_DIST_BASE=$(GIC_DIST_BASE)
DEFINES		+= -DGIC_RDIST_BASE=$(GIC_RDIST_BASE)
COMMON_OBJ	+= gic-v3.o
else
GIC_DIST_BASE	:= $(shell perl -I $(SCRIPT_DIR) $(SCRIPT_DIR)/findbase.pl $(KERNEL_DTB) 0 'arm,cortex-a15-gic')
GIC_CPU_BASE	:= $(shell perl -I $(SCRIPT_DIR) $(SCRIPT_DIR)/findbase.pl $(KERNEL_DTB) 1 'arm,cortex-a15-gic')
DEFINES		+= -DGIC_CPU_BASE=$(GIC_CPU_BASE)
DEFINES		+= -DGIC_DIST_BASE=$(GIC_DIST_BASE)
COMMON_OBJ	+= gic.o
endif

if KERNEL_32
MBOX_OFFSET	:= 0x7ff8
TEXT_LIMIT	:= 0x3000
KERNEL_OFFSET	:= 0x8000
else
MBOX_OFFSET	:= 0xfff8
TEXT_LIMIT	:= 0x80000
KERNEL_OFFSET	:= $(shell perl -I $(SCRIPT_DIR) $(SCRIPT_DIR)/aa64-load-offset.pl $(KERNEL_IMAGE) $(TEXT_LIMIT))
endif

LD_SCRIPT	:= model.lds.S

FS_OFFSET	:= 0x10000000
FILESYSTEM_START:= $(shell echo $$(($(PHYS_OFFSET) + $(FS_OFFSET))))
FILESYSTEM_SIZE	:= $(shell stat -Lc %s $(FILESYSTEM) 2>/dev/null || echo 0)
FILESYSTEM_END	:= $(shell echo $$(($(FILESYSTEM_START) + $(FILESYSTEM_SIZE))))

FDT_OFFSET	:= 0x08000000

if XEN
XEN		:= -DXEN=$(XEN_IMAGE)
XEN_OFFSET	:= 0x08200000
KERNEL_SIZE	:= $(shell stat -Lc %s $(KERNEL_IMAGE) 2>/dev/null || echo 0)
DOM0_OFFSET	:= $(shell echo $$(($(PHYS_OFFSET) + $(KERNEL_OFFSET))))
XEN_CHOSEN	:= xen,xen-bootargs = \"$(XEN_CMDLINE)\";		\
		   \#address-cells = <2>;				\
		   \#size-cells = <2>;					\
		   module@1 {						\
			compatible = \"xen,linux-zimage\", \"xen,multiboot-module\"; \
			reg = <0x0 $(DOM0_OFFSET) 0x0 $(KERNEL_SIZE)>;	\
		   };
DEFINES		+= -DXEN
endif

if INITRD
INITRD_FLAGS	:= -DUSE_INITRD
INITRD_CHOSEN   := linux,initrd-start = <$(FILESYSTEM_START)>;	\
		   linux,initrd-end = <$(FILESYSTEM_END)>;
endif

CHOSEN_NODE	:= chosen {						\
			bootargs = \"$(CMDLINE)\";			\
			$(INITRD_CHOSEN)				\
			$(XEN_CHOSEN)					\
		   };

CPPFLAGS	+= $(INITRD_FLAGS)
CFLAGS		+= -I$(top_srcdir)/include/ -I$(top_srcdir)/$(ARCH_SRC)/include/
CFLAGS		+= -Wall -fomit-frame-pointer
CFLAGS		+= -ffreestanding -nostdlib
CFLAGS		+= -fno-stack-protector
CFLAGS		+= -ffunction-sections -fdata-sections
CFLAGS		+= -fno-pic -fno-pie
LDFLAGS		+= --gc-sections
LDFLAGS		+= $(call test-ld-option,--no-warn-rwx-segments)

OBJ		:= $(addprefix $(ARCH_SRC),$(ARCH_OBJ)) $(addprefix $(COMMON_SRC),$(COMMON_OBJ))

# Don't lookup all prerequisites in $(top_srcdir), only the source files. When
# building outside the source tree $(ARCH_SRC) needs to be created.
VPATH		=
vpath %.c $(top_srcdir)
vpath %.S $(top_srcdir)

all: $(IMAGE)

CLEANFILES = $(IMAGE) linux-system.axf xen-system.axf $(OBJ) model.lds fdt.dtb

$(IMAGE): $(OBJ) model.lds fdt.dtb $(KERNEL_IMAGE) $(FILESYSTEM) $(XEN_IMAGE)
	$(LD) $(LDFLAGS) $(OBJ) -o $@ --script=model.lds

$(ARCH_SRC):
	$(MKDIR_P) $@

$(COMMON_SRC):
	$(MKDIR_P) $@

%.o: %.S Makefile | $(ARCH_SRC)
	$(CC) $(CPPFLAGS) -D__ASSEMBLY__ $(CFLAGS) $(DEFINES) -c -o $@ $<

%.o: %.c Makefile | $(ARCH_SRC) $(COMMON_SRC)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(DEFINES) -c -o $@ $<

model.lds: $(LD_SCRIPT) Makefile
	$(CPP) $(CPPFLAGS) -ansi -DPHYS_OFFSET=$(PHYS_OFFSET) -DMBOX_OFFSET=$(MBOX_OFFSET) -DKERNEL_OFFSET=$(KERNEL_OFFSET) -DFDT_OFFSET=$(FDT_OFFSET) -DFS_OFFSET=$(FS_OFFSET) $(XEN) -DXEN_OFFSET=$(XEN_OFFSET) -DKERNEL=$(KERNEL_IMAGE) -DFILESYSTEM=$(FILESYSTEM) -DTEXT_LIMIT=$(TEXT_LIMIT) -P -C -o $@ $<

DTC_NOWARN  = $(call test-dtc-option,-Wno-clocks_property)
DTC_NOWARN += $(call test-dtc-option,-Wno-gpios_property)

fdt.dtb: $(KERNEL_DTB) Makefile $(FILESYSTEM)
	( $(DTC) -O dts -I dtb $(KERNEL_DTB) ; echo "/ { $(CHOSEN_NODE) $(PSCI_NODE) }; $(CPU_NODES)" ) | $(DTC) -O dtb -o $@ $(DTC_NOWARN) -

# The filesystem archive might not exist if INITRD is not being used
.PHONY: all clean $(FILESYSTEM)

MAINTAINERCLEANFILES = aclocal.m4 compile config.* configure install-sh \
	Makefile.in missing
