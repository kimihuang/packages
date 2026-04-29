/*
 * arch/aarch64/psci.S - basic PSCI implementation
 *
 * Copyright (C) 2013 ARM Limited. All rights reserved.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE.txt file.
 */
#include <linkage.h>
#include <psci.h>

#include "common.S"

	.macro	ventry	label
	.align	7
	b	\label
	.endm

	.section .vectors, "w"

	.align 11
ASM_DATA(psci_vectors)
	// current EL, SP_EL0
	ventry	err_exception	// synchronous
	ventry	err_exception	// IRQ
	ventry	err_exception	// FIQ
	ventry	err_exception	// SError

	// current EL, SP_ELx
	ventry	err_exception
	ventry	err_exception
	ventry	err_exception
	ventry	err_exception

	// lower EL, AArch64
	ventry	smc_entry64
	ventry	err_exception
	ventry	err_exception
	ventry	err_exception

	// lower EL, AArch32
	ventry	smc_entry32
	ventry	err_exception
	ventry	err_exception
	ventry	err_exception

	.text

err_exception:
	b err_exception

smc_entry32:
	/* Clear upper bits */
	mov	w0, w0
	/* Pass through */

smc_entry64:
	/* SMC entry uses 112 bytes of stack */
	stp	x18, x19, [sp, #-16]!
	stp	x20, x21, [sp, #-16]!
	stp	x22, x23, [sp, #-16]!
	stp	x24, x25, [sp, #-16]!
	stp	x26, x27, [sp, #-16]!
	stp	x28, x29, [sp, #-16]!
	// Keep sp aligned to 16 bytes
	stp	x30, xzr, [sp, #-16]!

	bl	psci_call

	b	smc_exit

smc_exit:
	ldp	x30, xzr, [sp], #16
	ldp	x28, x29, [sp], #16
	ldp	x26, x27, [sp], #16
	ldp	x24, x25, [sp], #16
	ldp	x22, x23, [sp], #16
	ldp	x20, x21, [sp], #16
	ldp	x18, x19, [sp], #16
	eret

ASM_FUNC(start_bootmethod)
	b	psci_first_spin
