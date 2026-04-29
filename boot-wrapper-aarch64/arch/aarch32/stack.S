/*
 * arch/aarch32/stack.S - stack handling
 *
 * Copyright (C) 2015 ARM Limited. All rights reserved.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE.txt file.
 */
#include <linkage.h>

	.text
	/*
	 * Setup initial stack pointer
	 * r0: logical CPU ID
	 * Clobbers r1 and r2
	 */
ASM_FUNC(setup_stack)
	mov	r1, #STACK_SIZE
	ldr	r2, =stack_top
	mls	sp, r0, r1, r2
	bx	lr

	.section .stack
	.align 2
ASM_DATA(stack_bottom)
	.irp cpu, CPU_IDS
	.space STACK_SIZE
	.endr
ASM_DATA(stack_top)
