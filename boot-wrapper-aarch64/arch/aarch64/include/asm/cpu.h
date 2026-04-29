/*
 * arch/aarch64/include/asm/cpu.h
 *
 * Copyright (C) 2015 ARM Limited. All rights reserved.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE.txt file.
 */
#ifndef __ASM_AARCH64_CPU_H
#define __ASM_AARCH64_CPU_H

#include <bits.h>

#define MPIDR_ID_BITS		0xff00ffffff

#define CURRENTEL_EL3		(3 << 2)
#define CURRENTEL_EL2		(2 << 2)
#define CURRENTEL_EL1		(1 << 2)

#define TCR2_EL2		s3_4_c2_c0_3
#define TCR2_EL1		s3_0_c2_c0_3

#define SCTLR2_EL1		s3_0_c1_c0_3
#define SCTLR2_EL2		s3_4_c1_c0_3

#define HDFGRTR2_EL2		s3_4_c3_c1_0
#define HDFGWTR2_EL2		s3_4_c3_c1_1
#define HFGRTR2_EL2		s3_4_c3_c1_2
#define HFGWTR2_EL2		s3_4_c3_c1_3
#define HFGITR2_EL2		s3_4_c3_c1_7

/*
 * RES1 bit definitions definitions as of ARM DDI 0487G.b
 *
 * These includes bits which are RES1 in some configurations.
 */
#define SCTLR_EL3_RES1							\
	(BIT(29) | BIT(28) | BIT(23) | BIT(22) | BIT(18) | BIT(16) |	\
	 BIT(11) | BIT(5) | BIT(4))

#define CPTR_EL2_NO_E2H_RES1					\
	(BITS(13,12) | BIT(9) | BITS(7,0))

#define SCTLR_EL2_RES1							\
	(BIT(29) | BIT(28) | BIT(23) | BIT(22) | BIT(18) | BIT(16) |	\
	 BIT(11) | BIT(5) | BIT(4))

#define VSTCR_EL2_RES1							(BIT(31))

#define SCTLR_EL1_RES1							\
	(BIT(29) | BIT(28) | BIT(23) | BIT(22) | BIT(20) | BIT(11) |	\
	 BIT(8) | BIT(7) | BIT(4))

#define MDCR_EL3_NSPB_NS_NOTRAP			(UL(3) << 12)
#define MDCR_EL3_NSTB_NS_NOTRAP			(UL(3) << 24)
#define MDCR_EL3_SBRBE_NOTRAP_NOPROHIBIT	(UL(3) << 32)
#define MDCR_EL3_ENPMSN				BIT(36)
#define MDCR_EL3_ENPMS3				BIT(42)
#define MDCR_EL3_EBWE				BIT(43)
#define MDCR_EL3_EnPM2				BIT(7)

#define SCR_EL3_RES1			BITS(5, 4)
#define SCR_EL3_NS			BIT(0)
#define SCR_EL3_HCE			BIT(8)
#define SCR_EL3_RW			BIT(10)
#define SCR_EL3_APK			BIT(16)
#define SCR_EL3_API			BIT(17)
#define SCR_EL3_ATA			BIT(26)
#define SCR_EL3_FGTEN			BIT(27)
#define SCR_EL3_ECVEN			BIT(28)
#define SCR_EL3_TME			BIT(34)
#define SCR_EL3_HXEn			BIT(38)
#define SCR_EL3_EnTP2			BIT(41)
#define SCR_EL3_RCWMASKEn		BIT(42)
#define SCR_EL3_TCR2EN			BIT(43)
#define SCR_EL3_SCTLR2En		BIT(44)
#define SCR_EL3_PIEN			BIT(45)
#define SCR_EL3_D128En			BIT(47)
#define SCR_EL3_EnFPM			BIT(50)
#define SCR_EL3_FGTEN2			BIT(59)

#define VTCR_EL2_MSA			BIT(31)

