/*
 * arch/aarch64/common.S - common definitions useful for boot code
 *
 * Copyright (C) 2013 ARM Limited. All rights reserved.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE.txt file.
 */

#include <cpu.h>

	/* Load a 64-bit value using immediates */
	.macro	mov_64 dest, val
	mov	\dest, #(((\val) >>  0) & 0xffff)
	movk	\dest, #(((\val) >> 16) & 0xffff), lsl #16
	movk	\dest, #(((\val) >> 32) & 0xffff), lsl #32
	movk	\dest, #(((\val) >> 48) & 0xffff), lsl #48
	.endm

	/* Put MPIDR into \dest, clobber \tmp and flags */
	.macro cpuid dest, tmp
	mrs	\dest, mpidr_el1
	mov_64	\tmp, MPIDR_ID_BITS
	ands	\dest, \dest, \tmp
	.endm
