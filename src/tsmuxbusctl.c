/* SPDX-License-Identifier: BSD-2-Clause */

/* Example application for communicating with MUXBUS based baseboards via
 * TS-4100 ZPU MUXBUS FIFO implementation.
 */

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tszpufifo.h"
#include "fpga.h"

const char copyright[] = "Copyright (c) embeddedTS - " __DATE__ " - "
  GITCOMMIT;

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


static void usage(char **argv) {
	fprintf(stderr,
	  "%s\n\n"
	  "Usage: %s ADDRESS [VALUE]\n"
	  "embeddedTS ZPU MUXBUS demo tool\n\n"

	  "  ADDRESS     The MUXBUS address to read/write 16-bit value\n"
	  "  VALUE       Optional argument, write 16-bit VALUE to ADDRESS\n\n"

	  "Print a 16-bit hex value to terminal indicating the value read from\n"
	  "ADDRESS. On a write, VALUE is written to ADDRESS, and then read\n"
	  "back. The resulting read is printed.\n\n"

	  "Returns 0 on success, 1 on any error.\n\n",
	  copyright, argv[0]
	);
}

int main(int argc, char **argv) {
	int twifd;
	int model;
	uint16_t addr, val;

	if ((argc == 1) || (argc > 3) || !strncmp(argv[1], "-h", 2) ||
	  !strncmp(argv[1], "--help", 6)) {
		usage(argv);
		return 1;
	}

	model = get_model();
	if(model != 0x4100) {
		fprintf(stderr, "Unsupported model 0x%X\n", model);
		return 1;
	}


	twifd = fpga_init("/dev/i2c-2", 0x28);
	if(twifd == -1) {
		/* fpga_init() calls open() which fails with errno set */
		perror("Can't open FPGA I2C bus");
		return 1;
	}

	/* zpu_fifo_init() also returns irqfd, we don't need to worry about that
	 * unless wanted. The irqfd is maintained by tszpufifo ctx */
        if (zpu_fifo_init(twifd, FLOW_CTRL) == NULL) return 1;

	addr = (uint16_t)strtoul(argv[1], NULL, 0);

	/* If VALUE was passed, write that first */
	if (argc == 3) {
		val = (uint16_t)strtoul(argv[2], NULL, 0);
		zpu_muxbus_poke16(twifd, addr, val);
	}

	printf("0x%04X\n", zpu_muxbus_peek16(twifd, addr));

	zpu_fifo_deinit(twifd);
	fpga_deinit(twifd);

	return 0;
}
