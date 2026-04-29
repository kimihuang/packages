/*
 * arch/aarch32/boot.S - simple register setup code for stand-alone Linux booting
 *
 * Copyright (C) 2015 ARM Limited. All rights reserved.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE.txt file.
 */

#include <cpu.h>
#include <linkage.h>

#include "common.S"

	.arch_extension sec
	.arch_extension virt

	.section .init

	/*
	 * The boot-wrapper must be entered from the reset vector at the
	 * highest implemented exception level. The boot-wrapper only supports
	 * being entered in the following modes:
	 *
	 * - PL1 / EL3 (Secure) Supervisor mode
	 *   Entering in this mode is strongly recommended.
	 *   PL2 must be implemented.
	 *
	 * - PL2 / EL2 (Non-secure) Hypervisor mode
	 *   Entering in this mode is partially supported.
	 *   PSCI is not supported when entered in this mode.
	 */
ASM_FUNC(_start)
	mrs	r0, cpsr
	and	r0, #PSR_MODE_MASK
	cmp	r0, #PSR_SVC
	beq	reset_at_svc
	cmp	r0, #PSR_HYP
	beq	reset_at_hyp

	/* Booting at other modes is not supported */
	b	.

reset_at_svc:
	/*
	 * When entered in Secure SVC mode we must switch to monitor mode to
	 * configure SCR.NS. Switch to monitor mode ASAP to simplify later
	 * code.
	 */
	adr	lr, reset_at_mon
	ldr	r0, =(PSR_A | PSR_I | PSR_F | PSR_MON)
	msr	spsr, r0
	movs	pc, lr

reset_at_mon:
	// TODO initialise monitor state
	b	reset_common

reset_at_hyp:
	// TODO initialise hyp state
	b	reset_common

reset_common:
	cpuid	r0, r1
	bl	find_logical_id
	cmp	r0, #MPIDR_INVALID
	beq	err_invalid_id

	bl	setup_stack

	bl	cpu_init_bootwrapper

	b	start_bootmethod

err_invalid_id:
	b	.

	.text

	/*
	 * r0: kernel address
	 * r1-r3, sp[0]: kernel arguments
	 */
ASM_FUNC(jump_kernel)
	mov	r4, r0
	mov	r5, r1
	mov	r6, r2
	mov	r7, r3

	ldr	r0, =HSCTLR_KERNEL
	mcr	p15, 4, r0, c1, c0, 0		@ HSCTLR

	ldr	r0, =SCTLR_KERNEL
	mcr	p15, 0, r0, c1, c0, 0		@ SCTLR

	/* Reset our stack pointer */
	cpuid	r0, r1
	bl	find_logical_id
	bl	setup_stack

	mov	r0, r5
	mov	r1, r6
	mov	r2, r7
	ldr	r3, =SPSR_KERNEL

	/* Return in thumb2 mode when bit 0 of address is 1 */
	tst	r4, #1
	orrne	r3, #PSR_T

	mrs	r5, cpsr
	and	r5, #PSR_MODE_MASK
	cmp	r5, #PSR_MON
	beq	eret_at_mon
	cmp	r5, #PSR_HYP
	beq	eret_at_hyp
	b	.

eret_at_mon:
	mov	lr, r4
	msr	spsr_mon, r3
	movs	pc, lr
eret_at_hyp:
	msr	elr_hyp, r4
	msr	spsr_hyp, r3
	eret
