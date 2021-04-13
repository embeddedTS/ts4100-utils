/* SPDX-License-Identifier: BSD-2-Clause */

/* This API is meant as both an example, and a usable API similar to the same
 * gpiolib.c using the sysfs/ GPIO access method. While not a 1:1 API match,
 * this abstraction should help ease transition from sysfs/ to libgpiod access.
 *
 * This does not use contextless libgpiod accesses. This allows for faster GPIO
 * manipulation at the cost of maintaining handles for chip and line access.
 * For information on the contextless API as well as general libgpiod info,
 * please see the official libgpiod project documentation:
 * https://git.kernel.org/pub/scm/libs/libgpiod/libgpiod.git/
 *
 * See the main() for examples of how to instantiate and use a single GPIO
 * line with this API. A separate bulk demo exists in this git repo to
 * demonstrate bulk line access.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <sys/select.h>
#include <sys/stat.h>
#include <gpiod.h>

#ifdef CTL
#include <getopt.h>
const char copyright[] = "Copyright (c) Technologic Systems - " __DATE__ " - "
  GITCOMMIT;
#endif

/* 0 = in
 * 1 = out/low
 * 2 = out/high
 *
 * Need to release a line and re-request it, libgpiod doesn't support arbitrary
 * direction changing in kernel 4.9.y
 */
int gpio_direction(struct gpiod_line *line, int dir)
{
	int ret;
	gpiod_line_release(line);

	switch (dir) {
	  case 0:
		ret = gpiod_line_request_input(line, "GPIOLIB");
		break;
	  case 1:
		ret = gpiod_line_request_output(line, "GPIOLIB", 0);
		break;
	  case 2:
		ret = gpiod_line_request_output(line, "GPIOLIB", 1);
		break;
	  default:
		ret = -1;
		break;
	}

	return ret;
}

/* Changes every bulk GPIO line to "dir", cannot set each line dir individually
 * 0 = in
 * 1 = out/low
 * 2 = out/high
 *
 * Need to release a line and re-request it, libgpiod doesn't support arbitrary
 * direction changing in kernel 4.9.y
 */
int gpio_direction_bulk(struct gpiod_line_bulk *bulk, int dir)
{
	int ret;
	int lines;
	void *vals = NULL;

	/* Need to get number of lines from bulk so we can set output state. */
	lines = gpiod_line_bulk_num_lines(bulk);

	gpiod_line_release_bulk(bulk);

	switch (dir) {
	  case 0:
		ret = gpiod_line_request_bulk_input(bulk, "GPIOLIB");
		break;
	  case 1:
		/* Create an array of values to initialize the lines */
		vals = malloc(lines * sizeof(int));
		memset((int *)vals, 0x00, (lines * sizeof(int)));
		ret = gpiod_line_request_bulk_output(bulk, "GPIOLIB", vals);
		free(vals);
		break;
	  case 2:
		/* Create an array of values to initialize the lines */
		vals = malloc(lines * sizeof(int));
		memset((int *)vals, 0x01, (lines * sizeof(int)));
		ret = gpiod_line_request_bulk_output(bulk, "GPIOLIB", vals);
		free(vals);
		break;
	  default:
		ret = -1;
		break;
	}

	return ret;
}

#if 0
int gpio_setedge(int gpio, int rising, int falling)
{
	int ret = 0;
	char buf[50];
	sprintf(buf, "/sys/class/gpio/gpio%d/edge", gpio);
	int gpiofd = open(buf, O_WRONLY);
	if(gpiofd < 0) {
		perror("Couldn't open IRQ file");
		ret = -1;
	}

	if(gpiofd && rising && falling) {
		if(4 != write(gpiofd, "both", 4)) {
			perror("Failed to set IRQ to both falling & rising");
			ret = -2;
		}
	} else {
		if(rising && gpiofd) {
			if(6 != write(gpiofd, "rising", 6)) {
				perror("Failed to set IRQ to rising");
				ret = -2;
			}
		} else if(falling && gpiofd) {
			if(7 != write(gpiofd, "falling", 7)) {
				perror("Failed to set IRQ to falling");
				ret = -3;
			}
		}
	}

	close(gpiofd);
	return ret;
}
#endif