#define HCR_EL2_RES1			BIT(1)
#define HCR_EL2_APK			BIT(40)
#define HCR_EL2_API			BIT(41)
#define HCR_EL2_FIEN			BIT(47)
#define HCR_EL2_EnSCXT			BIT(53)

#define ID_AA64DFR0_EL1_PMSVER		BITS(35, 32)
#define ID_AA64DFR0_EL1_TRACEBUFFER	BITS(47, 44)
#define ID_AA64DFR0_EL1_BRBE		BITS(55, 52)
#define ID_AA64DFR0_EL1_PMUVER		BITS(11, 8)
#define ID_AA64DFR0_EL1_DEBUGVER	BITS(3, 0)

#define ID_AA64ISAR0_EL1_TME		BITS(27, 24)

#define ID_AA64ISAR1_EL1_APA		BITS(7, 4)
#define ID_AA64ISAR1_EL1_API		BITS(11, 8)
#define ID_AA64ISAR1_EL1_GPA		BITS(27, 24)
#define ID_AA64ISAR1_EL1_GPI		BITS(31, 28)

#define ID_AA64ISAR2_EL1_GPA3		BITS(11, 8)
#define ID_AA64ISAR2_EL1_APA3		BITS(15, 12)

#define ID_AA64MMFR0_EL1_MSA		BITS(51, 48)
#define ID_AA64MMFR0_EL1_MSA_frac	BITS(55, 52)
#define ID_AA64MMFR0_EL1_FGT		BITS(59, 56)
#define ID_AA64MMFR0_EL1_ECV		BITS(63, 60)

#define ID_AA64MMFR1_EL1_HCX		BITS(43, 40)

#define ID_AA64MMFR3_EL1_TCRX		BITS(3, 0)
#define ID_AA64MMFR3_EL1_SCTLRX		BITS(7, 4)
#define ID_AA64MMFR3_EL1_S1PIE		BITS(11, 8)
#define ID_AA64MMFR3_EL1_S2PIE		BITS(15, 12)
#define ID_AA64MMFR3_EL1_S1POE		BITS(19, 16)
#define ID_AA64MMFR3_EL1_S2POE		BITS(23, 20)
#define ID_AA64MMFR3_EL1_D128		BITS(35, 32)

#define ID_AA64PFR0_EL1_RAS		BITS(31, 28)
#define ID_AA64PFR0_EL1_SVE		BITS(35, 32)
#define ID_AA64PFR0_EL1_CSV2		BITS(59, 56)

#define ID_AA64PFR1_EL1_MTE		BITS(11, 8)
#define ID_AA64PFR1_EL1_SME		BITS(27, 24)
#define ID_AA64PFR1_EL1_CSV2_frac	BITS(35, 32)
#define ID_AA64PFR1_EL1_THE		BITS(51, 48)

#define ID_AA64PFR2_EL1			s3_0_c0_c4_2
#define ID_AA64PFR2_EL1_FPMR		BITS(35, 32)

#define ID_AA64SMFR0_EL1		s3_0_c0_c4_5
#define ID_AA64SMFR0_EL1_FA64		BIT(63)

/*
 * Initial register values required for the boot-wrapper to run out-of-reset.
 */
#define SCTLR_EL3_RESET		SCTLR_EL3_RES1
#define CPTR_EL2_RESET		CPTR_EL2_NO_E2H_RES1
#define SCTLR_EL2_RESET		SCTLR_EL2_RES1
#define VSTCR_EL2_RESET		VSTCR_EL2_RES1
#define SCTLR_EL1_RESET		SCTLR_EL1_RES1
#define HCR_EL2_RESET		HCR_EL2_RES1

#define ID_AA64PFR0_EL1_GIC	BITS(27, 24)

/*
 * RES1 bits,  little-endian, caches and MMU off, no alignment checking,
 * no WXN.
 */
#define SCTLR_EL2_KERNEL	(3 << 28 | 3 << 22 | 1 << 18 | 1 << 16 | 1 << 11 | 3 << 4)

