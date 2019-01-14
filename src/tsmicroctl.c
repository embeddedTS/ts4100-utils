/* SPDX-License-Identifier: BSD-2-Clause */

#include <stdio.h>
#include <unistd.h>
#include <dirent.h> 
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>

#ifdef CTL
#include <getopt.h>
#endif

#include "i2c-dev.h"

const char copyright[] = "Copyright (c) Technologic Systems - " __DATE__ " - "
  GITCOMMIT;

char *model = 0;

char *get_model()
{
	FILE *proc;
	char mdl[256];
	char *ptr;
	int sz;

	proc = fopen("/proc/device-tree/model", "r");
	if (!proc) {
		perror("model");
		return 0;
	}
	sz = fread(mdl, 256, 1, proc);
	ptr = strstr(mdl, "TS-");
	return strndup(ptr, sz - (mdl - ptr));
}

int silabs_init()
{
	static int fd = -1;
	fd = open("/dev/i2c-0", O_RDWR);
	if(fd != -1) {
		if (ioctl(fd, I2C_SLAVE_FORCE, 0x4a) < 0) {
			perror("FPGA did not ACK 0x4a\n");
			return -1;
		}
	}

	return fd;
}

uint16_t* sread(int twifd, uint16_t *data)
{
	uint8_t tmp[28];
	bzero(tmp, 28);
	int i;

	read(twifd, tmp, 28);
	for (i = 0; i < 14; i++)
		data[i] = (tmp[i*2] << 8) | tmp[(i*2)+1];

	return data;
}

// Scale voltage to silabs 0-2.5V
uint16_t inline sscale(uint16_t data){
	return data * (2.5/1023) * 1000;
}

// Scale voltage for resistor dividers
uint16_t inline rscale(uint16_t data, uint16_t r1, uint16_t r2)
{
	uint16_t ret = (data * (r1 + r2)/r2);
	return sscale(ret);
}

void do_sleep(int twifd, int seconds)
{
	unsigned char dat[4] = {0};
	int opt_sleepmode = 1; // Legacy mode on new boards
	int opt_resetswitchwkup = 1;

	dat[0]=(0x1 | (opt_resetswitchwkup << 1) |
	  ((opt_sleepmode-1) << 4) | 1 << 6);
	dat[3] = (seconds & 0xff);
	dat[2] = ((seconds >> 8) & 0xff);
	dat[1] = ((seconds >> 16) & 0xff);
	write(twifd, &dat, 4);
}

void do_info(int twifd)
{
	uint16_t data[15];
	unsigned int pct;
	memset(data, 0, sizeof(uint16_t));
	sread(twifd, data);

	if(strstr(model, "4100")) {
		/* Byte order is P1.2-P1.4, P2.0-P2.7, temp sensor */
		printf("REVISION=%d\n", ((data[8] >> 8) & 0xF));
		printf("AN_SUP_CAP_1=%d\n", sscale(data[0]));
		printf("AN_SUP_CAP_2=%d\n", rscale(data[1], 20, 20));
		
		pct = ((data[1]*100/237));
		if (pct > 311) {
			pct = pct - 311;
			if (pct > 100) pct = 100;
		} else {
			pct = 0;
		} 
		printf("SUPERCAP_PCT=%d\n", pct > 100 ? 100 : pct);
		printf("AN_MAIN_4P7V=%d\n", rscale(data[2], 20, 20));
		printf("MAIN_5V=%d\n", rscale(data[3], 536, 422));
		printf("USB_OTG_5V=%d\n", rscale(data[4], 536, 422));
		printf("V3P3=%d\n", rscale(data[5], 422, 422));
		printf("RAM_1P35V=%d\n", sscale(data[6]));
		printf("VDD_6UL_CORE=%d\n", sscale(data[9]));
		printf("AN_CHRG=%d\n", rscale(data[10], 422, 422));
		printf("VDD_SOC_CAP=%d\n", sscale(data[11]));
		printf("VDD_ARM_CAP=%d\n", sscale(data[12]));
	}
}

static void usage(char **argv) {
	fprintf(stderr,
	  "%s\n\n"
	  "Usage: %s [OPTION] ...\n"
	  "Technologic Systems Microcontroller Access\n"
	  "\n"	
	  "  -h, --help           This message\n"
	  "  -i, --info           Read all Silabs ADC values\n"
	  "  -s, --sleep <sec>    Enter sleep mode for <sec> seconds\n"
	  "  -e, --tssiloon       Enable charging of TS-SILO supercaps\n"
	  "  -d, --tssilooff      Disable charging of TS-SILO supercaps\n"
	  "    All values are returned in mV\n\n",
	  copyright, argv[0]
	);
}

int main(int argc, char **argv)
{
	int c;
	int twifd, opt_supercap = 0;

	static struct option long_options[] = {
	  { "info", 0, 0, 'i' },
	  { "sleep", 1, 0, 's' },
	  { "tssiloon", 0, 0, 'e'},
	  { "tssilooff", 0, 0, 'd'},
	  { "help", 0, 0, 'h' },
	  { 0, 0, 0, 0 }
	};

	if(argc == 1) {
		usage(argv);
		return(1);
	}

	model = get_model();
	if(!strstr(model, "4100")) {
		fprintf(stderr, "Not supported on model \"%s\"\n", model);
		return 1;
	}

	twifd = silabs_init();
	if(twifd == -1)
		return 1;

	while((c = getopt_long(argc, argv, "edis:h",
	  long_options, NULL)) != -1) {
		switch (c) {
		  case 'i':
			do_info(twifd);
			break;
		  case 's':
			do_sleep(twifd, atoi(optarg));
			break;
		  case 'e':
			opt_supercap = 1;
			break;
		  case 'd':
			opt_supercap = 2;
			break;
		  case 'h':
		  default:
			usage(argv);
		}
	}

	if(opt_supercap) {
		unsigned char dat[1] = {(opt_supercap & 0x1)};
		write(twifd, dat, 1);
	}


	return 0;
}