struct gpiod_chip* gpio_open_chip(int chip_num)
{
	struct gpiod_chip *chip = NULL;

	chip = gpiod_chip_open_by_number(chip_num);
	if (chip == NULL) {
		perror("Unable to open chip");
	}

	return chip;
}

/* Closes all associated lines too */
void gpio_close_chip(struct gpiod_chip *chip)
{
	gpiod_chip_close(chip);
}

/* Open a single line and request it as an input.
 * Requires gpio_open_chip to return a valid gpiod_chip handle first before this
 * function can be called.
 *
 * Returns a valid gpiod_line handle or NULL on failure.
 */
struct gpiod_line* gpio_export(struct gpiod_chip *chip, int line_num)
{
	struct gpiod_line *line = NULL;


	line = gpiod_chip_get_line(chip, line_num);
	if (line == NULL) {
		perror("Unable to get line");
	}

	if (gpiod_line_request_input(line, "GPIOLIB")) {
		perror("Unable to request line");
		line = NULL;
	}

	return line;
}

/* Releases a single line. The gpiod_chip handle remains untouched and the line
 * state is unchanged when released.
 */
void gpio_unexport(struct gpiod_line *line)
{
	gpiod_line_release(line);
}

/* Returns a single line value. Either 0, 1, or -1 on any failure */
int gpio_read(struct gpiod_line *line)
{
	return gpiod_line_get_value(line);
}

/* Write a value to a single line. Val can be 0 or 1. Returns 0 on success,
 * -1 on any failure */
int gpio_write(struct gpiod_line *line, int val)
{
	return gpiod_line_set_value(line, val);
}

/* Open bulk lines and request it as an input.
 * Requires gpio_open_chip to return a valid gpiod_chip handle first before this
 * function can be called.
 *
 * The passed gpiod_line_bulk must be created outside of this function! For
 * some reason, libgpiod expects the end application to maintain the bulk
 * struct statically vs chip/line being allocated dynamically by libgpiod.
 * This will set the correct data inside of gpiod_line_bulk upon completion
 * 
 * The lines arg must be a uint array that is as many lines as num_lines being
 * requested,  and each array element lists the offset/line inside of the chip
 * to request.
 *
 * Returns 0 on success, -1 on any failure.
 */
int gpio_export_bulk(struct gpiod_chip *chip, struct gpiod_line_bulk *bulk,
						unsigned int *lines,
						unsigned int num_lines)
{
	int ret = 0;

	gpiod_line_bulk_init(bulk);
	if (gpiod_chip_get_lines(chip, lines, num_lines, bulk)) {
		perror("Unable to get lines");
		ret = -1;
	}

	if (gpiod_line_request_bulk_input(bulk, "GPIOLIB")) {
		perror("Unable to request lines");
		ret = -1;
	}

	return ret;
}

/* Releases bulk lines. The gpiod_chip handle remains untouched and the line
 * states are unchanged when released.
 */
void gpio_unexport_bulk(struct gpiod_line_bulk *bulk)
{
	gpiod_line_release_bulk(bulk);
}

/* Returns all bulk values in *values array. This array MUST be as large or
 * larger than num_lines * sizeof(uint)!
 *
 * Returns 0 on success, -1 on any failure
 */
int gpio_read_bulk(struct gpiod_line_bulk *bulk, int *values)
{
	return gpiod_line_get_value_bulk(bulk, values);
}

/* Sets all bulk values in *values array. This array MUST be as large or
 * larger than num_lines * sizeof(uint)!
 *
 * Returns 0 on success, -1 on any failure
 */
int gpio_write_bulk(struct gpiod_line_bulk *bulk, int *values)
{
	return gpiod_line_set_value_bulk(bulk, values);
}

#if 0
int gpio_select(int gpio)
{
	char gpio_irq[64];
	int buf, irqfd;
	fd_set fds;
	FD_ZERO(&fds);

	snprintf(gpio_irq, sizeof(gpio_irq), "/sys/class/gpio/gpio%d/value", gpio);
	irqfd = open(gpio_irq, O_RDONLY, S_IREAD);
	if(irqfd < 1) {
		perror("Couldn't open the value file");
		return -1;
	}

	// Read first since there is always an initial status
	read(irqfd, &buf, sizeof(buf));

	while(1) {
		FD_SET(irqfd, &fds);
		select(irqfd + 1, NULL, NULL, &fds, NULL);
		if(FD_ISSET(irqfd, &fds))
		{
			FD_CLR(irqfd, &fds);  //Remove the filedes from set
			// Clear the junk data in the IRQ file
			read(irqfd, &buf, sizeof(buf));
			return 1;
		}
	}
}
#endif

