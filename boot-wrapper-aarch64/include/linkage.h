/*
 * include/linkage.h
 *
 * Copyright (C) 2015 ARM Limited. All rights reserved.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE.txt file.
 */


#ifndef __LINKAGE_H
#define __LINKAGE_H

#ifdef __ASSEMBLY__

#define ASM_FUNC(name)				\
	.globl name;				\
	.type  name, %function;			\
	name:

#define ASM_DATA(name)				\
	.globl name;				\
	name:

#endif /* __ASSEMBLY__ */
#endif
