#include <stdarg.h>
#include <string.h>

/* XXX: Currently only supports 16bit transactions, meant for 8820 */

/* List of static locations in ZPU memory */

/* IRQ registers have a value written to them. In order to clear the IRQ in the
 * FPGA, the host CPU (not the ZPU) needs to do an I2C access to that address.
 * The IRQ is cleared automatically by the FPGA.
 *
 * Note that the FIFO uses IRQ0, and it is advised that customer applications
 * use IRQ1.
 */
#define IRQ0_REG  *(volatile unsigned long *)0x2030
#define IRQ1_REG  *(volatile unsigned long *)0x2034

/* 32-bit free running timer that runs when the FPGA is unreset. Runs at FPGA
 * main clock, 63 MHz
 */
#define TIMER_REG *(volatile unsigned long *)0x2030

/* Input, Output, and Output Enable registers.
 * These are 32-bit wide registers. Each bit position represents a single DIO
 * in contrast to the FPGA I2C address map which has each DIO in its own reg.
 * For example, bit 10 of O_REG1 controls the output state of DIO_4.
 * ((32 * 1) + 10) == 41 == DIO_4 in the FPGA GPIO map. All registers are mapped
 * the same way.
 * O_REG* is read/write, setting and reading back the output value.
 * I_REG* is read only, and reads the input value of each GPIO pin.
 * OE_REG* is read/write, setting a bit to a 1 sets that pin as an output.
 */
#define I_REG0		*(volatile unsigned long *)0x2000
#define I_REG1		*(volatile unsigned long *)0x2004
#define I_REG2		*(volatile unsigned long *)0x2008
#define OE_REG0		*(volatile unsigned long *)0x2010
#define OE_REG1		*(volatile unsigned long *)0x2014
#define OE_REG2		*(volatile unsigned long *)0x2018
#define O_REG0		*(volatile unsigned long *)0x2020
#define O_REG1		*(volatile unsigned long *)0x2024
#define O_REG2		*(volatile unsigned long *)0x2028

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
 *
 * TODO: Find the best numbers to use for the TS-8820, 0xFOFF is "worst case"
 */
#define TP_ALE		(0x07 + 1)	* 6
#define TH_ADR		(0x21 + 1)	* 6
#define TSU_DAT		(0x03 + 1)	* 6
#define TP_CS		(0x03 + 1)	* 6
#define TH_DAT		(0x03 + 1)	* 6

/* MUXBUS packet construction
 *
 * MSB
 * bit 0: 1 = MB Read, 0 = MB Write
 * bit 1: 1 = 16bit, 0 = 8bit
 *
 * Following that, exact expected byte count is known
 *
 * 3 or 5 bytes
 * MSB =
 *   0: read
 *   1: write
 *   *: Undefined (Expand to support 8bit transactions?
 * MSB - 1:2
 *   MUXBUS address
 * MSB - 3:4
 *   MUXBUS data (optional)
 *
 * Response 2 bytes
 *   2:1 Read data
 *
 * Need to think about how to clearly define a packet start and end so the FIFO
 * can resync as necessary. A two way IRQ could be used, this was implemented
 * in other cutom logic. Assert it when there is data ready for the ZPU to read.
 */

/* FIFO Connection to CPU
 *
 * While the ZPU can be used standalone, its often beneficial to move data
 * between it and the host CPU. We have created a simple FIFO that resides in
 * the ZPU memory space. Due to the layout of the FPGA and memory, the CPU has
 * full access to ZPU RAM at any time. This allows the CPU to pull/push data
 * to the ZPU fifo as needed.
 *
 * All of the below functions are commented for clarity.
 *
 * HOWEVER! We do not recommend modifying these functions in any way unless you
 * know exactly what you are doing!
 */

/* Setup of FIFO sizes as well as the struct that contains the FIFO */
#define ZPU_TXFIFO_SIZE		256
#define ZPU_RXFIFO_SIZE		16
#define ZPU_TXFIFO_NOFLOW_OPT	(1 << 25)
#define ZPU_ATTENTION		(1 << 26)
#define MB_WIDTH		(1 << 27) // 1 = 16bit, 0 = 8bit
#define MB_WRn			(1 << 28)
static struct zpu_fifo {
	volatile unsigned long flags;			// buffer sz, flow opt
	unsigned long txput;				// TX FIFO head
	volatile unsigned long txget;			// TX FIFO tail
	unsigned char txdat[ZPU_TXFIFO_SIZE];		// TX buffer
	volatile unsigned long rxput;			// RX FIFO head
	unsigned long rxget;				// RX FIFO tail
	volatile unsigned char rxdat[ZPU_RXFIFO_SIZE];  // RX buffer
} fifo;

/* This function is used from printf() to format int's to printable ASCII
 * Not intended to be called directly
 */
void _printn(unsigned u, unsigned base, char issigned,
  volatile void (*emitter)(char, void *), void *pData)
{
	const char *_hex = "0123456789ABCDEF";
	if (issigned && ((int)u < 0)) {
		(*emitter)('-', pData);
		u = (unsigned)-((int)u);
	}
	if (u >= base) _printn(u/base, base, 0, emitter, pData);
	(*emitter)(_hex[u%base], pData);
}

/* A light-weight implementation of printf() that supports the common formats
 * Not intended to be called directly
 */
