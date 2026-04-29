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

void announce_arch(void)
{
	unsigned char el = mrs(CurrentEl) >> 2;

	print_string("Entered at EL");
	print_char('0' + el);
	print_string("\r\n");
}

static inline bool kernel_is_32bit(void)
{
#ifdef KERNEL_32
	return true;
#else
	return false;
#endif
}

static inline bool cpu_has_pauth(void)
{
	const unsigned long isar1_pauth = ID_AA64ISAR1_EL1_APA |
					  ID_AA64ISAR1_EL1_API |
					  ID_AA64ISAR1_EL1_GPA |
					  ID_AA64ISAR1_EL1_GPI;

	const unsigned long isar2_pauth = ID_AA64ISAR2_EL1_APA3 |
					  ID_AA64ISAR2_EL1_GPA3;

	return (mrs(ID_AA64ISAR1_EL1) & isar1_pauth) ||
	       (mrs(ID_AA64ISAR2_EL1) & isar2_pauth);
}

static inline bool cpu_has_permission_indirection(void)
{
	const unsigned long mask = ID_AA64MMFR3_EL1_S1PIE |
				   ID_AA64MMFR3_EL1_S2PIE |
				   ID_AA64MMFR3_EL1_S1POE |
				   ID_AA64MMFR3_EL1_S2POE;

	return mrs(ID_AA64MMFR3_EL1) & mask;
}

static bool cpu_has_scxt(void)
{
	unsigned long csv2 = mrs_field(ID_AA64PFR0_EL1, CSV2);

	if (csv2 >= 2)
		return true;
	if (csv2 < 1)
		return false;

	return mrs_field(ID_AA64PFR1_EL1, CSV2_frac) >= 2;
}

static inline bool bootwrapper_is_r_class(void)
{
#ifdef BOOTWRAPPER_64R
	return true;
#else
	return false;
#endif
}

static void cpu_init_el3(void)
{
	unsigned long scr = SCR_EL3_RES1 | SCR_EL3_NS | SCR_EL3_HCE;
	unsigned long mdcr = 0;
	unsigned long cptr = 0;
	unsigned long smcr = 0;

	if (cpu_has_pauth())
		scr |= SCR_EL3_APK | SCR_EL3_API;

	if (mrs_field(ID_AA64ISAR0_EL1, TME))
		scr |= SCR_EL3_TME;

	if (mrs_field(ID_AA64MMFR0_EL1, FGT))
		scr |= SCR_EL3_FGTEN;

	if (mrs_field(ID_AA64MMFR0_EL1, FGT) >= 2) {
		scr |= SCR_EL3_FGTEN2;
		msr(HDFGRTR2_EL2, 0);
		msr(HDFGWTR2_EL2, 0);
		msr(HFGITR2_EL2, 0);
		msr(HFGRTR2_EL2, 0);
		msr(HFGWTR2_EL2, 0);
	}

	if (mrs_field(ID_AA64MMFR0_EL1, ECV) >= 2)
		scr |= SCR_EL3_ECVEN;

	if (mrs_field(ID_AA64MMFR1_EL1, HCX))
		scr |= SCR_EL3_HXEn;

	if (mrs_field(ID_AA64MMFR3_EL1, TCRX)) {
		scr |= SCR_EL3_TCR2EN;
		msr(TCR2_EL2, 0);
		msr(TCR2_EL1, 0);
	}

	if (cpu_has_permission_indirection())
		scr |= SCR_EL3_PIEN;

	if (mrs_field(ID_AA64PFR1_EL1, MTE) >= 2)
		scr |= SCR_EL3_ATA;

	if (!kernel_is_32bit())
		scr |= SCR_EL3_RW;

	if (mrs_field(ID_AA64MMFR3_EL1, SCTLRX)) {
		scr |= SCR_EL3_SCTLR2En;
		msr(SCTLR2_EL2, 0);
		msr(SCTLR2_EL1, 0);
	}

	if (mrs_field(ID_AA64MMFR3_EL1, D128))
		scr |= SCR_EL3_D128En;

	if (mrs_field(ID_AA64PFR1_EL1, THE))
		scr |= SCR_EL3_RCWMASKEn;

	if (mrs_field(ID_AA64PFR2_EL1, FPMR))
		scr |= SCR_EL3_EnFPM;

	msr(SCR_EL3, scr);

	msr(CPTR_EL3, cptr);

	if (mrs_field(ID_AA64DFR0_EL1, PMSVER))
		mdcr |= MDCR_EL3_NSPB_NS_NOTRAP;

	if (mrs_field(ID_AA64DFR0_EL1, PMSVER) >= 3)
		mdcr |= MDCR_EL3_ENPMSN;

	if (mrs_field(ID_AA64DFR0_EL1, PMSVER) >= 1 &&
	    mrs_field(PMSIDR_EL1, FDS))
		mdcr |= MDCR_EL3_ENPMS3;

	if (mrs_field(ID_AA64DFR0_EL1, TRACEBUFFER))
		mdcr |= MDCR_EL3_NSTB_NS_NOTRAP;

	if (mrs_field(ID_AA64DFR0_EL1, BRBE))
		mdcr |= MDCR_EL3_SBRBE_NOTRAP_NOPROHIBIT;

	if (mrs_field(ID_AA64DFR0_EL1, DEBUGVER) >= 11)
		mdcr |= MDCR_EL3_EBWE;

	if (mrs_field(ID_AA64DFR0_EL1, PMUVER) >= 0b1001)
		mdcr |= MDCR_EL3_EnPM2;

	msr(MDCR_EL3, mdcr);

	if (mrs_field(ID_AA64PFR0_EL1, SVE)) {
		cptr |= CPTR_EL3_EZ;
		msr(CPTR_EL3, cptr);
		isb();
		/*
		 * Write the maximum possible vector length, hardware
		 * will constrain to the actual limit.
		 */
		msr(ZCR_EL3, ZCR_EL3_LEN_MAX);
	}

	if (mrs_field(ID_AA64PFR1_EL1, SME)) {
		cptr |= CPTR_EL3_ESM;
		msr(CPTR_EL3, cptr);
		isb();

		scr |= SCR_EL3_EnTP2;
		msr(SCR_EL3, scr);
		isb();

		/*
		 * Write the maximum possible vector length, hardware
		 * will constrain to the actual limit.
		 */
		smcr = SMCR_EL3_LEN_MAX;

		if (mrs_field(ID_AA64SMFR0_EL1, FA64))
			smcr |= SMCR_EL3_FA64;

		if (mrs_field(ID_AA64PFR1_EL1, SME) >= 2)
			smcr |= SMCR_EL3_EZT0;

		msr(SMCR_EL3, smcr);
	}
}

