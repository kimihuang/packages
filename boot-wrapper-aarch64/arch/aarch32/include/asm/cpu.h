/*
 * arch/aarch32/include/asm/cpu.h
 *
 * Copyright (C) 2015 ARM Limited. All rights reserved.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE.txt file.
 */
#ifndef __ASM_AARCH32_CPU_H
#define __ASM_AARCH32_CPU_H

#include <bits.h>

#define MPIDR_ID_BITS		0x00ffffff
#define MPIDR_INVALID		(-1)

#define ID_PFR1_GIC		BITS(31, 28)

/* Only RES1 bits and CP15 barriers for the kernel */
#define HSCTLR_KERNEL		(3 << 28 | 3 << 22 | 1 << 18 | 1 << 16 | 1 << 11 | 3 << 4)
#define SCTLR_KERNEL		(3 << 22 | 1 << 11 | 1 << 5 | 3 << 4)

#define PSR_SVC			0x13
#define PSR_HYP			0x1a
#define PSR_MON			0x16
#define PSR_MODE_MASK		0x1f

#define PSR_T			(1 << 5)
#define PSR_F			(1 << 6)
#define PSR_I			(1 << 7)
#define PSR_A			(1 << 8)

#define SCR_NS			BIT(0)
#define SCR_HCE			BIT(8)

#define NSACR_CP10		BIT(10)
#define NSACR_CP11		BIT(11)

#define SPSR_KERNEL		(PSR_A | PSR_I | PSR_F | PSR_HYP)

#ifndef __ASSEMBLY__

#include <stdint.h>

#ifdef __ARM_ARCH_8A__
#define sevl()		asm volatile ("sevl" : : : "memory")
#else
/* sevl doesn't exist on ARMv7. Send event globally */
#define sevl()		asm volatile ("sev" : : : "memory")
#endif

static inline unsigned long read_cpsr(void)
{
	unsigned long cpsr;
	asm volatile ("mrs      %0, cpsr\n" : "=r" (cpsr));
	return cpsr;
}

#define read_cpsr_mode()       (read_cpsr() & PSR_MODE_MASK)

#define MPIDR		"p15, 0, %0, c0, c0, 5"
#define ID_PFR1		"p15, 0, %0, c0, c1, 1"
#define SCR		"p15, 0, %0, c1, c1, 0"
#define NSACR		"p15, 0, %0, c1, c1, 2"
#define ICIALLU		"p15, 0, %0, c7, c5, 0"
#define MVBAR		"p15, 0, %0, c12, c0, 1"

#define ICC_SRE		"p15, 6, %0, c12, c12, 5"
#define ICC_CTLR	"p15, 6, %0, c12, c12, 4"

#define CNTFRQ		"p15, 0, %0, c14, c0, 0"

#define mrc(reg)						\
({								\
	unsigned long __mrc_val;				\
	asm volatile("mrc " reg : "=r" (__mrc_val));		\
	__mrc_val;						\
})

#define mcr(reg, val)						\
do {								\
	unsigned long __mcr_val = val;				\
	asm volatile("mcr " reg : : "r" (__mcr_val));		\
} while (0)


#define mrc_field(reg, field) \
	BITS_EXTRACT(mrc(reg), (reg##_##field))

static inline unsigned long read_mpidr(void)
{
	return mrc(MPIDR) & MPIDR_ID_BITS;
}

static inline void iciallu(void)
{
	mcr(ICIALLU, 0);
}

static inline int has_gicv3_sysreg(void)
{
	return !!mrc_field(ID_PFR1, GIC);
}

#endif /* __ASSEMBLY__ */

#endif
