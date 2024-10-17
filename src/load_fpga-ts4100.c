/* SPDX-License-Identifier: BSD-2-Clause */

#include <stdio.h>
#include <stdint.h>
#include <assert.h>
#include <gpiod.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>

#include "ispvm.h"

static struct gpiod_chip *chip4;
static struct gpiod_chip *chip3;
static struct gpiod_line *jtag_tms;
static struct gpiod_line *jtag_tck;
static struct gpiod_line *jtag_tdo;
static struct gpiod_line *jtag_tdi;
static struct gpiod_line *fpga_resetn;

#define TS4100_JTAG_TMS_LINE	6 // Chip 4
#define TS4100_JTAG_TCK_LINE	7 // Chip 4
#define TS4100_JTAG_TDO_LINE	4 // Chip 4
#define TS4100_JTAG_TDI_LINE	5 // Chip 4
#define TS4100_FPGA_RESETN_LINE 11 // Chip 3

void reset_ts4100(void)
{
	gpiod_line_set_value(fpga_resetn, 0);
	/* Give 10ms of reset */
	usleep(1000*10);
	gpiod_line_set_value(fpga_resetn, 1);
}

void init_ts4100(void)
{
	chip3 = gpiod_chip_open_by_number(3);
	chip4 = gpiod_chip_open_by_number(4);

	assert(chip3 != NULL);
	assert(chip4 != NULL);

	jtag_tms = gpiod_chip_get_line(chip4, TS4100_JTAG_TMS_LINE);
	jtag_tck = gpiod_chip_get_line(chip4, TS4100_JTAG_TCK_LINE);
	jtag_tdo = gpiod_chip_get_line(chip4, TS4100_JTAG_TDO_LINE);
	jtag_tdi = gpiod_chip_get_line(chip4, TS4100_JTAG_TDI_LINE);
	fpga_resetn = gpiod_chip_get_line(chip3, TS4100_FPGA_RESETN_LINE);

	assert(jtag_tms != NULL);
	assert(jtag_tck != NULL);
	assert(jtag_tdo != NULL);
	assert(jtag_tdi != NULL);

	assert(gpiod_line_request_output(jtag_tms, "load_fpga", 1) == 0);
	assert(gpiod_line_request_output(jtag_tck, "load_fpga", 1) == 0);
	assert(gpiod_line_request_output(jtag_tdi, "load_fpga", 1) == 0);
	assert(gpiod_line_request_output(fpga_resetn, "load_fpga", 1) == 0);
	assert(gpiod_line_request_input(jtag_tdo, "load_fpga") == 0);
}

void restore_ts4100(void)
{
	gpiod_line_release(jtag_tms);
	gpiod_line_release(jtag_tdi);
	gpiod_line_release(jtag_tck);
	gpiod_line_release(jtag_tdo);
	gpiod_line_release(fpga_resetn);

	gpiod_chip_close(chip3);
	gpiod_chip_close(chip4);
}

int readport_ts4100(void)
{
	return gpiod_line_get_value(jtag_tdo);
}

void writeport_ts4100(int pins, int val)
{
	switch (pins) {
	case g_ucPinTDI:
		gpiod_line_set_value(jtag_tdi, val);
		break;
	case g_ucPinTCK:
		gpiod_line_set_value(jtag_tck, val);
		break;
	case g_ucPinTMS:
		gpiod_line_set_value(jtag_tms, val);
		break;
	default:
		printf("%s: requested unknown pin\n", __func__);
	}
}

void sclock_ts4100()
{
	writeport_ts4100(g_ucPinTCK, 1);
	writeport_ts4100(g_ucPinTCK, 0);
}
