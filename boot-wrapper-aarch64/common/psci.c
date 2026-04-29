/*
 * psci.c - basic PSCI implementation
 *
 * Copyright (C) 2015 ARM Limited. All rights reserved.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE.txt file.
 */

#include <stdint.h>

#include <bakery_lock.h>
#include <boot.h>
#include <cpu.h>
#include <platform.h>
#include <psci.h>

#ifndef CPU_IDS
#error "No MPIDRs provided"
#endif

static unsigned long branch_table[NR_CPUS];

bakery_ticket_t branch_table_lock[NR_CPUS];

static int psci_store_address(unsigned int cpu, unsigned long address)
{
	if (branch_table[cpu] != PSCI_ADDR_INVALID)
		return PSCI_RET_ALREADY_ON;

	branch_table[cpu] = address;
	return PSCI_RET_SUCCESS;
}

static int psci_cpu_on(unsigned long target_mpidr, unsigned long address)
{
	int ret;
	unsigned int cpu = find_logical_id(target_mpidr);
	unsigned int this_cpu = this_cpu_logical_id();

	if (cpu == MPIDR_INVALID)
		return PSCI_RET_INVALID_PARAMETERS;

	bakery_lock(branch_table_lock, this_cpu);
	ret = psci_store_address(cpu, address);
	bakery_unlock(branch_table_lock, this_cpu);

	return ret;
}

static int psci_cpu_off(void)
{
	unsigned int cpu = this_cpu_logical_id();

	if (cpu == MPIDR_INVALID)
		return PSCI_RET_DENIED;

	branch_table[cpu] = PSCI_ADDR_INVALID;

	spin(branch_table + cpu, PSCI_ADDR_INVALID);

	unreachable();
}

long psci_call(unsigned long fid, unsigned long arg1, unsigned long arg2)
{
	switch (fid) {
	case PSCI_CPU_OFF:
		return psci_cpu_off();
#ifdef KERNEL_32
	case PSCI_CPU_ON_32:
		return psci_cpu_on(arg1, arg2);
#else
	case PSCI_CPU_ON_64:
		return psci_cpu_on(arg1, arg2);
#endif
	default:
		return PSCI_RET_NOT_SUPPORTED;
	}
}

void __noreturn psci_first_spin(void)
{
	unsigned int cpu = this_cpu_logical_id();

	first_spin(cpu, branch_table + cpu, PSCI_ADDR_INVALID);

	unreachable();
}
