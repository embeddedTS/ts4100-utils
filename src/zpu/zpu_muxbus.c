/* SPDX-License-Identifier: BSD-2-Clause */

#include <stdarg.h>
#include <string.h>

#include "muxbus.h"
#include "fifo.h"
#include "ts_zpu.h"

/* State machine defines for operation loop */
#define	GET_CMD		0
#define	GET_ADRH	1
#define	GET_ADRL	2
#define	GET_DATH	3
#define	GET_DATL	4
#define	RET_WRITE	5
#define	RET_READ	6

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
	unsigned char readcnt;

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
			readcnt = ((buf & 0xFC) >> 2) + 1;
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
			readcnt--;
			set_ad_oe(0);
			delay_clks(TSU_DAT);
			set_csn(0);
			delay_clks(TP_CS);
			dat = get_ad();
			set_csn(1);
			delay_clks(TH_DAT);
			/* Write both bytes to the FIFO, MSB first. Do not raise
			 * an IRQ until we're writing the absolute last byte
			 * of a stream of bytes.
			 * The CPU side is expecting to read a full two bytes in
			 * a single FIFO readout. It will also be more efficient
			 * since this can be done in a single I2C transaction.
			 */
			putc_noirq((dat >> 8) & 0xFF);
			if (!readcnt) {
				putc(dat & 0xFF);
				state = GET_CMD;
			} else {
				putc_noirq(dat & 0xFF);
			}
			break;
		  default:
			state = GET_CMD;
			break;
		}
	}

	return 0;
}

