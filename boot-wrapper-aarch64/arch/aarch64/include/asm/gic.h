/*
 * arch/aarch64/include/asm/gic.h
 *
 * Copyright (C) 2015 ARM Limited. All rights reserved.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE.txt file.
 */
#ifndef __ASM_AARCH64_GICV3_H
#define __ASM_AARCH64_GICV3_H

#include <asm/cpu.h>

static inline void gic_write_icc_sre(uint32_t val)
{
	msr(ICC_SRE_EL3, val);
}

static inline void gic_write_icc_ctlr(uint32_t val)
{
	msr(ICC_CTLR_EL3, val);
}

#endif
