/* SPDX-License-Identifier: BSD-2-Clause */

#include <stdarg.h>
#include <string.h>

#include "ts_zpu.h"
#include "zpu_fifo.h"
#include "zpu_strings.h"


/* ZPU Demo application.
 *
 * This simple application does a number of things:
 * - Initialize the FIFO to the CPU, allowing tszpuctl to connect to this
 *     application as it is running in the ZPU.
 * - Echo all characters received, and toggle the red and green LEDs with every
 *     character as they are received from the FIFO.
 * - If a newline is received, the application will then print the time since
 *     the last newline was received in number of 63 MHz clocks.
 */
int main(int argc, char **argv)
{
	signed long c;
	unsigned long d, d2;

	fifo_init();

	for(d = TIMER_REG;;) {
		while ((c = getc()) == -1);
		O_REG0 ^= 0x18000000;
		d2 = TIMER_REG;
		if (c == '\r') printf(" %d\r\n", d2 - d);
		else putc(c);
		d = d2;
	}

	return 0;
}

