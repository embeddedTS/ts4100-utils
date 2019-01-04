#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <getopt.h>
#include <asm-generic/termbits.h>
#include <asm-generic/ioctls.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <errno.h>
#include <stdint.h>
#include <linux/types.h>
#include <math.h>

#include "fpga.h"

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
	return strtoull(ptr+3, NULL, 16);
}

void usage(char **argv) {
	fprintf(stderr,
		"Usage: %s [OPTIONS] ...\n"
		"Technologic Systems I2C FPGA Utility\n"
		"\n"
		"  -a, --addr <address>   Sets up the address for a peek/poke\n"
		"  -w, --poke <value>     Writes the value to the specified address\n"
		"  -r, --peek             Reads from the specified address\n"
		"  -o, --out=<IO>         FPGA pin to output signal from specified input\n"
		"  -j, --in=<IO>          FPGA input that will be routed to the output\n"
		"  -i, --info             Print fpga rev and board model id\n"
		"  -h, --help             This message\n"
		"\n",
		argv[0]
	);
}

int main(int argc, char **argv) 
{
	int c;
	uint16_t addr = 0x0;
	int opt_addr = 0;
	int opt_info = 0;
	int opt_poke = 0, opt_peek = 0;
	uint16_t model;
	uint8_t pokeval = 0;
	int opt_input = -1;
	int opt_output = -1;

	static struct option long_options[] = {
		{ "addr", 1, 0, 'a' },
		{ "peek", 0, 0, 'r' },
		{ "poke", 1, 0, 'w' },
		{ "out", 1, 0, 'o' },
		{ "in", 1, 0, 'j' },
		{ "info", 0, 0, 'i' },
		{ "help", 0, 0, 'h' },
		{ 0, 0, 0, 0 }
	};

	twifd = fpga_init();

	if(twifd == -1) {
		perror("Can't open FPGA I2C bus");
		return 1;
	}

	while((c = getopt_long(argc, argv, "a:rw:j:o:i", long_options, NULL)) != -1) {
		switch(c) {
		case 'a':
			opt_addr = 1;
			addr = strtoull(optarg, NULL, 0);
			break;
		case 'w':
			opt_poke = 1;
			pokeval = strtoull(optarg, NULL, 0);
			break;
		case 'j':
			opt_input = strtoull(optarg, NULL, 0);
			break;
		case 'o':
			opt_output = strtoull(optarg, NULL, 0);
			break;
		case 'r':
			opt_peek = 1;
			break;
		case 'i':
			opt_info = 1;
			break;
		default:
			usage(argv);
		}
	}

	if(opt_poke) {
		if(opt_addr) {
			fpoke8(twifd, addr, pokeval);
		} else {
			fprintf(stderr, "No address specified\n");
			return 1;
		}
	}

	if(opt_peek) {
		if(opt_addr) {
			printf("addr%d=0x%X\n", addr, fpeek8(twifd, addr));
		} else {
			fprintf(stderr, "No address specified\n");
			return 1;
		}
	}

	if(opt_input >= 0 && opt_output >= 0) {
		fpoke8(twifd, 128 + opt_output, (uint8_t)opt_input);
	} else if (opt_input >= 0 || opt_output >= 0) {
		fprintf(stderr, "You must specify both input and output\n");
		return 1;
	}

	if(opt_info) {
		uint8_t rev, tmp[3];
		fpeekstream8(twifd, tmp, 304, 3);
		model = tmp[1] | (tmp[0] << 8);
		rev = tmp[2];

		printf("model=0x%X\n", model);
		printf("fpgarev=%d\n", rev);
	}

	close(twifd);

	return 0;
}
