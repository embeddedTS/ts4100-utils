/*  Copyright 2011, Unpublished Work of Technologic Systems
 *  All Rights Reserved.
 *
 *  THIS WORK IS AN UNPUBLISHED WORK AND CONTAINS CONFIDENTIAL,
 *  PROPRIETARY AND TRADE SECRET INFORMATION OF TECHNOLOGIC SYSTEMS.
 *  ACCESS TO THIS WORK IS RESTRICTED TO (I) TECHNOLOGIC SYSTEMS EMPLOYEES
 *  WHO HAVE A NEED TO KNOW TO PERFORM TASKS WITHIN THE SCOPE OF THEIR
 *  ASSIGNMENTS  AND (II) ENTITIES OTHER THAN TECHNOLOGIC SYSTEMS WHO
 *  HAVE ENTERED INTO  APPROPRIATE LICENSE AGREEMENTS.  NO PART OF THIS
 *  WORK MAY BE USED, PRACTICED, PERFORMED, COPIED, DISTRIBUTED, REVISED,
 *  MODIFIED, TRANSLATED, ABRIDGED, CONDENSED, EXPANDED, COLLECTED,
 *  COMPILED,LINKED,RECAST, TRANSFORMED, ADAPTED IN ANY FORM OR BY ANY
 *  MEANS,MANUAL, MECHANICAL, CHEMICAL, ELECTRICAL, ELECTRONIC, OPTICAL,
 *  BIOLOGICAL, OR OTHERWISE WITHOUT THE PRIOR WRITTEN PERMISSION AND
 *  CONSENT OF TECHNOLOGIC SYSTEMS . ANY USE OR EXPLOITATION OF THIS WORK
 *  WITHOUT THE PRIOR WRITTEN CONSENT OF TECHNOLOGIC SYSTEMS  COULD
 *  SUBJECT THE PERPETRATOR TO CRIMINAL AND CIVIL LIABILITY.
 */
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
	  "  -s, --sample=N          Display N samples per ADC channel in mV\n"
          "  -a, --acquire=N         Send N raw samples per ADC channel to stdout\n"
          "  -r, --rate=SPEED        Sample at SPEED Hz (default 10000)\n"
          "  -m, --mask=MASK         Sample only channels actived in 16 bit MASK\n"
          "  -d, --setdac=N          Set DAC #N (1-4)\n"
          "  -v, --mvolts=VALUE      DAC/PWM voltage in mV (0 to 10000, default 0)\n"
          "  -p, --pwm=N             Put PWM on digital out #N (1-6)\n"
          "  -P, --prescalar=VALUE   PWM freq will be (12207/(2^VALUE)) Hz (0-7)\n"
          "  -1, --hbridge1=ARG      Control H-bridge #1 (0/1/2, default 1)\n"
          "  -2, --hbridge2=ARG      Control H-bridge #2 (0/1/2, default 1)\n"
          "  -c, --counter=N         Read pulse counter for digital input #N (1-14)\n"
	  "  -D, --setdio=LVAL       Set DIO output to LVAL\n"
	  "  -G, --getdio            Get DIO input\n"
#if 0
          "  -R  --read=BYTES        Read BYTES bytes from TS-8820 SRAM to stdout\n"
          "  -W  --write=BYTES       Write BYTES bytes from stdin to TS-8820 SRAM\n"
#endif
	  "  -h, --help              This help\n\n"
          "PWMs 1-6 feed digital outputs; PWMs 7 and 8 feed H-bridges.\n"
          "The --pwm option overrides the DIO setting and makes the pin a PWM.\n"
          "The --pwm=N --mvolts=10000 gives a 100%% duty cycle.\n"
          "To revert back to simple DIO, use --mvolts=-1.\n"
          "H-bridge arguments: 1=run forward; 2=run backward; 0=disable.\n"
          "To \"free-wheel\" an H-bridge, set its PWM to 0%% and leave it enabled.\n"
          "A disabled H-bridge will have high-impedance on both sides.\n\n",
	  copyright, argv[0]
	);
}

int main(int argc, char **argv) {
	int devmem, c, x;
	int model;
        int opt_sample = 0, opt_acquire = 0, opt_setdac = 0;
        int opt_rate = 10000, opt_mask = 0xffff, opt_mvolts = 0;
        int opt_pwm = 0, opt_prescalar = 0, opt_hb = 0, opt_hbarg = 1;
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
          { "read", 1, 0, 'R' },
          { "write", 1, 0, 'W' },
	  { "help", 0, 0, 'h' },
	  { 0, 0, 0, 0 }
	};
	
        twifd = fpga_init();
        model = get_model();
        if(model != 0x4100) {
                fprintf(stderr, "Unsupported model 0x%X\n", model);
                return 1;
        }

        if(twifd == -1) {
                perror("Can't open FPGA I2C bus");
                return 1;
        }

	if (ts8820_init(twifd)) {
			printf("TS-8820 not detected.\n");
			return -1;
	}

	while((c = getopt_long(argc, argv,
	  "c:p:P:1:2:r:v:m:hs:a:d:D:GR:W:", long_options, NULL)) != -1) {
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
                        ts8820_do_set(strtoul(optarg, NULL, 0));
			break;
                case 'G':
                        printf("dio=0x%x\n", ts8820_di_get());
			break;
                case 'c':
                        x = strtoul(optarg, NULL, 0);
                        printf("counter%d=%d\n", x, ts8820_counter(x));
			break;
#if 0
                case 'R':
                        ts8820_sram_read(strtoul(optarg, NULL, 0));
			break;
                case 'W':
                        ts8820_sram_write(strtoul(optarg, NULL, 0));
			break;
#endif
		case 'h':
		default:
			usage(argv);
			return(1);
		}
	}
        if (opt_sample) ts8820_adc_sam(opt_rate, opt_sample);
        if (opt_acquire) ts8820_adc_acq(opt_rate, opt_acquire, opt_mask);
        if (opt_setdac) ts8820_dac_set(opt_setdac, opt_mvolts);
        if (opt_pwm) {
                if (opt_mvolts < 0) ts8820_pwm_disable(opt_pwm);
                else ts8820_pwm_set(opt_pwm, opt_prescalar, 
                                opt_mvolts*0x1000/10000);
        }
        if (opt_hb) {
                if (opt_hbarg) ts8820_hb_set(opt_hb, opt_hbarg - 1);
                else ts8820_hb_disable(opt_hb);
        }
	return 0;
}

