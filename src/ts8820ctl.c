/* SPDX-License-Identifier: BSD-2-Clause */

/* Based on ts8820ctl.c for the TS-4700 */

#include <assert.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <getopt.h>
#include <stdint.h>
#include <string.h>

#include "ts8820.h"
#include "fpga.h"

static int twifd;

const char copyright[] = "Copyright (c) Technologic Systems - " __DATE__ " - "
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
	  "Usage: %s [OPTION] ...\n"
	  "Technologic Systems TS-8820 FPGA manipulation\n"
	  "\n"
	  "General options:\n"
	  "  -s, --sample=<num>     Display num samples per ADC channel in mV\n"
	  "  -a, --acquire=<num>    Send num raw samples per ADC channel to "\
	    "stdout\n"
	  "  -r, --rate=<speed>     Sample at <speed> Hz (default 10000)\n"
	  "  -m, --mask=<mask>      Sample only channels set in 16-bit <mask>\n"
	  "  -d, --setdac=<chan>    Set DAC channel (1-4)\n"
	  "  -v, --mvolts=<mvolts>  DAC/PWM voltage in mV (0-10000)\n"
	  "  -p, --pwm=<out>        Put PWM on digital out (1-6)\n"
	  "  -P, --prescalar=<val>  PWM freq will be (12207/(2^val)) Hz (0-7)\n"
	  "  -1, --hbridge1=<val>   Control H-bridge #1 (0/1/2, default 1)\n"
	  "  -2, --hbridge2=<val>   Control H-bridge #2 (0/1/2, default 1)\n"
	  "  -c, --counter=<in>     Read pulse counter for digital in (1-14)\n"
	  "  -D, --setdio=<val>     Set DIO output to val\n"
	  "  -G, --getdio           Get DIO input\n"
	  "  -R, --read             Read 16-bit register at <addr>\n"
	  "  -W, --write=<val>      Write 16-bit <val> to register at <addr>\n"
	  "  -A, --address=<addr>   TS-8820 FPGA address to read or write\n"
	  "  -h, --help             This help\n\n"
	  "PWMs 1-6 feed digital outputs; PWMs 7 and 8 feed H-bridges.\n"
	  "The --pwm option overrides DIO settings and makes the pin a PWM.\n"
	  "The --pwm=<out> --mvolts=10000 gives a 100%% duty cycle.\n"
	  "To revert an output back to DIO, use --mvolts=-1.\n"
	  "H-bridge arguments: 1=run forward; 2=run backward; 0=disable.\n"
	  "To \"free-wheel\" an H-bridge, set its PWM to 0%% and leave it "
	    "enabled.\n"
	  "A disabled H-bridge will have high-impedance on both sides.\n\n",
	  copyright, argv[0]
	);
}

