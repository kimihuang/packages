/*
 * arch/aarch64/boot.S - simple register setup code for stand-alone Linux booting
 *
 * Copyright (C) 2012 ARM Limited. All rights reserved.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE.txt file.
 */
#include <linkage.h>

#include "common.S"

	.section .init

	/*
	 * The boot-wrapper must be entered from the reset vector at the
	 * highest implemented exception level. The boot-wrapper only supports
	 * being entered at the following exception levels:
	 *
	 * - EL3 (Secure)
	 *   Entering at EL3 is strongly recommended.
	 *   EL2 must be implemented.
	 *
	 * - EL2 (Non-secure)
	 *   Entering at EL2 is partially supported.
	 *   PSCI is not supported when entered in this exception level.
	 */
ASM_FUNC(_start)
	mrs	x0, CurrentEL
	cmp	x0, #CURRENTEL_EL3
	b.eq	reset_at_el3
	cmp	x0, #CURRENTEL_EL2
	b.eq	reset_at_el2

	/* Booting at EL1 or EL0 is not supported */
	b	.

	/*
	 * EL3 initialisation
	 */
reset_at_el3:
	mov_64	x0, SCTLR_EL3_RESET
	msr	sctlr_el3, x0
	isb

	b	reset_common

	/*
	 * EL2 initialization
	 */
reset_at_el2:
	// Ensure E2H is not in use
	mov_64	x0, HCR_EL2_RESET
	msr	hcr_el2, x0
	isb

	mov_64	x0, SCTLR_EL2_RESET
	msr	sctlr_el2, x0
	isb

	b	reset_common

reset_common:
	cpuid	x0, x1
	bl	find_logical_id
	cmp	x0, #MPIDR_INVALID
	b.eq	err_invalid_id
	bl	setup_stack

	bl	cpu_init_bootwrapper

	b	start_bootmethod

err_invalid_id:
	b	.

	/*
	 * Drop to the kernel
	 * x0:		entry address
	 * x1-x4:	arguments
	 */
ASM_FUNC(jump_kernel)
	mov	x19, x0
	mov	x20, x1
	mov	x21, x2
	mov	x22, x3
	mov	x23, x4

	ldr	x0, =SCTLR_EL1_KERNEL
	msr	sctlr_el1, x0

	ldr	x0, =SCTLR_EL2_KERNEL
	msr	sctlr_el2, x0

	cpuid	x0, x1
	bl	find_logical_id
	bl	setup_stack		// Reset stack pointer

	mov	x0, x20
	mov	x1, x21
	mov	x2, x22
	mov	x3, x23
#if defined(BOOTWRAPPER_64R) && !defined(XEN)
	// On Armv8-R Linux needs to be booted at EL1
	mov	x4, #SPSR_KERNEL_EL1
#else
	mov	x4, #SPSR_KERNEL
#endif

	/*
	 * If bit 0 of the kernel address is set, we're entering in AArch32
	 * thumb mode. Set SPSR.T accordingly.
	 */
	bfi	x4, x19, #5, #1

	mrs	x5, CurrentEL
	cmp	x5, #CURRENTEL_EL3
	b.eq	eret_at_el3
	cmp	x5, #CURRENTEL_EL2
	b.eq	eret_at_el2
	b	.			// Not possible

eret_at_el3:
	msr	elr_el3, x19
	msr	spsr_el3, x4
	eret
eret_at_el2:
	msr	elr_el2, x19
	msr	spsr_el2, x4
	eret
