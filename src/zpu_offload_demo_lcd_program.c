/* SPDX-License-Identifier: BSD-2-Clause */

#include <assert.h>

#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <termios.h>
#include <unistd.h>

#include "tszpufifo.h"
#include "fpga.h"

const char copyright[] = "Copyright (c) embeddedTS - " __DATE__ " - "
  GITCOMMIT;

struct cust_chars {
	const uint8_t lines[8];
};

struct cust_chars cust_chars[] = {
	{{ 0x0E, 0x06, 0x0A, 0x10, 0x0E, 0x06, 0x0A, 0x10 }}, // Two up-right arrows
	{{ 0x07, 0x03, 0x05, 0x08, 0x00, 0x00, 0x00, 0x00 }}, // Top-right arrow
	{{ 0x00, 0x00, 0x00, 0x00, 0x0E, 0x06, 0x0A, 0x10 }}, // Bottom-left arrow
	{},
};

int lcd_write(int lcdfd, const void* buf, size_t count);

const char stringbuf[] = {
	"      \002                 \000\000embeddedTS       \001                 www.embeddedTS.com "};

void write_startup_string(int lcdfd)
{
	uint8_t cmd[2] = { 0xFE, 0x40 };

	lcd_write(lcdfd, cmd, sizeof(cmd));
	lcd_write(lcdfd, stringbuf, sizeof(stringbuf));
};


void write_cust_chars_startup_bank(int lcdfd)
{
	/* Just writes it to RAM */
	//uint8_t cmd[3] = {0xFE, 0x4E, 0x00};
	/* Writes and saves to flash */
	uint8_t cmd[4] = {0xFE, 0xC1, 0x00, 0x00};
	int i;

	for (i = 0; i < 3; i++) {
		cmd[3] = i;
		lcd_write(lcdfd, cmd, sizeof(cmd));
		lcd_write(lcdfd, &cust_chars[i], sizeof(struct cust_chars));
	}
}

/* Function to handle writing that will always check the return code and if
 * the write fails, assume the LCD is no longer present and let the main
 * loop handle opening and closing.
 */
int lcd_is_open = 0;
int lcd_write(int lcdfd, const void* buf, size_t count)
{
	int rc;
	rc = write(lcdfd, buf, count);
	if (rc < 0) {
		lcd_is_open = 0;
	}

	return rc;
}

/* Read handler meant to match the write handler */
static inline int lcd_read(int lcdfd, void *buf, size_t count)
{
	return read(lcdfd, buf, count);
}

/* Turns the LEDs from default Y to G */
void lcd_led_green(int lcdfd)
{
	unsigned char gpo_on[3] = {0xFE, 0x57, 2};
	unsigned char gpo_off[3] = {0xFE, 0x56, 1};
	int i;

	for (i = 0; i < 3; i++) {
		lcd_write(lcdfd, gpo_off, sizeof(gpo_off));
		lcd_write(lcdfd, gpo_on, sizeof(gpo_on));
		gpo_on[2] += 2;
		gpo_off[2] += 2;
	}
}

/* This will always be 19200 baud and on /dev/ttyUSB0 */
int lcd_uart_open(void)
{
	struct termios term;
	int lcdfd;

	//Open the serial port
	lcdfd = open("/dev/ttyUSB0", O_RDWR | O_NOCTTY | O_NONBLOCK);

	//handle errors
	if (lcdfd < 0) {
		return -1;
	}

	/* Let the interface settle before configuring to ensure it will
	 * accept all commands properly.
	 */
	sleep(1);

	//Setup the serial port 8 data bits, no parity ,1 stopbit, no flow control
	term.c_cflag = B19200 | CS8 | CSTOPB | CLOCAL | CREAD;
	term.c_iflag = 0;
	term.c_oflag = 0;
	term.c_lflag = 0;
	tcflush(lcdfd, TCIOFLUSH);
	tcsetattr(lcdfd,TCSANOW,&term);

	lcd_is_open = 1;

	return lcdfd;
}

int main(int argc, char **argv) {
	int lcdfd = 0;

	do {
		sleep(1);
		lcdfd = lcd_uart_open();
	} while (lcdfd == -1);

	write_cust_chars_startup_bank(lcdfd);
	write_startup_string(lcdfd);


	lcd_led_green(lcdfd);
	return 0;
}