int main(int argc, char **argv) {
	int c;
	int model;
	int opt_sample = 0, opt_acquire = 0, opt_setdac = 0;
	int opt_rate = 10000, opt_mask = 0xffff, opt_mvolts = 0;
	int opt_pwm = 0, opt_prescalar = 0, opt_hb = 0, opt_hbarg = 1;
	int opt_DO = 0, opt_DOarg = 0, opt_DI = 0;
	int opt_counter = 0, opt_counterarg = 0;
	int opt_address = -1, opt_read = 0, opt_write = 0, opt_writearg = 0;
	static struct option long_options[] = {
	  { "sample", 1, 0, 's' },
	  { "acquire", 1, 0, 'a' },
	  { "rate", 1, 0, 'r' },
	  { "mask", 1, 0, 'm' },
	  { "setdac", 1, 0, 'd' },
	  { "mvolts", 1, 0, 'v' },
	  { "pwm", 1, 0, 'p' },
	  { "prescalar", 1, 0, 'P' },
	  { "hbridge1", 1, 0, '1' },
	  { "hbridge2", 1, 0, '2' },
	  { "counter", 1, 0, 'c' },
	  { "setdio", 1, 0, 'D'},
	  { "getdio", 0, 0, 'G'},
	  { "read", 0, 0, 'R' },
	  { "write", 1, 0, 'W' },
	  { "address", 1, 0, 'A' },
	  { "help", 0, 0, 'h' },
	  { 0, 0, 0, 0 }
	};

	if (argc == 1) {
		usage(argv);
		return 1;
	}

	while((c = getopt_long(argc, argv,
	  "c:p:P:1:2:r:v:m:hs:a:d:D:GRW:A:", long_options, NULL)) != -1) {
		switch (c) {
		  case 'r':
			opt_rate = strtoul(optarg, NULL, 0);
			break;
		  case 'v':
			opt_mvolts = strtoul(optarg, NULL, 0);
			break;
		  case 'm':
			opt_mask = strtoul(optarg, NULL, 0);
			break;
		  case 's':
                        opt_sample = strtoul(optarg, NULL, 0);
			break;
		  case 'a':
			opt_acquire = strtoul(optarg, NULL, 0);
			break;
		  case 'd':
			opt_setdac = strtoul(optarg, NULL, 0);
			break;
		  case 'p':
			opt_pwm = strtoul(optarg, NULL, 0);
			break;
		  case 'P':
			opt_prescalar = strtoul(optarg, NULL, 0);
			break;
		  case '1':
			opt_hb = 1;
			opt_hbarg = strtoul(optarg, NULL, 0);
			break;
		  case '2':
			opt_hb = 2;
			opt_hbarg = strtoul(optarg, NULL, 0);
			break;
		  case 'D':
			opt_DO = 1;
			opt_DOarg = strtoul(optarg, NULL, 0);
			break;
		  case 'G':
			opt_DI = 1;
			break;
		  case 'c':
			opt_counter = 1;
			opt_counterarg = strtoul(optarg, NULL, 0);
			break;
		  case 'R':
			opt_read = 1;
			break;
		  case 'W':
			opt_write = 1;
			opt_writearg = strtoul(optarg, NULL, 0);
			break;
		  case 'A':
			opt_address = strtoul(optarg, NULL, 0);
			break;
		  case 'h':
		  default:
			usage(argv);
			return 1;
		}
	}

	model = get_model();
	if(model != 0x4100) {
		fprintf(stderr, "Unsupported model 0x%X\n", model);
		return 1;
	}

	twifd = fpga_init("/dev/i2c-2", 0x28);
	if(twifd == -1) {
		perror("Can't open FPGA I2C bus");
		return 1;
	}

	if (ts8820_init(twifd)) {
		printf("TS-8820 not detected.\n");
		return 1;
	}

	if (opt_DI) printf("dio=0x%x\n", ts8820_di_get());

	if (opt_counter) {
		if (opt_counterarg > 0 && opt_counterarg < 15) {
			printf("counter%d=%d\n", opt_counterarg,
			  ts8820_counter(opt_counterarg));
		}
	}

	if (opt_DO) ts8820_do_set(opt_DOarg);

	if (opt_sample) ts8820_adc_sam(opt_rate, opt_sample);

	if (opt_acquire) ts8820_adc_acq(opt_rate, opt_acquire, opt_mask);

	if (opt_setdac) {
		if ((opt_setdac > 0 && opt_setdac <= 4) && \
		    (opt_mvolts > 0 && opt_mvolts <= 10000)) {
			ts8820_dac_set(opt_setdac, opt_mvolts);
		}
	}

	if (opt_pwm) {
		if (opt_pwm > 0 && opt_pwm <= 8) {
			if (opt_mvolts < 0) {
				ts8820_pwm_disable(opt_pwm);
			} else {
				ts8820_pwm_set(opt_pwm, opt_prescalar,
				  opt_mvolts*0x1000/10000);
			}
		}
	}

	if (opt_hb) {
		if (opt_hbarg > 0) {
			ts8820_hb_set(opt_hb, opt_hbarg - 1);
		} else {
			ts8820_hb_disable(opt_hb);
		}
	}

	if (opt_read) {
		if (opt_address >= 0 && opt_address <= 0xA6) {
			printf("0x%X\n", ts8820_read(opt_address));
		}
	}

	if (opt_write) {
		if (opt_address >= 0 && opt_address <= 0xA6) {
			ts8820_write(opt_address, opt_writearg);
		}
	}

	return 0;
}

