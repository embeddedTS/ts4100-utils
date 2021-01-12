/* SPDX-License-Identifier: BSD-2-Clause */

#include <assert.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <getopt.h>
#include <asm-generic/ioctls.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/select.h>
#include <errno.h>
#include <stdint.h>
#include <linux/types.h>
#include <linux/limits.h>
#include <math.h>
#include <sys/time.h>
#include <termios.h>
#include <signal.h>

#include "fpga.h"
#include "gpiolib.h"

/* CPU GPIO number for the IRQ that the ZPU can control */
#define FPGA_IRQ	129
/* These numbers are from the perspective of the FPGA top level decode */
#define ZPU_RAM_START	0x2000
#define ZPU_RAM_SZ	0x2000

static int irqfd;
static uint32_t fifo_adr;
static uint32_t fifo_flags;
/* RX and TX naming is from the ZPU's point of view */
static uint16_t txfifo_sz, txfifo_put_adr, txfifo_dat_adr, txfifo_get_adr;
static uint16_t rxfifo_sz, rxfifo_put_adr, rxfifo_dat_adr, rxfifo_get_adr;
static uint8_t txget, rxget, rxfifo_spc;
static uint8_t txput = 0, rxput = 0;

/* Recalculate the ZPU RX buffer free space.
 * Used to update the local rxfifo_spc variable.
 *
 * Not intended to be called directly
 */
static void zpu_rx_recalc(int twifd)
{
	if (rxfifo_spc != (rxfifo_sz - 1)) {
		rxget = fpeek8(twifd, rxfifo_get_adr);
		if (rxget <= rxput) {
			rxfifo_spc =
			  rxfifo_sz - (rxput - rxget) - 1;
		} else {
			rxfifo_spc =
			  rxfifo_sz -
			  (rxput + (rxfifo_sz - rxget)) - 1;
		}
	}
}

/* This function must be called before any FIFO operations take place.
 * This function sets up the IRC, verifies that the running ZPU has the common
 * FIFO struct set up, and then gathers location information of the ZPU RAM
 * as well as initialization of the variables on the ZPU RAM side.
 *
 * If flow control is enabled, it tells the ZPU firmware to not attempt to put
 * more data in to the ZPU TX buffer until it is read from the CPU side. This
 * means that no data output will be lost, but it is possible for the ZPU to
 * stall execution.
 *
 * This function returns the FD of the IRQ, or an error if the IRQ GPIO was
 * unable to be opened.
 *
 * Can be called directly.
 */
int32_t zpu_fifo_init(int twifd, int flow_control)
{
	char gpio_buf[64];
	char x = '?';

	/* Use gpiolib functions to open IRQ, set input, and rising edge trig */
	gpio_export(FPGA_IRQ);
	gpio_direction(FPGA_IRQ, 0);
	gpio_setedge(FPGA_IRQ, 1, 0);

	/*
	 * Set up FIFO link addresses
	 */

	/* The ZPU stores the FIFO struct start address at 0x203C in
	 * FPGA I2C address map. However from the ZPU context it is at
	 * 0x3C. Acquire the struct address, byteswap, check it, put it
	 * in FPGA I2C address context.
	 */
	fpeekstream8(twifd, (uint8_t *)&fifo_adr, ZPU_RAM_START + 0x3c,4);
	fifo_adr = ntohl(fifo_adr);
	if (fifo_adr == 0 || fifo_adr >= ZPU_RAM_SZ) {
		fprintf(stderr, "ZPU connection refused\n");
		fprintf(stderr, "Is the ZPU application loaded and running?\n");
		close(twifd);
		return -1;
	}
	fifo_adr += ZPU_RAM_START;

	/* Now that we have the start of the FIFO struct in the ZPU,
	 * start getting flags and other data addresses from it.
	 * ZPU FIFO struct looks like:
	 *
	 * static struct zpu_fifo {
	 *   uint32_t flags;				// sizes, opt
	 *   uint32_t txput;				// TX FIFO head
	 *   volatile uint32_t txget;			// TX FIFO tail
	 *   uint8_t txdat[ZPU_TXFIFO_SIZE];		// TX buffer
	 *   volatile uint32_t rxput;			// RX FIFO head
	 *   uint32_t rxget;				// RX FIFO tail
	 *   volatile uint8_t rxdat[ZPU_RXFIFO_SIZE];	// RX buffer
	 * } fifo;
	 */
	fpeekstream8(twifd, (uint8_t *)&fifo_flags, fifo_adr, 4);
	fifo_flags = ntohl(fifo_flags);
	if (flow_control) fifo_flags &= ~(1 << 25);
	else fifo_flags |= (1 << 25);
	fpoke8(twifd, fifo_adr, fifo_flags >> 24);

	/* Sanity check
	 * TX and RX FIFO in the ZPU has an arbitrary limit of 256 bytes.
	 * Any larger than this and we error under the assumption that the data
	 * from the the struct is not valid for some reason.
	 */
	txfifo_sz = fifo_flags & 0xfff;
	assert(txfifo_sz <= 256);
	txfifo_put_adr = fifo_adr + 7;
	txfifo_get_adr = txfifo_put_adr + 4;
	txfifo_dat_adr = fifo_adr + 12;

	rxfifo_sz = (fifo_flags >> 12) & 0xfff;
	assert(rxfifo_sz <= 256);
	rxfifo_put_adr = txfifo_dat_adr + txfifo_sz + 3;
	rxfifo_get_adr = rxfifo_put_adr + 4;
	rxfifo_dat_adr = rxfifo_get_adr + 1;

	/* Get current RX FIFO position.
	 * Zero out TX FIFO by setting tail to head.
	 */
	rxput = fpeek8(twifd, rxfifo_put_adr);
	txget = txput = fpeek8(twifd, txfifo_put_adr);
	fpoke8(twifd, txfifo_get_adr, txget);
	rxfifo_spc = 0;
	zpu_rx_recalc(twifd);


	/* ZPU drives the FPGA IRQ line. */
	snprintf(gpio_buf, sizeof(gpio_buf), "/sys/class/gpio/gpio%d/value",
	  FPGA_IRQ);
	irqfd = open(gpio_buf, O_RDONLY);

	/* Drain the IRQ FD in case there is a spurious IRQ waiting */
	lseek(irqfd, 0, 0);
	read(irqfd, &x, 1);

	return irqfd;
};

