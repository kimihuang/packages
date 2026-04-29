# SPDX-License-Identifier: GPL-2.0
# Top-level Makefile for demo_dmabuf kernel modules + user-space programs

# Cross-compilation settings
ARCH       ?= arm64
CROSS      ?= aarch64-linux-gnu-
KDIR       ?= /lib/modules/$(shell uname -r)/build

# ----------------------------------------------------------------------
# Kernel modules  (.c files live in subdirectories)
# ----------------------------------------------------------------------
obj-m += demo_heap.o
obj-m += demo_exporter.o
obj-m += demo_importer.o

demo_heap-y       := demo_heap/demo_heap.o
demo_exporter-y   := demo_exporter/demo_exporter.o
demo_importer-y   := demo_importer/demo_importer.o

# Extra include paths so subdirectory headers are visible during kbuild
ccflags-y += -I$(src)/demo_heap
ccflags-y += -I$(src)/demo_exporter
ccflags-y += -I$(src)/demo_importer

# ----------------------------------------------------------------------
# User-space toolchain (cross or native)
# ----------------------------------------------------------------------
ifneq ($(CROSS),)
CC_USER   := $(CROSS)gcc
else
CC_USER   := gcc
endif

CFLAGS_USER := -Wall -Wextra -O2 -g

# ----------------------------------------------------------------------
# Targets
# ----------------------------------------------------------------------
.PHONY: all modules user rootfs clean

all: modules user

# --- Kernel modules ---------------------------------------------------
modules:
	$(MAKE) -C $(KDIR) M=$(CURDIR) ARCH=$(ARCH) CROSS_COMPILE=$(CROSS) modules

# --- User-space programs ----------------------------------------------
USER_TARGETS := demo_app/demo_app demo_test/test_dmabuf demo_app/sync_file_info

user: $(USER_TARGETS)

demo_app/demo_app: demo_app/demo_app.c
	$(CC_USER) $(CFLAGS_USER) -o $@ $<

demo_test/test_dmabuf: demo_test/test_dmabuf.c
	$(CC_USER) $(CFLAGS_USER) -o $@ $<

demo_app/sync_file_info: demo_app/sync_file_info.c
	$(CC_USER) $(CFLAGS_USER) -o $@ $<

# --- Rootfs cpio archive ---------------------------------------------
ROOTFS_DIR  := rootfs staging
ROOTFS_CPIO := rootfs.cpio

rootfs: modules user
	@./scripts/create_rootfs.sh

# --- Clean ------------------------------------------------------------
clean:
	$(MAKE) -C $(KDIR) M=$(CURDIR) ARCH=$(ARCH) CROSS_COMPILE=$(CROSS) clean
	rm -f $(USER_TARGETS)
	rm -rf $(ROOTFS_DIR) $(ROOTFS_CPIO)
