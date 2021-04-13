/* SPDX-License-Identifier: BSD-2-Clause */

#ifndef _GPIOLIB_H_
#define _GPIOLIB_H_

/* 0 = in
 * 1 = out/low
 * 2 = out/high
 *
 * Need to release a line and re-request it, libgpiod doesn't support arbitrary
 * direction changing in kernel 4.9.y
 *
 * Returns 0 on success, -1 on failure
 */
int gpio_direction(struct gpiod_line *line, int dir);

/* Chnages every bulk GPIO line to direction
 * 0 = in
 * 1 = out/low
 * 2 = out/high
 *
 * Need to release a line and re-request it, libgpiod doesn't support arbitrary
 * direction changing in kernel 4.9.y
 *
 * Returns 0 on success, -1 on failure
 */
int gpio_direction_bulk(struct gpiod_line_bulk *bulk, int dir);

/* Opens a chip by number
 * Returns NULL on failure, otherwise pointer to chip
 */
struct gpiod_chip* gpio_open_chip(int chip_num);

/* Closes all associated lines too */
void gpio_close_chip(struct gpiod_chip *chip);

/* Returns NULL on failure, otherwise pointer to line
 * Requires chip initialized from gpio_open_chip
 */
struct gpiod_line* gpio_export(struct gpiod_chip *chip, int line_num);

/* Releases a single line */
void gpio_unexport(struct gpiod_line *line);

/* Returns 0 or 1 on success, -1 on any failure */
int gpio_read(struct gpiod_line *line);

/* Set line to value
 * Returns 0 on success, -1 on any failure */
int gpio_write(struct gpiod_line *line, int val);

/* returns 0 on success, -1 on any failure
 * Requires chip initialized from gpio_chip_open
 * lines is an array of unsigned int's of the line number, max 64 (in gpiod.h)
 * num_lines is the number of lines in an array.
 * Lines do not need to be sequential, can be arbitrary
 */
int gpio_export_bulk(struct gpiod_chip *chip, struct gpiod_line_bulk *bulk,
						unsigned int *lines,
						unsigned int num_lines);

/* Releases a whole bulk line */
void gpio_unexport_bulk(struct gpiod_line_bulk *bulk);

/* Returns 0 on success, -1 on any failure
 * values is an array of value read from the associated bulk lines, values must
 * be enough to hold the num_lines of the bulk!
 */
int gpio_read_bulk(struct gpiod_line_bulk *bulk, int *values);

/* Returns 0 on success, -1 on any failure
 * vales is an array of values to write to the bulk lines in order.
 */
int gpio_write_bulk(struct gpiod_line_bulk *bulk, int *values);

#endif //_GPIOLIB_H_
