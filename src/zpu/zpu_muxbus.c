/* SPDX-License-Identifier: BSD-2-Clause */

#include <stdarg.h>
#include <string.h>

#include "zpu_fifo.h"
#include "ts_zpu.h"

/* NOTE: In this current implementation, only 16-but MUXBUS access are supported.
 * This is meant specifically for the TS-8820 which has 16-bit registers only.
 */

#define READ		1
#define WRITE		0

/* AD bits are 31:27 of REG1, and 10:0 of REG2 */
#define ALEn_bit	26
#define DIR_bit		25
#define CSn_bit		24
#define BHEn_bit	23
#define WAIT_bit	22

/* MUXBUS timing definitions have been in number of clocks of the main BB clk.
 * In the case of the TS-8820, these are 12.5 MHz. MUXBUS guidelines can be
 * found here: https://wiki.embeddedarm.com/wiki/Generic_MUXBUS
 *
 * The ZPU in the TS-4100 operates at 63 MHz, and has a free running counter
 * that delay times are based on. This means that every 5.04 63 MHz clocks is
 * 1 12.5 MHz clock. Round up to 6 to be safe. Additionally, all of the delay
 * clks are + 1 in the final application
 *
 * The numbers below are based on a 0xF0FF value in the standard MUXBUS config
 * register.
 */
#define TP_ALE		(0x07 + 1)	* 6
#define TH_ADR		(0x21 + 1)	* 6
#define TSU_DAT		(0x03 + 1)	* 6
#define TP_CS		(0x03 + 1)	* 6
#define TH_DAT		(0x03 + 1)	* 6

/* State machine defines for operation loop */
#define	GET_CMD		0
#define	GET_ADRH	1
#define	GET_ADRL	2
#define	GET_DATH	3
#define	GET_DATL	4
#define	RET_WRITE	5
#define	RET_READ	6

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
 *   bit 2-7: Undefined
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
	O_REG1 |= ((1 << ALEn_bit) | (1 << CSn_bit) | (1 << DIR_bit) |
	  (1 << BHEn_bit));
	OE_REG1 |= ((1 << ALEn_bit) | (1 << CSn_bit) | (1 << DIR_bit) |
	  (1 << BHEn_bit));
}

void set_alen(unsigned long val)
{
	if (val) {
		O_REG1 |= (1 << ALEn_bit);
	} else {
		O_REG1 &= ~(1 << ALEn_bit);
	}
}

void set_dir(unsigned long val)
{
	if (val) {
		O_REG1 |= (1 << DIR_bit);
	} else {
		O_REG1 &= ~(1 << DIR_bit);
	}
}

void set_csn(unsigned long val)
{
	if (val) {
		O_REG1 |= (1 << CSn_bit);
	} else {
		O_REG1 &= ~(1 << CSn_bit);
	}
}

/* The TS-8820 actually ignores BHE# and only accepts 16bit accesses. This line
 * is unused in this application, but here for completeness
 */
void set_bhen(unsigned long val)
{
	if (val) {
		O_REG1 |= (1 << BHEn_bit);
	} else {
		O_REG1 &= ~(1 << BHEn_bit);
	}
}

