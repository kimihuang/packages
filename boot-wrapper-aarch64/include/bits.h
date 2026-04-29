/*
 * include/bits.h - helpers for bit-field definitions.
 *
 * Copyright (C) 2021 ARM Limited. All rights reserved.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE.txt file.
 */
#ifndef __BITS_H
#define __BITS_H

#ifdef __ASSEMBLY__
#define UL(x)	x
#define ULL(x)	x
#else
#define UL(x)	x##UL
#define ULL(x)	x##ULL
#endif

/*
 * Define a contiguous mask of bits with `msb` as the most significant bit and
 * `lsb` as the least significant bit. The `msb` value must be greater than or
 * equal to `lsb`.
 *
 * For example:
 * - BITS(63, 63) is 0x8000000000000000
 * - BITS(63, 0)  is 0xFFFFFFFFFFFFFFFF
 * - BITS(0, 0)   is 0x0000000000000001
 * - BITS(49, 17) is 0x0003FFFFFFFE0000
 */
#define BITS(msb, lsb) \
	((~ULL(0) >> (63 - msb)) & (~ULL(0) << lsb))

/*
 * Define a mask of a single set bit `b`.
 *
 * For example:
 * - BIT(63) is 0x8000000000000000
 * - BIT(0)  is 0x0000000000000001
 * - BIT(32) is 0x0000000100000000
 */
#define BIT(b)	BITS(b, b)

/*
 * Find the least significant set bit in the contiguous set of bits in `mask`.
 *
 * For example:
 * - BITS_LSB(0x0000000000000001) is 0
 * - BITS_LSB(0x000000000000ff00) is 8
 * - BITS_LSB(0x8000000000000000) is 63
 */
#define BITS_LSB(mask)	(__builtin_ffsll(mask) - 1)

/*
 * Extract a bit-field out of `val` described by the contiguous set of bits in
 * `mask`.
 *
 * For example:
 * - BITS_EXTRACT(0xABCD, BITS(15, 12)) is 0xA
 * - BITS_EXTRACT(0xABCD, BITS(11, 8))  is 0xB
 * - BITS_EXTRACT(0xABCD, BIT(7))       is 0x1
 */
#define BITS_EXTRACT(val, mask) \
	(((val) & (mask)) >> BITS_LSB(mask))

#endif
