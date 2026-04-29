/*
 * include/platform.h - Platform initialization and I/O.
 *
 * Copyright (C) 2021 ARM Limited. All rights reserved.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE.txt file.
 */
#ifndef __PLATFORM_H
#define __PLATFORM_H

void print_char(char c);
void print_string(const char *str);
void print_ulong_hex(unsigned long val);
void print_uint_dec(unsigned int val);

void print_cpu_warn(unsigned int cpu, const char *str);
void print_cpu_msg(unsigned int cpu, const char *str);

void init_uart(void);

void init_platform(void);

#endif /* __PLATFORM_H */