void _printf(const char *format, volatile void (*emitter)(char, void *),
  void *pData, va_list va)
{
	char c;
	unsigned u;
	char *s;

	while (*format) {
		if (*format == '%') {
			switch (*++format) {
			  case 'c':
				c = (char)va_arg(va, int);
				(*emitter)(c, pData);
				break;
			  case 'u':
				u = va_arg(va, unsigned);
				_printn(u, 10, 0, emitter, pData);
				break;
			  case 'd':
				u = va_arg(va, unsigned);
				_printn(u, 10, 1, emitter, pData);
				break;
			  case 'x':
				u = va_arg(va, unsigned);
				_printn(u, 16, 0, emitter, pData);
				break;
			  case 's':
				s = va_arg(va, char *);
				while (*s) {
					(*emitter)(*s, pData);
					s++;
				}
			}
		} else {
			(*emitter)(*format, pData);
		}

		format++;
	}
}

/* Called from sprintf() to output string to a buffer.
 * Not intended to be called directly
 */
void _buf_emitter(char c, void *pData)
{
	*((*((char **)pData)))++ = c;
}

/* Place a single byte in to the TX FIFO
 * Once the byte is added to the FIFO, raise the IRQ with the address of the
 * TX FIFO head. When the CPU reads the current TX FIFO head, the IRQ is cleared
 * automatically by the FPGA.
 *
 * This function will not immediately return if the last byte put in to the TX
 * FIFO is one pos. behind the current tail which would result in the current
 * head and tail pos. being the same as head is post incremented. This only
 * applies if the CPU has requested flow control to be enabled.
 *
 * A intermediate variable is used for the TX FIFO head location.
 *
 * Can be called directly
 */
void putc(char c)
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

		while (put == fifo.txget &&
		  (fifo.flags & ZPU_TXFIFO_NOFLOW_OPT) == 0);
	}

	fifo.txput = put;
}

/* Called from printf() to output each character to the FIFO.
 * Not intended to be called directly
 */
void _char_emitter(char c, void *pData)
{
	putc(c);
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
 * long as flow control is enabled no data will be lost.
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

			while (put == fifo.txget &&
			  (fifo.flags & ZPU_TXFIFO_NOFLOW_OPT) == 0);
		}
	}

	fifo.txput = put;

	return 0;
}

/* The actual printf() function used that calls another function to do the real
 * formatting.
 * Can be called directly
 */
int printf(const char *format, ...)
{
	va_list va;

	va_start(va, format);
	_printf(format, _char_emitter, NULL, va);
	return 0;
}

/* An implementation of sprintf() that uses the same printf() formatting func.
 * Can be called directly
 */
int sprintf(char *pInto, const char *format, ...)
{
	va_list va;
	char *pInto_orig = pInto;

	va_start(va, format);
	_printf(format, _buf_emitter, &pInto, va);
	*pInto = '\0';

	return pInto - pInto_orig;
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
 */
void initfifo(void)
{
	*(unsigned long *)0x3c = (unsigned long)(&fifo);
	fifo.flags = sizeof(fifo.txdat) | sizeof(fifo.rxdat) << 12 |
	  ZPU_TXFIFO_NOFLOW_OPT;
}

/* This ends the TS created FIFO code. */

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

void delay_clks(unsigned long cnt)
{
	unsigned long cur_time = TIMER_REG;

	if ((cur_time + cnt) > cur_time) {
		while ((cur_time + cnt) > TIMER_REG);
	} else {
		while ((cur_time + cnt) < TIMER_REG);
	}
}

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


/* ZPU MUXBUS application.
 *
 * TODO: Comment this application and the state machine below
 */
int main(int argc, char **argv)
{
	unsigned char state = 0, rwn, width;
	unsigned short adr, dat;
	signed long buf;

	initfifo();
	initmuxbusio();

	/* NOTE: Assert IRQ only when we have fully put a packet in the FIFO
	 * This could cause overrun issues though if not handled carefully.
	 */

	while(1) {
		if (state < 5) {
			while ((buf = getc()) == -1);
		}

		switch(state) {
		  /* Get command byte, first byte */
		  case 0:
			rwn = buf & 0x1;
			set_dir(rwn);
			width = (buf & 0x2) >> 1;
			state++;
			adr = 0;
			dat = 0;
			break;
		  case 1:
		  case 2:
			adr = (adr << 8) + (buf & 0xFF);
			state++;
			if (state == 3) {
				set_ad(adr);
				set_ad_oe(1);
				set_alen(0);
				delay_clks(TP_ALE);
				set_alen(1);
				delay_clks(TH_ADR);
				if (rwn == READ) state = 6;
			}
			break;
		  case 3:
		  case 4:
			dat = (dat << 8) + (buf & 0xFF);
			state++;
			break;
		  case 5:
			set_ad(dat);
			delay_clks(TSU_DAT);
			set_csn(0);
			delay_clks(TP_CS);
			set_csn(1);
			delay_clks(TH_DAT);
			IRQ0_REG = (unsigned long)(&fifo.txput) + 3;
			state = 0;
			break;
		  case 6:
			set_ad_oe(0);
			delay_clks(TSU_DAT);
			set_csn(0);
			delay_clks(TP_CS);
			dat = get_ad();
			set_csn(1);
			delay_clks(TH_DAT);
			putc((dat >> 8) & 0xFF);
			putc(dat & 0xFF);
			IRQ0_REG = (unsigned long)(&fifo.txput) + 3;
			state = 0;
			break;
		  default:
			state = 0;
			break;
		}
	}

	return 0;
}

