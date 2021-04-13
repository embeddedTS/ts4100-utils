/* SPDX-License-Identifier: BSD-2-Clause */

/* Use gpiod bulk to grab three IO pins and rotate their output */
/* Example tool to demonstrate bulk line access with libgpiod via our gpiolib
 * intended for libgpiod based access.
 *
 * This tool allocates GPIOs 5 78, 5 79, and 5 80 on the TS-4100, these are
 * DIO_41, DIO_42, and DIO_43  which are on both the CN1 connector and the HD1
 * expansion header of the TS-4100.
 * Note that this is an output only example, but the process for input is not
 * much different. These CAN be read as inputs, but it only returns the output
 * state.
 */
#include <stdio.h>
#include <stdint.h>
#include <unistd.h>
#include <gpiod.h>

#include "gpiolib-gpiod.h"

int main(void)
{
	struct gpiod_chip *chip;
	struct gpiod_line_bulk bulk;
	unsigned int lines[3] = {78, 79, 80};
	int vals[3];
	int tmp;
	int *ptr;
	int i, x;

	chip = gpio_open_chip(5);
	if (chip == NULL) {
		return 1;
	}

	/* Allocate out 3 lines, numbered offsets stored in the lines array
	 * Exporting the GPIO in this way sets them to an input 
	 */
	if(gpio_export_bulk(chip, &bulk, lines, 3)) {
		return 1;
	}

	/* Set all IO to output high, 2 == output high, 1 == output low,
	 * 0 == input
	 */
	gpio_direction_bulk(&bulk, 2);

	/* Sleep 1s */
	usleep(1000000);

	/* Rotate the output through all three once a second */
	/* Example of manipulating the array as an array */
	vals[0] = 1;
	vals[1] = 0;
	vals[2] = 0;
	for (i = 0; i < 10; i++) {
		gpio_write_bulk(&bulk, vals);
		usleep(1000000);
		tmp = vals[0];
		for (x = 0; x < 2; x++) vals[x] = vals[x+1];
		vals[2] = tmp;
	}

	/* Set all IO low */
	/* Example of manipulating the array as a pointer */
	ptr = vals;
	for (i = 0; i < 3; i++) {
		*ptr = 0;
		ptr++;
	}
	gpio_write_bulk(&bulk, vals);

	/* As fast as possible, toggle one IO
	 * Note that since this GPIO is on the I2C bus via the FPGA, the speed
	 * is limited by that. CPU GPIO in general will be able to toggle much
	 * faster. */
	for (i = 1; i < 1001; i++) {
		vals[0] = i & 0x1;
		gpio_write_bulk(&bulk, vals);
	}

	/* Clean up */
	gpio_close_chip(chip);

	return 0;
}
