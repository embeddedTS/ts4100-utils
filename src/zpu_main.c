#include <stdarg.h>
#include <string.h>

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
#define I_REG0	  *(volatile unsigned long *)0x2000
#define I_REG1	  *(volatile unsigned long *)0x2004
#define I_REG2	  *(volatile unsigned long *)0x2008
#define OE_REG0	  *(volatile unsigned long *)0x2010
#define OE_REG1	  *(volatile unsigned long *)0x2014
#define OE_REG2	  *(volatile unsigned long *)0x2018
#define O_REG0	  *(volatile unsigned long *)0x2020
#define O_REG1	  *(volatile unsigned long *)0x2024
#define O_REG2	  *(volatile unsigned long *)0x2028

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
#define ZPU_TXFIFO_SIZE	256
#define ZPU_RXFIFO_SIZE	16
#define ZPU_TXFIFO_NOFLOW_OPT (1<<25)
static struct zpu_fifo {
	unsigned long flags;				// buffer sz, flow opt
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
 * Can be called directly
 */
void putc(char c)
{
	unsigned long put = fifo.txput;

	if (fifo.flags & ZPU_TXFIFO_NOFLOW_OPT) {
		fifo.txdat[put++] = c;
		if (put == sizeof(fifo.txdat)) put = 0;
	} else {
		fifo.txdat[put++] = c;
		if (put == sizeof(fifo.txdat)) put = 0;
		/* Pause until FIFO not full */
		while (put == fifo.txget &&
		  (fifo.flags & ZPU_TXFIFO_NOFLOW_OPT) == 0);
	}

	fifo.txput = put;

	/* Assert IRQ until I2C reads the LSB of fifo.txput */
	IRQ0_REG = (unsigned long)(&fifo.txput) + 3;
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
 * Can be called directly
 */
int puts(const char *s)
{
	unsigned long put = fifo.txput;
	unsigned char c;
	if (fifo.flags & ZPU_TXFIFO_NOFLOW_OPT) while ((c = *(s++)) != 0) {
		fifo.txdat[put++] = c;
		if (put == sizeof(fifo.txdat)) put = 0;
	} else {
		while ((c = *(s++)) != 0) {
			fifo.txdat[put++] = c;
			if (put == sizeof(fifo.txdat)) put = 0;
			/* Pause until FIFO not full */
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

	initfifo();

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

