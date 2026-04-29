/*
 * arch/aarch64/spin.S - spin-table boot protocol implementation
 *
 * Copyright (C) 2013 ARM Limited. All rights reserved.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE.txt file.
 */
#include <linkage.h>

#include "common.S"

	.text

ASM_FUNC(start_bootmethod)
	cpuid	x0, x1
	bl	find_logical_id
	adr	x1, mbox
	mov	x2, #0
	bl	first_spin