/* This function should be called when disconnecting from the FIFO
 * It simply disables flow control from the ZPU TX FIFO. This allows the ZPU
 * to continue execution and not stall waiting for data to be removed from the
 * FIFO after we're disconnected from it.
 *
 * Can be called directly.
 */
void zpu_fifo_deinit(int twifd)
{
	fifo_flags |= (1<<25);
	fpoke8(twifd, fifo_adr, fifo_flags >> 24);
	close(irqfd);
}

/* The get and put functions are named from the CPU perspective, while variables
 * inside of them are named from the ZPU perspective.
 */

/* This function will read from ZPU FIFO, to buf, up to max size.
 * FIFO will be read until size bytes have been read, or until the FIFO is empty
 *
 * Passing a buffer larger than 256 bytes (the standard FIFO size) is not useful
 * or recommended.
 *
 * This function returns the number of bytes actually read from the FIFO.
 */
size_t zpu_fifo_get(int twifd, uint8_t *buf, size_t size)
{
	int rdsz0 = 0, rdsz = 0;

	assert(buf != NULL);

	/* ZPU sending data to host
	 *
	 * Get the current TX FIFO head
	 *
	 * If head pos. is behind the tail pos., then host will pull data out
	 * through the end of the FIFO in one contiguous chunk. rdsz0 is used
	 * in this case as an offset for later calculating the full count.
	 *
	 * Host pull out data from the tail through the current head.
	 *
	 * Update ZPU RAM and our local var with new tail pos.
	 */
	txput = fpeek8(twifd, txfifo_put_adr);
	if (txput != txget) {
		if (txput < txget) { 
			rdsz0 = txfifo_sz - txget;
			if (size < rdsz0) rdsz0 = size;
			fpeekstream8(twifd, buf,
			  txfifo_dat_adr + txget, rdsz0);
			size = size - rdsz0;
			txget = txget + rdsz0;
		}

		/* Skip the following if head still behind tail.
		 * Otherwise, keep trying to pull more data from FIFO.
		 */
		if (!(txput < txget)) {
			rdsz = txput - txget;
			if (size < rdsz) rdsz = size;
			if (rdsz) {
				fpeekstream8(twifd, buf + rdsz0,
				  txfifo_dat_adr + txget, rdsz);
				txget = txget + rdsz;
			}
		}

		rdsz += rdsz0;
		fpoke8(twifd, txfifo_get_adr, txget); 
	}

	/* Should never encounter a situation where no bytes were read and
	 * FIFO head and tail are not the same position.
	 */
	if (rdsz == 0 && txput != txget) assert(0);
	return rdsz;
}

