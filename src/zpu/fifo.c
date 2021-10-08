/* SPDX-License-Identifier: BSD-2-Clause */

#include <stdarg.h>
#include <string.h>

#include "ts_zpu.h"

/* FIFO Connection to CPU
 *
 * While the ZPU can be used standalone, its often beneficial to move data
 * between it and the host CPU. We have created a simple FIFO that resides in
 * the ZPU memory space. Due to the layout of the FPGA and memory, the CPU has
 * full access to ZPU RAM at any time. This allows the CPU to pull/push data
 * to the ZPU fifo as needed.
 *
 * All of the below functions are commented for clarity.
 */

/* NOTE: This code is not intended to be modified in any way. While the use of the
 * FIFO is not necesary for operation of the ZPU, this interface is one that we
 * provide and use in our examples and production applications. Modification of
 * these functions may result in our production applications breaking in
 * unexpected ways. */


/* Setup of FIFO sizes as well as the struct that contains the FIFO */
#define ZPU_TXFIFO_SIZE		256
#define ZPU_RXFIFO_SIZE		16
#define ZPU_TXFIFO_NOFLOW_OPT	(1 << 25)
#define ZPU_ATTENTION		(1 << 26)
static struct zpu_fifo {
	volatile unsigned long flags;			// buffer sz, flow opt
	unsigned long txput;				// TX FIFO head
	volatile unsigned long txget;			// TX FIFO tail
	unsigned char txdat[ZPU_TXFIFO_SIZE];		// TX buffer
	volatile unsigned long rxput;			// RX FIFO head
	unsigned long rxget;				// RX FIFO tail
	volatile unsigned char rxdat[ZPU_RXFIFO_SIZE];  // RX buffer
} fifo;

/* Place a single byte in to the TX FIFO
 *
 * This will not raise an IRQ when a byte is placed in the FIFO normally.
 * See putc() below
 *
 * This function will not immediately return if the last byte put in to the TX
 * FIFO is one pos. behind the current tail which would result in the current
 * head and tail pos. being the same as head is post incremented. In order to
 * get the CPU's attention, an IRQ is raised before busywaiting for the FIFO to
 * be emptied. This only applies if the CPU has requested flow control is enabled.
 * If flow control is disabled in this state, then the busywait will end.
 *
 * A intermediate variable is used for the TX FIFO head location.
 *
 * Intended to be called directly. If an IRQ is required after putc, use putc()
 * below.
 */
void putc_noirq(char c)
{
	unsigned long put = fifo.txput;

	fifo.txdat[put++] = c;
	if (put == sizeof(fifo.txdat)) put = 0;

	/* If the head was just set to the tail position after incrementing, and
	 * the CPU has requested that flow control is enabled; set the head loc.
	 * to just behind the tail, assert the IRQ, and spin until the tail is
	 * moved (data read by CPU) or flow control is disabled by the CPU.
	 */
	if (put == fifo.txget &&
	  (fifo.flags & ZPU_TXFIFO_NOFLOW_OPT) == 0) {
		fifo.txput = (put - 1);

		/* Raise IRQ if there is no space left in the buffer. While an IRQ
		 * likely has already been raised with a series of putc() calls,
		 * do it again to ensure that the CPU is aware that the firmware
		 * is now busylooping. */
		//fifo_raise_irq0()
		IRQ0_REG = (unsigned long)(&fifo.txput) + 3;

		/* Pause until FIFO not full or flow control disabled */
		while (put == fifo.txget &&
		  (fifo.flags & ZPU_TXFIFO_NOFLOW_OPT) == 0);
	}

	fifo.txput = put;
}

/* Put a byte in to the TX FIFO and raise an IRQ to the CPU. Calls putc_noirq()
 * for the heavy lifting.
 *
 * An IRQ is raised to the CPU with the address of the TX FIFO head. Once the CPU
 * reads from that address, the IRQ is automatically cleared by the FPGA.
 *
 * This function may stall and issue an IRQ if the FIFO is full when attempting
 * to place the byte in to it.
 */
