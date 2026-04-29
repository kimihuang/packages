/*
 * model.lds.S - simple linker script for stand-alone Linux booting
 *
 * Copyright (C) 2012 ARM Limited. All rights reserved.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE.txt file.
 */

#define _STR(s) #s
#define STR(s) _STR(s)

#ifdef BOOTWRAPPER_32
OUTPUT_FORMAT("elf32-littlearm")
OUTPUT_ARCH(arm)
#else
OUTPUT_FORMAT("elf64-littleaarch64")
OUTPUT_ARCH(aarch64)
#endif
TARGET(binary)

#ifdef XEN
INPUT(STR(XEN))
#endif
INPUT(STR(KERNEL))
INPUT(./fdt.dtb)

#ifdef USE_INITRD
INPUT(STR(FILESYSTEM))
#endif

ENTRY(_start)

SECTIONS
{
	/*
	 * Order matters: consume binary blobs first, so they won't appear in
	 * the boot section's *(.data)
	 */
	.kernel (PHYS_OFFSET + KERNEL_OFFSET): {
		kernel__start = .;
		STR(KERNEL)
		kernel__end = .;
	}

#ifdef XEN
	.xen (PHYS_OFFSET + XEN_OFFSET): {
		xen__start = .;
		STR(XEN)
		xen__end = .;
	}

	entrypoint = xen__start;
#else
	entrypoint = kernel__start;
#endif

	.dtb (PHYS_OFFSET + FDT_OFFSET): {
		dtb__start = .;
		dtb = .;
		./fdt.dtb
		dtb__end = .;
	}

#ifdef USE_INITRD
	.filesystem (PHYS_OFFSET + FS_OFFSET): {
		filesystem__start = .;
		STR(FILESYSTEM)
		filesystem__end = .;
	}
#endif

	.boot PHYS_OFFSET: {
		text__start = .;
		*(.init)
		*(.text*)
		*(.data* .rodata* .bss* COMMON)
		*(.vectors)
		*(.stack)
		PROVIDE(etext = .);
		text__end = .;
	}

	.mbox (PHYS_OFFSET + MBOX_OFFSET): {
		mbox__start = .;
		mbox = .;
		QUAD(0x0)
		mbox__end = .;
	}

	ASSERT(etext <= (PHYS_OFFSET + TEXT_LIMIT), ".text overflow!")
}
