/* SPDX-License-Identifier: BSD-2-Clause */

#include <stdio.h>
#include <stdint.h>
#include <assert.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>

#include "gpiolib.h"
#include "ispvm.h"

#define TS4100_JTAG_TMS 134
#define TS4100_JTAG_TCK 135
#define TS4100_JTAG_TDO 132
#define TS4100_JTAG_TDI 133
#define TS4100_FPGA_RESETN 107

void reset_ts4100(void)
{
	assert(gpio_export(TS4100_FPGA_RESETN) == 0);

	gpio_direction(TS4100_FPGA_RESETN, 1);
	/* Give 10ms of reset */
	usleep(1000*10);
	gpio_direction(TS4100_FPGA_RESETN, 2);
	gpio_unexport(TS4100_FPGA_RESETN);
}

void init_ts4100(void)
{
	assert(gpio_export(TS4100_JTAG_TMS) == 0);
	assert(gpio_export(TS4100_JTAG_TCK) == 0);
	assert(gpio_export(TS4100_JTAG_TDO) == 0);
	assert(gpio_export(TS4100_JTAG_TDI) == 0);

	tmsfd = gpio_getfd(TS4100_JTAG_TMS);
	tckfd = gpio_getfd(TS4100_JTAG_TCK);
	tdofd = gpio_getfd(TS4100_JTAG_TDO);
	tdifd = gpio_getfd(TS4100_JTAG_TDI);

	assert(tmsfd >= 0);
	assert(tckfd >= 0);
	assert(tdofd >= 0);
	assert(tdifd >= 0);

	gpio_direction(TS4100_JTAG_TCK, 2);
	gpio_direction(TS4100_JTAG_TDI, 2);
	gpio_direction(TS4100_JTAG_TMS, 2);
	gpio_direction(TS4100_JTAG_TDO, 0);
}

void restore_ts4100(void)
{
	gpio_unexport(TS4100_JTAG_TMS);
	gpio_unexport(TS4100_JTAG_TDI);
	gpio_unexport(TS4100_JTAG_TCK);
	gpio_unexport(TS4100_JTAG_TDO);
}

int readport_ts4100(void)
{
	//return gpio_read(TS4100_JTAG_TDO);
	char in;
	read(tdofd, &in, 1);
	lseek(tdofd, 0, SEEK_SET);
	if(in == '1') return 1;
	return 0;
}

void writeport_ts4100(int pins, int val)
{
	uint8_t *buf;
	if(val) buf = "1";
	else buf = "0";

	switch (pins) {
	case g_ucPinTDI:
		write(tdifd, buf, 1);
		break;
	case g_ucPinTCK:
		write(tckfd, buf, 1);
		break;
	case g_ucPinTMS:
		write(tmsfd, buf, 1);
		break;
	default:
		printf("%s: requested unknown pin\n", __func__);
	}
}

void sclock_ts4100()
{
	write(tckfd, "1", 1);
	write(tckfd, "0", 1);
}