void putc(char c)
{
	putc_noirq(c);
	IRQ0_REG = (unsigned long)(&fifo.txput) + 3; //fifo_raise_irq0()
}

/* Place a null terminated string in to the TX FIFO.
 * This will directly write to the FIFO itself.
 *
 * Once the string has been completely added to the FIFO, raise the IRQ with the
 * address of the TX FIFO head. When the CPU reads the current TX FIFO head, the
 * IRQ is automatically cleared by the FPGA.
 *
 * This function will not immediately return if the last byte put in to the TX
 * FIFO is one pos. behind the current tail which would result in the current
 * head and tail pos. being the same as head is post incremented. This only
 * applies if the CPU has requested flow control to be enabled.
 *
 * If the string is longer than the TX FIFO or there is not enough space, so
 * long as flow control is enabled no data will be lost. This function will raise
 * an IRQ, stall, and wait for the CPU to read from the FIFO.
 *
 * A intermediate variable is used for the TX FIFO head location.
 *
 * Can be called directly
 */
int puts(const char *s)
{
	unsigned long put = fifo.txput;
	unsigned char c;

	while ((c = *(s++)) != 0) {
		fifo.txdat[put++] = c;
		if (put == sizeof(fifo.txdat)) put = 0;

		/* If the head was just set to the tail position after
		 * incrementing, and the CPU has request that flow control is
		 * enabled; set the head loc. to just behind the tail, assert
		 * the IRQ, and spin until the tail is moved (data read by CPU)
		 * or flow control is disabled by the CPU.
		 *
		 * This can happen if the string is bigger than the FIFO or if
		 * there is already data waiting in the FIFO.
		 */
		if (put == fifo.txget &&
		  (fifo.flags & ZPU_TXFIFO_NOFLOW_OPT) == 0) {
			fifo.txput = (put - 1);

			/* Raise IRQ in case the string to write is longer than
			 * the buffer and an IRQ is otherwise unraised */
			//fifo_raise_irq0()
			IRQ0_REG = (unsigned long)(&fifo.txput) + 3;

			/* Pause until FIFO not full or flow control disabled */
			while (put == fifo.txget &&
			  (fifo.flags & ZPU_TXFIFO_NOFLOW_OPT) == 0);
		}
	}

	fifo.txput = put;
	IRQ0_REG = (unsigned long)(&fifo.txput) + 3; //fifo_raise_irq0()

	return 0;
}

/* Receive a single byte from the RX FIFO.
 * This is simply polled from from the main program flow.
 * Can be called directly
 */
signed long getc(void)
{
	signed long r;
	unsigned long rxget = fifo.rxget;
	if (rxget != fifo.rxput) {
		r = fifo.rxdat[rxget++];
		if (rxget == sizeof(fifo.rxdat)) rxget = 0;
		fifo.rxget = rxget;
		return r;
	} else {
		return -1;
	}
}

/* Initialize the FIFO link so the CPU knows where it is and how to access it.
 * This needs to be called early in the main() function, before any FIFO actions
 * take place.
 *
 * The RAM address of the FIFO struct is stored at 0x3c (translates to 0x203c
 * in the FPGA I2C addressing). The CPU FIFO checks this location and the
 * software sets up all of the offsets based on this.
 *
 * In the ZPU architecture, this address is within the IRQ vector table. Since we
 * do not use the IVT, we repurpose it for this common communication structure.
 */
void fifo_init(void)
{
	*(unsigned long *)0x3c = (unsigned long)(&fifo);
	fifo.flags = sizeof(fifo.txdat) | sizeof(fifo.rxdat) << 12 |
	  ZPU_TXFIFO_NOFLOW_OPT;
}

/* Raise IRQ0 on the last TX FIFO address
 * When the CPU reads the address, the IRQ qill be desasserted
 */
void fifo_raise_irq0(void)
{
	IRQ0_REG = (unsigned long)(&fifo.txput) + 3;
}

/* This ends the TS created FIFO code. */