#define SPSR_A			(1 << 8)	/* System Error masked */
#define SPSR_D			(1 << 9)	/* Debug masked */
#define SPSR_I			(1 << 7)	/* IRQ masked */
#define SPSR_F			(1 << 6)	/* FIQ masked */
#define SPSR_T			(1 << 5)	/* Thumb */
#define SPSR_EL1H		(5 << 0)	/* EL1 Handler mode */
#define SPSR_EL2H		(9 << 0)	/* EL2 Handler mode */
#define SPSR_HYP		(0x1a << 0)	/* M[3:0] = hyp, M[4] = AArch32 */

#define CPTR_EL3_ESM		(1 << 12)
#define CPTR_EL3_EZ		(1 << 8)

#define ICC_SRE_EL2		S3_4_C12_C9_5
#define ICC_SRE_EL3		S3_6_C12_C12_5
#define ICC_CTLR_EL1		S3_0_C12_C12_4
#define ICC_CTLR_EL3		S3_6_C12_C12_4
#define ICC_PMR_EL1		S3_0_C4_C6_0

#define VSTCR_EL2		s3_4_c2_c6_2
#define VSCTLR_EL2		s3_4_c2_c0_0

#define ZCR_EL3			s3_6_c1_c2_0
#define ZCR_EL3_LEN_MAX		0xf

#define SMCR_EL3		s3_6_c1_c2_6
#define SMCR_EL3_EZT0		BIT(30)
#define SMCR_EL3_FA64		BIT(31)
#define SMCR_EL3_LEN_MAX	0xf

#define ID_AA64ISAR2_EL1	s3_0_c0_c6_2

#define ID_AA64MMFR3_EL1	s3_0_c0_c7_3

#define SCTLR_EL1_CP15BEN	(1 << 5)

#define PMSIDR_EL1		s3_0_c9_c9_7
#define PMSIDR_EL1_FDS		BIT(7)

#ifdef KERNEL_32
/*
 * When booting a 32-bit kernel, EL1 uses AArch32 and registers which are
 * architecturally mapped must be configured with the AArch32 layout.
 *
 * We copy the AArch32 definition of SCTLR_KERNEL here.
 *
 * TODO: restructure the headers to share a single definition.
 */
#define SCTLR_EL1_KERNEL	(3 << 22 | 1 << 11 | 1 << 5 | 3 << 4)
#define SPSR_KERNEL		(SPSR_A | SPSR_I | SPSR_F | SPSR_HYP)
#else
#define SCTLR_EL1_KERNEL	SCTLR_EL1_RES1
#define SPSR_KERNEL		(SPSR_A | SPSR_D | SPSR_I | SPSR_F | SPSR_EL2H)
#define SPSR_KERNEL_EL1		(SPSR_A | SPSR_D | SPSR_I | SPSR_F | SPSR_EL1H)
#endif

#ifndef __ASSEMBLY__

#include <stdint.h>

#define sevl()		asm volatile ("sevl\n" : : : "memory")

#define __str(def)	#def

#define mrs(reg)							\
({									\
	unsigned long __mrs_val;					\
	asm volatile("mrs %0, " __str(reg) : "=r" (__mrs_val));		\
	__mrs_val;							\
})

#define msr(reg, val)							\
do {									\
	unsigned long __msr_val = val;					\
	asm volatile("msr " __str(reg) ", %0" : : "r" (__msr_val));	\
} while (0)

#define mrs_field(reg, field) \
	BITS_EXTRACT(mrs(reg), (reg##_##field))

static inline unsigned long read_mpidr(void)
{
	return mrs(mpidr_el1) & MPIDR_ID_BITS;
}

static inline void iciallu(void)
{
	asm volatile ("ic	iallu");
}

static inline int has_gicv3_sysreg(void)
{
	return !!mrs_field(ID_AA64PFR0_EL1, GIC);
}

#endif /* !__ASSEMBLY__ */

#endif
