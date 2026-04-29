/*
 * init.c - common boot-wrapper initialization
 *
 * Copyright (C) 2021 ARM Limited. All rights reserved.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE.txt file.
 */
#include <boot.h>
#include <cpu.h>
#include <platform.h>

static void announce_bootwrapper(void)
{
	print_string("Boot-wrapper v0.2\r\n");
}

#define announce_object(object, desc)				\
do {								\
	extern char object##__start[];				\
	extern char object##__end[];				\
	print_string("[");					\
	print_ulong_hex((unsigned long)object##__start);	\
	print_string("..");					\
	print_ulong_hex((unsigned long)object##__end);		\
	print_string("] => " desc "\r\n");			\
} while (0)

static void announce_objects(void)
{
	print_string("Memory layout:\r\n");
	announce_object(text, "boot-wrapper");
	announce_object(mbox, "mbox");
	announce_object(kernel, "kernel");
#ifdef XEN
	announce_object(xen, "xen");
#endif
	announce_object(dtb, "dtb");
#ifdef USE_INITRD
	announce_object(filesystem, "initrd");
#endif
}

void announce_arch(void);

static void init_bootwrapper(void)
{
	init_uart();
	announce_bootwrapper();
	announce_arch();
	announce_objects();
	init_platform();
}

static void cpu_init_self(unsigned int cpu)
{
	print_string("CPU");
	print_uint_dec(cpu);
	print_string(": (MPIDR ");
	print_ulong_hex(read_mpidr());
	print_string(") initializing...\r\n");

	cpu_init_arch(cpu);
}

void cpu_init_bootwrapper(void)
{
	static volatile unsigned int cpu_next = 0;
	unsigned int cpu = this_cpu_logical_id();

	if (cpu == 0)
		init_bootwrapper();

	while (cpu_next != cpu)
		wfe();

	cpu_init_self(cpu);

	cpu_next = cpu + 1;
	dsb(sy);
	sev();

	if (cpu != 0)
		return;

	while (cpu_next != NR_CPUS)
		wfe();

	print_string("All CPUs initialized. Entering kernel...\r\n\r\n");
}