unsigned long get_wait(void)
{
	return ((O_REG1 & (1 << WAIT_bit)) >> 21);
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
 */
void delay_clks(unsigned short cnt)
{
	unsigned long end_time;

	end_time = TIMER_REG + cnt;
	while ((signed long)(end_time - TIMER_REG) > 0);
}

/* The following functions are unused in this application as we normally
 * interleave reading bytes from the ZPU FIFO and doing MUXBUS accesses. Doing so
 * allows for a slightly smaller memory footprint
 * The functions are left in place for completeness, but are not compiled during
 * normal use. */
#if 0
void muxbus_write_16(unsigned short adr, unsigned short dat)
{
	set_dir(WRITE);
	set_ad(adr);
	set_ad_oe(1);
	set_alen(0);
	delay_clks(TP_ALE);
	set_alen(1);
	delay_clks(TH_ADR);
	set_ad(dat);
	delay_clks(TSU_DAT);
	set_csn(0);
	delay_clks(TP_CS);
	set_csn(1);
	delay_clks(TH_DAT);
}

unsigned short muxbus_read_16(unsigned short adr)
{
	unsigned short dat;

	set_dir(READ);
	set_ad(adr);
	set_ad_oe(1);
	set_alen(0);
	delay_clks(TP_ALE);
	set_alen(1);
	delay_clks(TH_ADR);
	set_ad_oe(0);
	delay_clks(TSU_DAT);
	set_csn(0);
	delay_clks(TP_CS);
	dat = get_ad();
	set_csn(1);
	delay_clks(TH_DAT);

	return dat;
}
#endif


/* ZPU MUXBUS application.
 *
 * As noted above, this is only intended for 16-bit access of the TS-8820 FPGA.
 * This implementation is packet based. e.g. a read is 3 bytes while a write is 5.
 * The return value for a read is 2 bytes, while a write only notifies the CPU
 * upon completion. IRQs are not asserted from the ZPU until a whole 16-bit word
 * is available from the MUXBUS transaction.
 */
int main(int argc, char **argv)
{
	unsigned char state = 0, rwn, width;
	unsigned short adr, dat;
	signed long buf;

	fifo_init();
	initmuxbusio();

	while(1) {
		/* Every loop of this state machine, query to see if there is new
		 * data in the RX FIFO. This only happens through the GET_DATL
		 * state, any states beyond GET_CMD, GET_ADRH, GET_ADRL, GET_DATH,
		 * and GET_DATL will no longer be expecting RX FIFO data */
		if (state < RET_WRITE) {
			while ((buf = getc()) == -1);
		}

		switch(state) {
		  /* Get command byte, first byte */
		  case GET_CMD:
			rwn = buf & 0x1;
			set_dir(rwn);
			width = (buf & 0x2) >> 1;
			state = GET_ADRH;
			adr = 0;
			dat = 0;
			break;
		  /* Get address high and low bytes, high byte first */
		  case GET_ADRH:
		  case GET_ADRL:
			adr = (adr << 8) + (buf & 0xFF);
			state++;
			if (state == GET_DATH) {
				set_ad(adr);
				set_ad_oe(1);
				set_alen(0);
				delay_clks(TP_ALE);
				set_alen(1);
				delay_clks(TH_ADR);
				if (rwn == READ) state = RET_READ;
			}
			break;
		  /* In the case of a write, get H/L bytes of data to write to the
		   * MUXBUS. High byte first. */
		  case GET_DATH:
		  case GET_DATL:
			dat = (dat << 8) + (buf & 0xFF);
			state++;
			break;
		  /* Do the actual write of data to MUXBUS register. While this
		   * does not return any data, an IRQ is still asserted to let the
		   * CPU know that the operation is complete */
		  case RET_WRITE:
			set_ad(dat);
			delay_clks(TSU_DAT);
			set_csn(0);
			delay_clks(TP_CS);
			set_csn(1);
			delay_clks(TH_DAT);
			/* Used to indicate to the CPU that data was written to
			 * MUXBUS. Dummy read of the FIFO is required from the CPU
			 * side. */
			fifo_raise_irq0();
			state = GET_CMD;
			break;
		  /* Do the actual read. The CPU is expecting a full 16-bit qty
		   * to be returned in a single read, therefore, do not assert IRQ
		   * after the first byte, only the second byte. */
		  case RET_READ:
			set_ad_oe(0);
			delay_clks(TSU_DAT);
			set_csn(0);
			delay_clks(TP_CS);
			dat = get_ad();
			set_csn(1);
			delay_clks(TH_DAT);
			/* Write both bytes to the FIFO, MSB first, do not raise
			 * an IRQ with the first byte, only the second. The CPU
			 * side is expecting to read a full two bytes in a single
			 * FIFO readout. It will also be more efficient since this
			 * can be done in a single I2C transaction. */
			putc_noirq((dat >> 8) & 0xFF);
			putc(dat & 0xFF);
			state = GET_CMD;
			break;
		  default:
			state = GET_CMD;
			break;
		}
	}

	return 0;
}

