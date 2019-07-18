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

/* Enum for Hbridge settings */
enum hb_state {
	invalid = -1,
	disable = 0,
	brake,
	fwd,
	rev
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
	  "  -P, --prescaler=<val>  PWM freq will be (12207/(2^val)) Hz (0-7)\n"
	  "  -R, --read             Read 16-bit register at <addr>\n"
	  "  -W, --write=<val>      Write 16-bit <val> to register at <addr>\n"
	  "  -A, --address=<addr>   TS-8820 FPGA address to read or write\n"
	  "  -h, --help             This help\n\n"

	  "H Bridge Control:\n"
	  "  -1, --hbridge1         Control H Bridge #1 with following flags\n"
	  "  -2, --hbridge2         Control H Bridge #2 with following flags\n"
	  "  -I, --disable          Disable selected H Bridge (same as coast)\n"
	  "  -C, --coast            Set selected H Bridge to coast\n"
	  "  -B, --brake            Set selected H Bridge to brake\n"
	  "  -F, --fwd=<duty>       Drive sel. H Bridge fwd with <duty> cycle\n"
	  "  -E, --rev=<duty>       Drive sel. H Bridge rev with <duty> cycle\n"
	  "When moving fwd or rev, <duty> is 0 - 1000, where 1000 is 100%%.\n"
	  "The --prescaler flag controls H Bridge PWM drive frequency. When\n"
	  "unspecified, defaults to 12207 Hz.\n"
	  "Only the last --hbridge* option on the command line will be\n"
	  "affected by the specified control flags\n\n"

	  "DIO control:\n"
	  "  -c, --counter=<in>     Read pulse counter for digital in (1-14)\n"
	  "  -D, --setdio=<val>     Set DIO output to val\n"
	  "  -G, --getdio           Get DIO input\n"

	  "PWMs 1-6 feed digital outputs; PWMs 7 and 8 feed H-bridges.\n"
	  "The --pwm option overrides DIO settings and makes the pin a PWM.\n"
	  "The --pwm=<out> --mvolts=10000 gives a 100%% duty cycle.\n"
	  "To revert an output back to DIO, use --mvolts=-1.\n",
	  copyright, argv[0]
	);
}

int main(int argc, char **argv) {
	int c;
	int model;
	int opt_sample = 0, opt_acquire = 0, opt_setdac = 0;
	int opt_rate = 10000, opt_mask = 0xffff, opt_mvolts = 0;
	int opt_pwm = 0, opt_prescaler = 0;
	/* H Bridge specific */
	int opt_hb = 0, opt_hbset = -1, opt_hbduty = 0;
	int opt_DO = 0, opt_DOarg = 0, opt_DI = 0;
	int opt_counter = 0, opt_counterarg = 0;
	int opt_address = -1, opt_read = 0, opt_write = 0, opt_writearg = 0;
	static struct option long_options[] = {
	  { "sample",	required_argument,	0, 's' },
	  { "acquire",	required_argument,	0, 'a' },
	  { "rate",	required_argument,	0, 'r' },
	  { "mask",	required_argument,	0, 'm' },
	  { "setdac",	required_argument,	0, 'd' },
	  { "mvolts",	required_argument,	0, 'v' },
	  { "pwm",	required_argument,	0, 'p' },
	  { "prescaler",required_argument,	0, 'P' },
	  { "prescalar",required_argument,	0, 'P' },
	  { "hbridge1",	no_argument,		0, '1' },
	  { "hbridge2",	no_argument,		0, '2' },
	  { "disable",	no_argument,		0, 'I' },
	  { "coast",	no_argument,		0, 'C' },
	  { "brake",	no_argument,		0, 'B' },
	  { "fwd",	required_argument,	0, 'F' },
	  { "rev",	required_argument,	0, 'E' },
	  { "counter",	required_argument,	0, 'c' },
	  { "setdio",	required_argument,	0, 'D' },
	  { "getdio",	no_argument,		0, 'G' },
	  { "read",	no_argument,		0, 'R' },
	  { "write",	required_argument,	0, 'W' },
	  { "address",	required_argument,	0, 'A' },
	  { "help",	no_argument,		0, 'h' },
	  { 0,		0,			0,  0 }
	};

	if (argc == 1) {
		usage(argv);
		return 1;
	}

	while((c = getopt_long(argc, argv,
	  "c:p:P:12ICBF:E:r:v:m:hs:a:d:D:GRW:A:", long_options, NULL)) != -1) {
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
			opt_prescaler = strtoul(optarg, NULL, 0);
			break;
		  case '1':
			opt_hb = 1;
			break;
		  case '2':
			opt_hb = 2;
			break;
		  case 'I':
		  case 'C':
			opt_hbset = disable;
			break;
		  case 'B':
			opt_hbset = brake;
			break;
		  case 'F':
			opt_hbset = fwd;
			opt_hbduty = strtoul(optarg, NULL, 0);
			if (opt_hbduty < 0) opt_hbduty = 0;
			if (opt_hbduty > 1000) opt_hbduty = 1000;
			break;
		  case 'E':
			opt_hbset = rev;
			opt_hbduty = strtoul(optarg, NULL, 0);
			if (opt_hbduty < 0) opt_hbduty = 0;
			if (opt_hbduty > 1000) opt_hbduty = 1000;
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
		if (opt_pwm > 0 && opt_pwm <= 6) {
			if (opt_mvolts < 0) {
				ts8820_pwm_disable(opt_pwm);
			} else {
				ts8820_pwm_set(opt_pwm, opt_prescaler,
				  opt_mvolts*0x1000/10000);
			}
		}
	}

	/* Top of file has an enum of H Bridge states that only this ctl app
	 * cares about. Therefore setting fwd and rev from here still uses magic
	 * numbers to the ts8820 API. The goal is to cause as little disruption
	 * as possible for the API, but give the ctl a much cleaner and easier
	 * to understand interface.
	 */
	if (opt_hb) {
		switch(opt_hbset) {
		  case disable:
			/* Disable and then set PWM output to 0. Not necessary
			 *   as "disable" by itself will tri-state the bridge
			 *   outputs, but it's here for clarity sake.
			 * H Bridge PWM channels are 7 and 8.
			 */
			ts8820_hb_disable(opt_hb);
			ts8820_pwm_set((opt_hb + 6), opt_prescaler, 0);
			break;
		  case brake:
			/* Both sides of the motor will by default set to
			 *   GND when braking due to setting the PWM output to
			 *   0% duty cycle
			 * Still set the output direction to a known value,
			 *   doing so un-disables the H Bridge to allow braking
			 *   from a coast/disabled state.
			 * Set direction after setting PWM to prevent reversing
			 *   direction on accident and straining the system.
			 */
			ts8820_pwm_set((opt_hb + 6), opt_prescaler, 0);
			ts8820_hb_set(opt_hb, 0);
			break;
		  case fwd:
			ts8820_hb_set(opt_hb, 0);
			ts8820_pwm_set((opt_hb + 6), opt_prescaler,
			  (opt_hbduty*0x1000/1000));
			break;
		  case rev:
			ts8820_hb_set(opt_hb, 1);
			ts8820_pwm_set((opt_hb + 6), opt_prescaler,
			  (opt_hbduty*0x1000/1000));
			break;
		  case invalid:
		  default:
			break;
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
