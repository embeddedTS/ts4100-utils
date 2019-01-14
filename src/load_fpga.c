#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <unistd.h>
#include <sys/time.h>
#include <sys/reboot.h>

#include "ispvm.h"
#include "load_fpga-ts4100.h"

const char copyright[] = "Copyright (c) Technologic Systems - " __DATE__ " - "
  GITCOMMIT;

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

void udelay_imx6(unsigned int us)
{
	struct timeval start;
	struct timeval now;
	uint32_t elapsed = 0;

	gettimeofday(&start, NULL);
	do {
		gettimeofday(&now, NULL);
		elapsed = (now.tv_usec - start.tv_usec);
	} while(elapsed < us);
}

int main(int argc, char **argv)
{
	int x;
	char *model = 0;
	struct ispvm_f hardware;

	const char * ispvmerr[] = { "pass", "verification fail",
	  "can't find the file", "wrong file type", "file error",
	  "option error", "crc verification error" };

	if(argc != 2 && argc != 3) {
		printf("%s\n\n", copyright);
		printf("Usage: %s file.vme <reboot>\n", argv[0]);
		printf("\tif reboot is specified it will reset the cpu after\n");
		return 1;
	}

	model = get_model();
	if(strstr(model, "4100")) {
		hardware.init = init_ts4100;
		hardware.restore =restore_ts4100;
		hardware.readport = readport_ts4100;
		hardware.writeport = writeport_ts4100;
		hardware.sclock = sclock_ts4100;
		hardware.udelay = udelay_imx6;
	} else {
		printf("Model \"%s\" not supported\n", model);
		return 1;
	}

	x = ispVM(&hardware, argv[1]);

	if (x == 0) {
		reset_ts4100();

		printf("loadfpga_ok=1\n");
	} else {
		assert(x < 0);
		printf("loadfpga_ok=0\n");
		printf("loadfpga_error=\"%s\"\n", ispvmerr[-x]);
	}

	if(argc == 3) {
		printf("rebooting...\n");
		fflush(stdout);
		/* Allow time for this to print */
		sleep(1);
		reboot(RB_AUTOBOOT);
	}

	return 0;
}
