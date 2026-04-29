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
#include <gic.h>
#include <platform.h>
#include <stdbool.h>

static const char *mode_string(void)
{
	switch (read_cpsr_mode()) {
	case PSR_MON:
		return "PL1";
	case PSR_HYP:
		return "PL2 (Non-secure)";
	default:
		return "<UNKNOWN MODE>";
	}
}

void announce_arch(void)
{
	print_string("Entered at ");
	print_string(mode_string());
	print_string("\r\n");
}

static void cpu_init_monitor(void)
{
	unsigned long scr = SCR_NS | SCR_HCE;
	unsigned long nsacr = NSACR_CP10 | NSACR_CP11;

	mcr(SCR, scr);

	mcr(NSACR, nsacr);
}

#ifdef PSCI
extern char psci_vectors[];

static void cpu_init_psci_arch(unsigned int cpu)
{
	if (read_cpsr_mode() != PSR_MON) {
		print_cpu_warn(cpu, "PSCI could not be initialized (not booted at PL1).\r\n");
		return;
	}

	mcr(MVBAR, (unsigned long)psci_vectors);
	isb();
}
#else
static static void cpu_init_psci_arch(unsigned int cpu) { }
#endif

void cpu_init_arch(unsigned int cpu)
{
	if (read_cpsr_mode() == PSR_MON) {
		cpu_init_monitor();
		gic_secure_init();
	}

	cpu_init_psci_arch(cpu);

	mcr(CNTFRQ, COUNTER_FREQ);
}