/* This function will write to ZPU FIFO, from buf, up to max size.
 * The FIFO will be writen until size bytes have been placed in the FIFO, or
 * until the FIFO is full.
 *
 * Passing a buffer larger than 16 bytes (the standard FIFO size) is not useful
 * or recommended.
 *
 * This function returns the number of bytes actually written to the FIFO.
 */
size_t zpu_fifo_put(int twifd, uint8_t *buf, size_t size)
{
	size_t wrsz = 0;

	assert(buf != NULL);

	/* ZPU recv. data from host
	 *
	 * If the RX buffer has free space and the head is less than size bytes
	 * from the largest FIFO position, write data to the end of the FIFO.
	 *
	 * If the RX buffer has free space and the head is more than size bytes
	 * from the largest FIFO position, write data until size.
	 *
	 * Note that this process does not care about head/tail location. The
	 * amount of free space is known with rxfifo_spc, we never write more
	 * than that.
	 *
	 * Recalculate the amount of free space in the RX FIFO.
	 * Update RX FIFO head in ZPU RAM space.
	 */
	if (size > rxfifo_spc) size = rxfifo_spc;
	if (size > 0) {

		if ((rxput + size) > rxfifo_sz) {
			wrsz = rxfifo_sz - rxput;
			fpokestream8(twifd, buf, rxfifo_dat_adr + rxput,
			 wrsz);
			rxput = rxput + wrsz;
			assert(rxput <= rxfifo_sz);
			if (rxput == rxfifo_sz) rxput = 0;
			size = size - wrsz;
		}

		if (size > 0) {
			fpokestream8(twifd, buf + wrsz, rxfifo_dat_adr + rxput,
			  size);
			rxput = rxput + size;
			assert(rxput <= rxfifo_sz);
			if (rxput == rxfifo_sz) rxput = 0;
			wrsz = wrsz + size;
		}
		rxfifo_spc = rxfifo_spc - wrsz;
		fpoke8(twifd, rxfifo_put_adr, rxput);
	}
	zpu_rx_recalc(twifd);

	return wrsz;
}


/* MUXBUS specific functions
 *
 * The following functions are simple abstractions for use with the MUXBUX
 * interface bridge in the ZPU. They handle the packet structure for read/write
 * operations, as well as the IRQ indicating operation complete.
 */

#define MB_READ		(1 << 0)
#define MB_WRITE	(0 << 0)
#define MB_16BIT	(1 << 1)
#define MB_8BIT		(0 << 1)

/* MUXBUS 16bit peek
 *
 * Internally handles the IRQ from the ZPU. Function only returns when data is
 * fully read back from the ZPU FIFO.
 */
uint16_t zpu_muxbus_peek16(int twifd, uint16_t adr)
{
	char x = '?';
	uint8_t buf[3];
	fd_set efds;

	buf[0] = (MB_READ | MB_16BIT);
	buf[1] = (adr >> 8) & 0xFF;
	buf[2] = (adr & 0xFF);

	zpu_fifo_put(twifd, buf, 3);
	FD_ZERO(&efds);
	FD_SET(irqfd, &efds);
	/* TODO: Check the return value, maybe set a timeout? */
	select(irqfd + 1, NULL, NULL, &efds, NULL);
	if (FD_ISSET(irqfd, &efds)) {
		lseek(irqfd, 0, 0);
		read(irqfd, &x, 1);
		assert (x == '0' || x == '1');
	}
	zpu_fifo_get(twifd, buf, 2);
	return (uint16_t)(((buf[0] << 8) & 0xFF00) + (buf[1] & 0xFF));
}

/* MUXBUS 16bit poke
 *
 * Internally handles the IRQ from the ZPU. Function only returns when data is
 * successfully written to the MUXBUS register.
 */
void zpu_muxbus_poke16(int twifd, uint16_t adr, uint16_t dat)
{
	char x = '?';
	uint8_t buf[5];
	fd_set efds;

	buf[0] = (MB_WRITE | MB_16BIT);
	buf[1] = (adr >> 8) & 0xFF;
	buf[2] = (adr & 0xFF);
	buf[3] = (dat >> 8) & 0xFF;
	buf[4] = (dat & 0xFF);

	zpu_fifo_put(twifd, buf, 5);
	FD_ZERO(&efds);
	FD_SET(irqfd, &efds);
	/* TODO: Check the return value, maybe set a timeout? */
	select(irqfd + 1, NULL, NULL, &efds, NULL);
	if (FD_ISSET(irqfd, &efds)) {
		lseek(irqfd, 0, 0);
		read(irqfd, &x, 1);
		assert (x == '0' || x == '1');
	}
	/* Read required to clear IRQ from ZPU side */
	zpu_fifo_get(twifd, buf, 2);
}