void cpu_init_el2_armv8r(void)
{
	unsigned long hcr = mrs(hcr_el2);

	/* On Armv8-R ID_AA64MMFR0_EL1[51:48] == 0xF */
	if (mrs_field(ID_AA64MMFR0_EL1, MSA) != 0xF) {
		print_string("ID_AA64MMFR0_EL1.MSA != 0xF, not R-class\r\n");
		while (1)
			wfe();
	}

	if (mrs_field(ID_AA64MMFR0_EL1, MSA_frac) < 2) {
		print_string("ID_AA64MMFR0_EL1.MSA_frac < 2, EL1&0 VMSA not supported\r\n");
		while (1)
			wfe();
	}

	msr(vpidr_el2, mrs(midr_el1));
	msr(vmpidr_el2, mrs(mpidr_el1));

	msr(VSCTLR_EL2, 0);
	msr(VSTCR_EL2, VSTCR_EL2_RESET);
	msr(vtcr_el2, VTCR_EL2_MSA);

	msr(cntvoff_el2, 0);
	msr(cptr_el2, CPTR_EL2_RESET);
	msr(mdcr_el2, 0);

	if (cpu_has_scxt())
		hcr |= HCR_EL2_EnSCXT;

	if (mrs_field(ID_AA64PFR0_EL1, RAS) >= 2)
		hcr |= HCR_EL2_FIEN;

	if (cpu_has_pauth())
		hcr |= HCR_EL2_APK | HCR_EL2_API;

	msr(hcr_el2, hcr);
	isb();
}

#ifdef PSCI
extern char psci_vectors[];

static void cpu_init_psci_arch(unsigned int cpu)
{
	if (!bootwrapper_is_r_class() && mrs(CurrentEL) == CURRENTEL_EL3) {
		msr(VBAR_EL3, (unsigned long)psci_vectors);
		isb();
		return;
	}

	if (bootwrapper_is_r_class() && mrs(CurrentEL) == CURRENTEL_EL2) {
		msr(VBAR_EL2, (unsigned long)psci_vectors);
		isb();
		return;
	}

	print_cpu_warn(cpu, "PSCI could not be initialized (not booted at EL3).\r\n");

}
#else
static void cpu_init_psci_arch(unsigned int cpu) { }
#endif

void cpu_init_arch(unsigned int cpu)
{
	if (!bootwrapper_is_r_class() && mrs(CurrentEL) == CURRENTEL_EL3) {
		cpu_init_el3();
		gic_secure_init();
	}

	if (bootwrapper_is_r_class() && mrs(CurrentEL) == CURRENTEL_EL2) {
		cpu_init_el2_armv8r();
	}

	cpu_init_psci_arch(cpu);

	msr(CNTFRQ_EL0, COUNTER_FREQ);
}
