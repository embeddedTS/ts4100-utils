/* SPDX-License-Identifier: BSD-2-Clause */

#include <stdarg.h>
#include <string.h>

#include "ts_zpu.h"
#include "fifo.h"

#include "muxbus.h"

/* NOTE: In this current implementation, only 16-but MUXBUS access are supported.
 * This is meant specifically for the TS-8820 which has 16-bit registers only.
 */

#define READ		1
#define WRITE		0



/* MUXBUS packet construction
 *
 * NOTE: in this current implementation, only 16-bit MUXBUS accesses are
 * supported.
 *
 * The request packet is 3 or 5 bytes. The first byte being a configuration
 * packet, followed by the 16-bit MUXBUS address, and in the case of a write,
 * 16-bit data to write to the address.
 *
 * 3 or 5 bytes
 * MSB - 0
 *   bit 0: 1 = MB Read, 0 = MB Write
 *   bit 1: 1 = 16bit, 0 = 8bit
 *   bit 7-2: Number of reads to stream + 1
 *     A value from 0 to 63. Will cause <value>+1 muxbus transactions
 *     which will end up with <value>+1 * 2 bytes written to the TX buffer.
 *   This value only has meaning for reads, it is unused for MUXBUS writes.
 * MSB - 1:2
 *   MUXBUS address
 * MSB - 3:4
 *   MUXBUS data (only during MB Write)
 *
 * Response 2 bytes
 *   2:1 Read data
 *
 * After a successful MUXBUS read or write, the ZPU will assert an IRQ to the CPU.
 * In the case of a read, the 2 bytes are put in the FIFO before an IRQ is raised.
 * In the case of a write, no data is put in the ZPU TX FIFO, but an IRQ is still
 * asserted. This allows the CPU to wait until it can be assured the MUXBUS write
 * was completed. This is safe since the CPU side FIFO read would clear the IRQ,
 * and simply find that there was no new data in the buffer.
 */

void initmuxbusio(void)
{
	/* Enable 12.5 MHz clock on DIO_03 pin via FPGA reg 'd87 */
	O_REG2 |= (1 << 23);

	/* Set all AD pins to input for now */
	OE_REG1 &= ~(0xF8000000);
	OE_REG2 &= ~(0x000007FF);

	/* ALE#, CS#, DIR, and BHE# are always outputs
	 * Start them all off as high, aka deasserted. */
	O_REG1 |= (ALEn_mask | CSn_mask | DIR_mask | BHEn_mask);
	OE_REG1 |= (ALEn_mask | CSn_mask | DIR_mask | BHEn_mask);

	set_dir(WRITE);
}

void set_alen(unsigned long val)
{
	if (val) {
		O_REG1 |= (ALEn_mask);
	} else {
		O_REG1 &= ~(ALEn_mask);
	}
}

void set_dir(unsigned long val)
{
	if (val) {
		O_REG1 |= (DIR_mask);
	} else {
		O_REG1 &= ~(DIR_mask);
	}
}

void set_csn(unsigned long val)
{
	if (val) {
		O_REG1 |= (CSn_mask);
	} else {
		O_REG1 &= ~(CSn_mask);
	}
}

/* The TS-8820 actually ignores BHE# and only accepts 16bit accesses. This line
 * is unused in this application, but here for completeness
 */
void set_bhen(unsigned long val)
{
	if (val) {
		O_REG1 |= (BHEn_mask);
	} else {
		O_REG1 &= ~(BHEn_mask);
	}
}

unsigned long get_wait(void)
{
	return !!(O_REG1 & WAIT_mask);
}

void set_ad(unsigned short dat)
{
	O_REG1 &= ~(0xF8000000);
	O_REG1 |= ((unsigned long)(dat << 27 ) & 0xF8000000);

	O_REG2 &= ~(0x000007FF);
	O_REG2 |= ((unsigned long)(dat >> 5) & 0x000007FF);
}

void set_ad_oe(unsigned long dir)
{
	if (dir) {
		OE_REG1 |= (0xF8000000);
		OE_REG2 |= (0x000007FF);
	} else {
		OE_REG1 &= ~(0xF8000000);
		OE_REG2 &= ~(0x000007FF);
	}
}

unsigned short get_ad(void)
{
	unsigned short dat = 0;

	dat = ((I_REG2 & 0x000007FF) << 5);
	dat |= ((I_REG1 & 0xF8000000) >> 27);

	return dat;
}

/* This works so long as the maximum delay count is not more than half the span
 * of the free running counter. The counter in this is 32 bits wide, so limit
 * the max delay to 16 bits. In practice, this MUXBUS application will come
 * nowhere near that max.
 *
 * Each call to this function, takes roughly 30 us round trip with a cnt of 0.
 * Therefore, its better to busywait when needing delays shorter than 30 us.
 */
void delay_clks(unsigned short cnt)
{
	unsigned long end_time;


	end_time = TIMER_REG + cnt;
	while ((signed long)(end_time - TIMER_REG) > 0);
}

/* The following functions are unused in the generic MUXBUS application since it
 * interleaves reading bytes from the ZPU FIFO and doing MUXBUS accesses. Doing
 * so allows for a slightly smaller memory footprint
 *
 * These functions are most useful for the ZPU itself doing MUXBUS accesses.
 */
void muxbus_write_16(unsigned short adr, unsigned short dat)
{
	volatile cnt;

	set_dir(WRITE);
	set_ad(adr);
	set_ad_oe(1);
	set_alen(0);
	/* TP_ALE is 7 clocks, ZPU code will take longer than this between
	 * set_alen() calls, a delay is not needed here */
	set_alen(1);
	/* TH_ADR is 7 clocks, ZPU code will take longer than this between
	 * set_alen() and set_ad(), a delay is not needed here */
	set_ad(dat);
	/* TSU_DAT is 7 clocks, ZPU code will take longer than this between
	 * set_ad() and set_csn(), a delay is not needed here */
	set_csn(0);
	/* TP_CS is 67 clocks, which is roughly 1 us, short busyloop to
	 * accommodate this delay rather than calling the longer func */
	cnt = 1;
	while (cnt--);
	set_csn(1);
	/* TH_DAT is 15 clocks, we will never get back to this function, even
	 * when called back to back, shorter than this, delay not needed */
}

unsigned short muxbus_read_16(unsigned short adr)
{
	unsigned short dat;
	volatile cnt;

	set_dir(READ);
	set_ad(adr);
	set_ad_oe(1);
	set_alen(0);
	/* TP_ALE is 7 clocks, ZPU code will take longer than this between
	 * set_alen() calls, a delay is not needed here */
	set_alen(1);
	/* TH_ADR is 7 clocks, ZPU code will take longer than this between
	 * set_alen() and set_ad(), a delay is not needed here */
	set_ad_oe(0);
	/* TSU_DAT is 7 clocks, ZPU code will take longer than this between
	 * set_ad() and set_csn(), a delay is not needed here */
	set_csn(0);
	/* TP_CS is 67 clocks, which is roughly 1 us, short busyloop to
	 * accommodate this delay rather than calling the longer func */
	cnt = 1;
	while (cnt--);
	dat = get_ad();
	set_csn(1);
	/* TH_DAT is 15 clocks, we will never get back to this function, even
	 * when called back to back, shorter than this, delay not needed */

	return dat;
}

