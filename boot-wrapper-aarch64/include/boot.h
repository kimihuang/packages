/*
 * include/boot.h
 *
 * Copyright (C) 2015 ARM Limited. All rights reserved.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE.txt file.
 */
#ifndef __BOOT_H
#define __BOOT_H

#include <compiler.h>
#include <stdbool.h>

void __noreturn spin(unsigned long *mbox, unsigned long invalid);

void __noreturn first_spin(unsigned int cpu, unsigned long *mbox,
			   unsigned long invalid_addr);

void cpu_init_arch(unsigned int cpu);

#endif