#ifdef CTL

static void usage(char **argv) {
	fprintf(stderr,
	  "%s\n\n"
	  "Usage: %s [OPTION] ...\n"
	  "Simple gpio access\n"
	  "\n"
	  "  -h, --help                  This message\n"
	  "  -p, --getin <chip>_<pin>    Returns the input value of DIO\n"
	  "  -e, --setout <chip>_<pin>   Sets DIO output value high\n"
	  "  -l, --clrout <chip>_<pin>   Sets DIO output value low\n"
	  "  -d, --ddrout <chip>_<pin>   Set  DIO to an output\n"
	  "  -w, --waitfor <chip>_<pin>  Wait for IO to change to the configured edge\n"
	  "  -r, --ddrin <chip>_<pin>    Set sysfs DIO to an input\n\n",
	  copyright, argv[0]
	);
}

int main(int argc, char **argv)
{
	int c;
	struct gpiod_chip *chip;
	struct gpiod_line *line;
	char *end;
	static struct option long_options[] = {
	  { "getin", 1, 0, 'p' },
	  { "setout", 1, 0, 'e' },
	  { "clrout", 1, 0, 'l' },
	  { "ddrout", 1, 0, 'd' },
	  { "ddrin", 1, 0, 'r' },
	  { "waitfor", 1, 0, 'w' },
	  { "help", 0, 0, 'h' },
	  { 0, 0, 0, 0 }
	};

	if(argc == 1) {
		usage(argv);
		return(1);
	}

	while((c = getopt_long(argc, argv, "p:e:l:d:r:w:", long_options, NULL)) != -1) {
		switch(c) {
		case 'p':
		case 'r':
			/* Acquire chip and line */
			chip = gpio_open_chip(strtoul(optarg, &end, 10));
			if (chip == NULL) return 1;
			/* gpio_export defaults to input */
			line = gpio_export(chip, strtoul((end+1), NULL, 10));
			if (line == NULL) return 1;

			/* print if --getin, otherwise, just setting dir to in */
			if (c == 'p') {
				printf("%s_%d=%d\n", gpiod_chip_name(chip),
				  gpiod_line_offset(line), gpio_read(line));
			}

			/* Clean up chip and line since we're done now */
			gpio_close_chip(chip);
			chip = NULL;
			line = NULL;
			break;
		case 'e':
			/* Acquire chip and line */
			chip = gpio_open_chip(strtoul(optarg, &end, 10));
			if (chip == NULL) return 1;
			/* gpio_export defaults to input */
			line = gpio_export(chip, strtoul((end+1), NULL, 10));
			if (line == NULL) return 1;

			/* Use direction here since gpio_export defaults to input
			 * and gpio_direction allows us to specify the output
			 * state */
			gpio_direction(line, 2);

			/* Clean up chip and line since we're done now */
			gpio_close_chip(chip);
			chip = NULL;
			line = NULL;
			break;
		case 'l':
		case 'd':
			/* Acquire chip and line */
			chip = gpio_open_chip(strtoul(optarg, &end, 10));
			if (chip == NULL) return 1;
			/* gpio_export defaults to input */
			line = gpio_export(chip, strtoul((end+1), NULL, 10));
			if (line == NULL) return 1;

			/* Use direction here since gpio_export defaults to input
			 * and gpio_direction allows us to specify the output
			 * state */
			gpio_direction(line, 1);

			/* Clean up chip and line since we're done now */
			gpio_close_chip(chip);
			chip = NULL;
			line = NULL;
			break;
#if 0
		case 'w':
			gpio = atoi(optarg);
			gpio_direction(gpio, 0);
			gpio_select(gpio);
			break;
#endif
		case 'h':

		default:
			usage(argv);
		}
	}
	return 0;
}

#endif // CTL
