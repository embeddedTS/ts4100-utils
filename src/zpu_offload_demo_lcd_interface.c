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

const char *motor_str[] = {
	"Man.",
	"R Up",
	"Auto",
	"R Dn",
	"R Up",
	"Auto",
	"R Dn",
	"Halt",
};

const char *info_str[] = {
	"This demo highlightsthe in-FPGA ZPU uC  in the TS-4100 CoM. It communicates    \x7e",

	"directly with the   TS-8820 baseboard tocontrol the I/O    \x7e",

	"and features. Linux userspace tools     interface with the \x7e",

	"ZPU FIFO to get infoout of the ZPU and  display it.        \x7e",

	"Relay mirror reads  digital input and   energizes relay fromthat signal.       \x7e",

	"ADC/DAC mirror readspotentiometer pos.  and displays it as avoltage on DAC out.\x7e",

	"Temp. reads the NTC thermistor probe andoutputs PWM value toanalog meter.      \x7e",

	"Motor reads multipledigital inputs and  ADC input to drive  H-bridge via PWM.  \x7e",
	"      \002                 \000\000embeddedTS       \001                 www.embeddedTS.com ",
	"",
};

int get_model()
{
	FILE *proc;
	char mdl[256];
	char *ptr;

	proc = fopen("/proc/device-tree/model", "r");
	if (!proc) {
		perror("model");
		return 0;
	}
	fread(mdl, 256, 1, proc);
	ptr = strstr(mdl, "TS-");
	return strtoull(ptr+3, NULL, 16);
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

void lcd_scroll_disable(int lcdfd)
{
	unsigned char cmd[2] = {0xFE, 0x52};

	lcd_write(lcdfd, cmd, sizeof(cmd));
}

void lcd_scroll_enable(int lcdfd)
{
	unsigned char cmd[2] = {0xFE, 0x51};

	lcd_write(lcdfd, cmd, sizeof(cmd));
}


void lcd_clear(int lcdfd)
{
	unsigned char cls[] = {0xFE, 0x58};
	lcd_write(lcdfd, cls, sizeof(cls));
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

	lcd_clear(lcdfd);
	lcd_scroll_disable(lcdfd);
	lcd_led_green(lcdfd);

	return lcdfd;
}

/* Check if we got an input, if so, display information on the project.
 * Screens are advanced with any button press. A timeout is set so after
 * enough time with no activity while in this loop, it simply exits.
 */
/* Note that, write failures are not handled here, that is a problem for the
 * main loop to deal with. Writing to the non-existant FD isn't an issue,
 * however, we do need to exit this function if a write fails in order to let
 * the main loop figure out what happened and try to re-establish connection to
 * the LCD.
 */
#define TIMEOUT_VAL_100MS 200
void lcd_demo_info_loop(int lcdfd)
{
	int i;
	int rc;
	unsigned char backspace[2] = { 0x08, ' '};
	unsigned char buf;
	int timeout;
	size_t len;

	rc = lcd_read(lcdfd, &buf, 1);
	if (rc < 1) return;

	lcd_clear(lcdfd);
	lcd_scroll_enable(lcdfd);

	for (i = 0; ; i++) {
		/* Since the last screen uses custom characters, these confuse
		 * strlen. So, if strlen is 0, break; if it is not 60 or 80,
		 * then force it to be 80 (the length of the last screen);
		 * otherwise the strlen is valid.
		 */
		len = strlen(info_str[i]);
		if (len == 0) break;
		if (len != 60 && len != 80) len = 80;

		/* Next, write out the next block of text */
		rc = lcd_write(lcdfd, info_str[i], len);
		if (rc < 0) return;
		//printf("wrote %d of %d\n", rc, len);

		/* Now, wait for another input */
		for (timeout = TIMEOUT_VAL_100MS; timeout; timeout--) {
			rc = lcd_read(lcdfd, &buf, 1);
			if (rc > 0) break;

			/* Use a dummy write to test if the LCD is still present */
			lcd_scroll_enable(lcdfd);
			if (!lcd_is_open) return;

			usleep(100000);
		}

		if (timeout == 0) break;

		/* Issue a backspace as the last character drawn may
		 * just have been a symbol that we don't want to scroll.
		 */
		rc = lcd_write(lcdfd, &backspace, sizeof(backspace));
		if (rc < 0) return;
	}

	/* Clear the LCD and disable scrolling */
	lcd_clear(lcdfd);
	lcd_scroll_disable(lcdfd);
}


int main(int argc, char **argv) {
	int twifd;
	uint8_t fifobuf[9]; // At most we expect 8 bytes
	int16_t temp;
	char lcdbuf[4][21] = {0};
	int irqfd;
	int lcdfd = 0;
	char x = '?';
	fd_set efds;
	int i;

	if(get_model() != 0x4100) {
		fprintf(stderr, "Unsupported model\n");
		return 1;
	}

	twifd = fpga_init("/dev/i2c-2", 0x28);
	if(twifd == -1) {
		/* fpga_init() calls open() which fails with errno set */
		perror("Can't open FPGA I2C bus");
		return 1;
	}

	/* In this specific application, the FIFO may not start up instantly,
	 * continue trying until we get a connection.
	 *
	 * This application cares about the IRQ from the ZPU as that is the
	 * signal that the whole packet we expect has been written to the FIFO.
	 */
	irqfd = zpu_fifo_init(twifd, FLOW_CTRL);
	if (irqfd < 0) {
		goto out;
	}

	while(1) {
		if (!lcd_is_open) {
			/* Close the fd if it was already opened */
			if (lcdfd > 0) {
				close(lcdfd);
				lcdfd = 0;
			}

			do {
				sleep(1);
				lcdfd = lcd_uart_open();
			} while (lcdfd == -1);
		}

		/* Check if button on LCD was pressed */
		lcd_demo_info_loop(lcdfd);

		/* 100 ms update interval */
		usleep(100000);

		/* Write a byte to trigger data out */
		fifobuf[0] = '\r';
		zpu_fifo_put(twifd, fifobuf, 1);

		/* Wait for IRQ from ZPU to denote the packet is complete and
		 * ready to consume.
		 */
		FD_ZERO(&efds);
		FD_SET(irqfd, &efds);
		select(irqfd + 1, NULL, NULL, &efds, NULL);
		if (FD_ISSET(irqfd, &efds)) {
			lseek(irqfd, 0, 0);
			read(irqfd, &x, 1);
			assert (x == '0' || x == '1');
		}
		zpu_fifo_get(twifd, fifobuf, sizeof(fifobuf));

		/* Update buffers to write to the LCD screen */
		if (fifobuf[0]) {
			memset(lcdbuf, '!', sizeof(lcdbuf));
			snprintf(lcdbuf[1], sizeof(lcdbuf[1]), "! E-Stop Triggered !");
	//		printf("!!!!!!!!!!!!!!!!!!!!\n");
	//		printf("! E-Stop Triggered !\n");
	//		printf("!!!!!!!!!!!!!!!!!!!!\n");
	//		printf("!!!!!!!!!!!!!!!!!!!!\n\n");
			for (i = 0; i < 4; i++) {
				lcd_write(lcdfd, lcdbuf[i], sizeof(lcdbuf[i])-1);
			}
			continue;
		}

		if (fifobuf[1]) {
			snprintf(lcdbuf[0], sizeof(lcdbuf[0]), "Relay: Energized    ");
		} else {
			snprintf(lcdbuf[0], sizeof(lcdbuf[0]), "Relay: Not Energized");
		}

		snprintf(lcdbuf[1], sizeof(lcdbuf[1]), "ADC/DAC Mirror: %3d%%",
			fifobuf[2]);

		temp = fifobuf[3];
		temp -= 25; // The scale is -25 to +125, but value from FIFO is 0-150

		snprintf(lcdbuf[2], sizeof(lcdbuf[2]), "Temp: %3d%cC PWM %3d%%", temp,
			0xDF, fifobuf[4]);
		snprintf(lcdbuf[3], sizeof(lcdbuf[3]), "Motor: %s %s %3d%%",
			motor_str[fifobuf[5]], fifobuf[6] ? "FWD" : "REV", fifobuf[7]);

		for (i = 0; i < 4; i++) {
			//printf("%s\n", lcdbuf[i]);
			lcd_write(lcdfd, lcdbuf[i], sizeof(lcdbuf[i])-1);
		}
	}

	zpu_fifo_deinit(twifd);

out:
	fpga_deinit(twifd);

	return 1;
}
