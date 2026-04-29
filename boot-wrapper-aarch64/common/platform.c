/*
 * platform.c - Platform initialization and I/O.
 *
 * Copyright (C) 2015 ARM Limited. All rights reserved.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE.txt file.
 */

#include <cpu.h>
#include <stdint.h>

#include <asm/io.h>

#define PL011_UARTDR		0x00
#define PL011_UARTFR		0x18
#define PL011_UARTIBRD		0x24
#define PL011_UARTFBRD		0x28
#define PL011_UART_LCR_H	0x2c
#define PL011_UARTCR		0x30

#define PL011_UARTFR_BUSY	(1 << 3)
#define PL011_UARTFR_FIFO_FULL	(1 << 5)

#define PL011(reg)	((void *)UART_BASE + PL011_##reg)

#ifdef SYSREGS_BASE
#define V2M_SYS_CFGDATA		0xa0
#define V2M_SYS_CFGCTRL		0xa4

#define V2M_SYS(reg)	((void *)SYSREGS_BASE + V2M_SYS_##reg)
#endif

void print_char(char c)
{
	uint32_t flags;

	do {
		flags = raw_readl(PL011(UARTFR));
	} while (flags & PL011_UARTFR_FIFO_FULL);

	raw_writel(c, PL011(UARTDR));

	do {
		flags = raw_readl(PL011(UARTFR));
	} while (flags & PL011_UARTFR_BUSY);
}

void print_string(const char *str)
{
	while (*str)
		print_char(*str++);
}

#define HEX_CHARS_PER_LONG	(2 * sizeof(long))
#define HEX_CHARS		"0123456789abcdef"

void print_ulong_hex(unsigned long val)
{
	int i;

	for (i = HEX_CHARS_PER_LONG - 1; i >= 0; i--) {
		int v = (val >> (4 * i)) & 0xf;
		print_char(HEX_CHARS[v]);
	}
}

// 2^32 is 4,294,967,296
#define DEC_CHARS_PER_UINT	10

void print_uint_dec(unsigned int val)
{
	char digits[DEC_CHARS_PER_UINT];
	int d = 0;

	do {
		digits[d] = val % 10;
		val /= 10;
		d++;
	} while (val);

	while (d--) {
		print_char('0' + digits[d]);
	}
}

void print_cpu_warn(unsigned int cpu, const char *str)
{
	print_string("CPU");
	print_uint_dec(cpu);
	print_string(" WARNING: ");
	print_string(str);
}

void print_cpu_msg(unsigned int cpu, const char *str)
{
	print_string("CPU");
	print_uint_dec(cpu);
	print_string(": ");
	print_string(str);
}

void init_uart(void)
{
	/*
	 * UART initialisation (38400 8N1)
	 */
	raw_writel(0x10,	PL011(UARTIBRD));
	raw_writel(0x0,		PL011(UARTFBRD));
	/* Set parameters to 8N1 and enable the FIFOs */
	raw_writel(0x70,	PL011(UART_LCR_H));
	/* Enable the UART, TXen and RXen */
	raw_writel(0x301,	PL011(UARTCR));
}

void init_platform(void)
{
#ifdef SYSREGS_BASE
	/*
	 * CLCD output site MB
	 */
	raw_writel(0x0,		V2M_SYS(CFGDATA));
	/* START | WRITE | MUXFPGA | SITE_MB */
	raw_writel((1 << 31) | (1 << 30) | (7 << 20) | (0 << 16),
				V2M_SYS(CFGCTRL));
#endif
}
