/* SPDX-License-Identifier: BSD-2-Clause */

#include <errno.h>
#include <error.h>
#include <getopt.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "eval_cmdline.h"
#include "fpga.h"

const char copyright[] = "Copyright (c) Technologic Systems - " __DATE__ " - "
  GITCOMMIT;

static int twifd;

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
	return strtoul(ptr+3, NULL, 16);
}

void usage(char **argv) {
	fprintf(stderr,
	  "%s\n\n"
	  "Usage: %s [OPTIONS] ...\n"
	  "Technologic Systems I2C FPGA Utility\n"
	  "\n"
	  "  -a, --address <addr>   Sets up the address for a peek/poke\n"
	  "  -w, --poke <value>     Writes the value to the specified address\n"
	  "  -r, --peek             Reads from the specified address\n"
	  "  -o, --out <I/O>        FPGA pin to output signal from "
	    "specified input\n"
	  "  -j, --in <I/O>         FPGA input that will be routed to "
	    "the output\n"
	  "  -i, --info             Print information about the device\n"
	  "  -h, --help             This message\n"
	  "\n",
	  copyright, argv[0]
	);
}

int main(int argc, char **argv) 
{
	int c;
	uint16_t model;
	uint8_t rev, tmp[3];
	uint16_t addr = 0x0;
	int opt_addr = 0, opt_poke = 0, opt_peek = 0;;
	uint8_t pokeval = 0;
	int opt_info = 0;
	int opt_input = -1, opt_output = -1;

	static struct option long_options[] = {
	  { "address", required_argument, NULL, 'a' },
	  { "peek",    no_argument,       NULL, 'r' },
	  { "poke",    required_argument, NULL, 'w' },
	  { "out",     required_argument, NULL, 'o' },
	  { "in",      required_argument, NULL, 'j' },
	  { "info",    no_argument,       NULL, 'i' },
	  { "help",    no_argument,       NULL, 'h' },
	  { NULL,      no_argument,       NULL,  0  }
	};

	if(argc == 1) {
		usage(argv);
		return 1;
	}

	while((c = getopt_long(argc, argv,
	  "a:rw:cgsqj:o:ih",
	  long_options, NULL)) != -1) {
		switch(c) {
		  case 'a': /* FPGA address */
			opt_addr = 1;
			addr = (uint16_t)strtoul(optarg, NULL, 0);
			break;
		  case 'r': /* FPGA Read */
			opt_peek = 1;
			break;
		  case 'w': /* FPGA Write */
			opt_poke = 1;
			pokeval = (uint8_t)strtoul(optarg, NULL, 0);
			break;
		  case 'j': /* FPGA input to route to -o output */
			opt_input = strtol(optarg, NULL, 0);
			break;
		  case 'o': /* FPGA out to route -j input from */
			opt_output = strtol(optarg, NULL, 0);
			break;
		  case 'i': /* Info */
			opt_info = 1;
			break;
		  case 'h':
		  default:
			usage(argv);
		}
	}

	/* While it would be nice and is possible to check the FPGA for the
	 * model, we need to know if we are on the correct platform to access
	 * the FPGA to get the model. So, this still relies on the /proc method.
	 */
	model = get_model();
	switch(model) {
	  case 0x4100:
		break;
	  default:
		fprintf(stderr, "Unsupported model TS-%X\n", model);
		return 1;
	}

	twifd = fpga_init("/dev/i2c-2", 0x28);

	if(twifd == -1) {
		perror("Can't open FPGA I2C bus");
		return 1;
	}

	if (opt_peek || opt_poke) {
		if (!opt_addr) {
			fprintf(stderr, "Address must be specified\n");
			return 1;
		}

		if (opt_poke) fpoke8(twifd, addr, pokeval);
		if (opt_peek) printf("0x%X\n", fpeek8(twifd, addr));
	}

	if (opt_input >= 0 && opt_output >= 0) {
		fpoke8(twifd, 0x80 + opt_output, (uint8_t)opt_input);
		/* Set the output and input bits. Set the output side to low as
		 * the FPGA inits the registers to be 0 anyway for the output
		 * direction.
		 */
		fpoke8(twifd, opt_output, 0x1);
		fpoke8(twifd, opt_input, 0x0);
		printf("0x%X\n", fpeek8(twifd, 0x80 + opt_output));
	} else if (opt_input >= 0 || opt_output >= 0) {
		fprintf(stderr, "Both input and output must be specified\n");
		return 1;
	}

	if(opt_info) {
		eval_cmd_init();
		fpeekstream8(twifd, tmp, 304, 3);
		model = tmp[1] | (tmp[0] << 8);
		rev = tmp[2];

		printf("model=0x%X\n", model);
		printf("fpgarev=%d\n", rev);
		printf("opts=0x%X\n", fpeek8(twifd, 308) & 0x1F);
		printf("bbid=0x%X\n", eval_cmd("bbid"));
		printf("bbrev=0x%X\n", eval_cmd("bbrev"));
	}

	close(twifd);

	return 0;
}
