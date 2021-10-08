/* SPDX-License-Identifier: BSD-2-Clause */

#ifndef __MUXBUS_H__
#define __MUXBUS_H__

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

/* AD bits are 31:27 of REG1, and 10:0 of REG2 */
#define ALEn_mask	0x04000000 // (1 << 26)
#define DIR_mask	0x02000000 // (1 << 25)
#define CSn_mask	0x01000000 // (1 << 24)
#define BHEn_mask	0x00800000 // (1 << 23)
#define WAIT_mask	0x00400000 // (1 << 22)

/* MUXBUS timing definitions have been in number of clocks of the BB WB clk
 * In the case of the TS-8820, these are 100 MHz. MUXBUS guidelines can be
 * found here: https://wiki.embeddedTS.com/wiki/Generic_MUXBUS
 *
 * The ZPU in the TS-4100 operates at 63 MHz, and has a free running counter
 * that delay times are based on. This means that every 1.58 63 MHz clocks is
 * 1 100 MHz clock. Round up to 2 to be safe. Additionally, all of the delay
 * clks are + 1 in the final application
 *
 * The numbers below are based on a 0xF0FF value in the standard MUXBUS config
 * register.
 */
#if 1
#define TP_ALE		(0x06 + 1)
#define TH_ADR		(0x06 + 1)
#define TSU_DAT		(0x06 + 1)
#define TP_CS		(0x42 + 1)
#define TH_DAT		(0x0E + 1)

#else
#define TP_ALE		100
#define TH_ADR		100
#define TSU_DAT		100
#define TP_CS		100
#define TH_DAT		100
#endif

void initmuxbusio(void);
void set_alen(unsigned long val);
void set_dir(unsigned long val);
void set_csn(unsigned long val);
void set_bhen(unsigned long val);
unsigned long get_wait(void);
void set_ad(unsigned short dat);
void set_ad_oe(unsigned long dir);
unsigned short get_ad(void);
void delay_clks(unsigned short cnt);
void muxbus_write_16(unsigned short adr, unsigned short dat);
unsigned short muxbus_read_16(unsigned short adr);

#endif // __MUXBUS_H__
