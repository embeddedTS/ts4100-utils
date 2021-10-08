/* SPDX-License-Identifier: BSD-2-Clause */

#ifndef __FIFO_H__
#define __FIFO_H__

/*
 * Place a byte in the ZPU TX FIFO and raise an IRQ after. If flow control is
 * enabled, this function will stall (after asserting an IRQ) if the byte to be
 * placed would overflow the FIFO.
 */
void putc(char c);

/*
 * Place a byte in the ZPU TX FIFO, but do NOT raise an IRQ after. If flow control
 * is enabled, this function will stall (after asserting an IRQ) if the byte to be
 * placed would overflow the FIFO.
 */
void putc_noirq(char c);

/*
 * Place a NULL terminated string in the ZPU TX FIFO and raise an IRQ after.
 * If flow control is enabled, this function will stall (after asserting an IRQ)
 * if any byte in the string would overflow the FIFO.
 */
int puts(const char *s);

/*
 * Get a single byte from the RX FIFO.
 * Returns byte value if data was available, returns -1 if no new byte in RX FIFO
 */
signed long getc(void);

/*
 * Initialize the ZPU FIFO link.
 * This places a pointer to the FIFO structure at a known address in memory that
 * the CPU can query to establish the link. This _MUST_ be run before any data
 * can be transferred to or from the ZPU FIFO.
 */
void fifo_init(void);

/*
 * Raise an IRQ on the last TX FIFO head address.
 * Used to manually raise an IRQ to signal the CPU.
 */
void fifo_raise_irq0(void);

#endif // __FIFO_H__
