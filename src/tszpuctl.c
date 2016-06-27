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
#include <linux/limits.h>
#include <math.h>
#include <sys/time.h>

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
		"Technologic Systems ZPU Utility\n"
		"\n"
		"  -l, --load <path>      Run the specified binary or \".c\" file in the ZPU\n"
		"  -s, --save             Puts ZPU inreset and output entire ZPU RAM to stdout\n"
		"  -d, --dmesg            Output debug log from ZPU\n"
		"  -c, --compile          Output a <filename>.bin in the same path\n"
		"  -i, --info             Print execution status of the ZPU\n"
		"  -r, --reset <1|0>      Reset ZPU (1 off, 0 on)\n"
		"  -h, --help             This message\n"
		"\n",
		argv[0]
	);
}

int zpucompile(char *infile, char *outfile) 
{
	char cmd[8192];
	int ret;

	char tempfile[] = "/tmp/zpu-XXXXXX";
	mkstemp(tempfile);

	sprintf(cmd, "zpu-elf-gcc -abel -Os -Wl,-relax -Wl,-gc-sections %s -o %s", 
		infile, tempfile);
	ret = system(cmd);
	if(ret) return ret;

	sprintf(cmd, "zpu-elf-objcopy -S -O binary %s %s", tempfile, outfile);
	ret = system(cmd);
	unlink(tempfile);
	return ret;
}


int main(int argc, char **argv) 
{
	int c, i, ret;
	int opt_info = 0;
	int opt_reset = 0;
	int opt_dmesg = 0;
	int opt_save = 0;
	char *compile_path = 0;
	char *opt_load = 0;
	int model;

	static struct option long_options[] = {
		{ "save", 0, 0, 's' },
		{ "dmesg", 0, 0, 'd' },
		{ "compile", 1, 0, 'c' },
		{ "info", 0, 0, 'i' },
		{ "reset", 1, 0, 'r' },
		{ "load", 1, 0, 'l' },
		{ "help", 0, 0, 'h' },
		{ 0, 0, 0, 0 }
	};

	if(argc <= 1) {
		usage(argv);
		return 1;
	}

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

	while((c = getopt_long(argc, argv, "sir:l:c:h", long_options, NULL)) != -1) {
		switch(c) {
		case 'i':
			opt_info = 1;
			break;
		case 'r':
			opt_reset = strtoull(optarg, NULL, 0);
			opt_reset++;
			opt_info = 1;
			break;
		case 'd':
			opt_dmesg = 1;
			break;
		case 's':
			opt_save = 1;
			break;
		case 'c':
			compile_path = strdup(optarg);
			break;
		case 'l':
			opt_load = strdup(optarg);
			break;
		default:
			usage(argv);
		}
	}

	if(compile_path)
	{
		char outfile[PATH_MAX];
		memcpy(outfile, compile_path, strlen(compile_path));
		for (i = strlen(outfile); i != 0; --i)
		{
			if(outfile[i] == '.') {
				outfile[i] = 0;
				break;
			} else if (outfile[i] == '/') {
				break;
			}
		}
		sprintf(outfile, "%s.bin", outfile);\

		if(zpucompile(compile_path, outfile))
			return 1;
		printf("outfile=%s\n", outfile);
	}

	if(opt_load) {
		FILE *f;
		ssize_t sz;
		uint8_t zpuram[8192];
		memset(zpuram, 0, 8192);
		uint8_t dat;
		char *binfile;
		char tempfile[] = "/tmp/zpu-bin-XXXXXX";

		/* If it's a .c file, compile it.  Otherwise
		 * we assume it's a compiled file */
		if(strstr(opt_load, ".c")) {
			mkstemp(tempfile);
			zpucompile(opt_load, tempfile);
			binfile = tempfile;
		} else {
			binfile = opt_load;
		}

		f = fopen(binfile, "rb");
		fseek(f, 0, SEEK_END);
		sz = ftell(f);
		if(sz > 8192){
			fprintf(stderr, "Error: File over 8192 bytes (%zu)\n", sz);
			fclose(f);
			unlink(tempfile);
			return 1;
		}
		fseek(f, 0, SEEK_SET);
		printf("Code RAM usage: (%zu/8192)\n", sz);

		fread(zpuram, 1, 8192, f);
		fclose(f);

		// Put ZPU in reset, program, take it out of reset
		dat = 0x3;
		ret = fpoke(twifd, 19, &dat, 1);
		ret |= fpoke(twifd, 8192, zpuram, 4096);
		ret |= fpoke(twifd, 12288, &zpuram[4096], 4096);
		dat = 0;
		ret |= fpoke(twifd, 19, &dat, 1);
		unlink(tempfile);

		if(ret) {
			perror("Failed to access FPGA to program ZPU");
			return 1;
		}
	}

	if(opt_save) {
		uint8_t dat;
		uint8_t zpuram[8192];

		if (isatty(fileno(stdout))) {
			fprintf(stderr, "Refusing to write binary to the terminal.\n");
			fprintf(stderr, "Did you mean \"%s --save | hexdump -C\"?\n", argv[0]);
			close(twifd);
			return 1;
		}
		/* Keep the ZPU in reset while we read memory */
		dat = 0x3;
		ret = fpoke(twifd, 19, &dat, 1);
		ret |= fpeek(twifd, 8192, zpuram, 8192);
		dat = 0;
		ret |= fpoke(twifd, 19, &dat, 1);
		ret |= fwrite(zpuram, 1, 8192, stdout);
		if(ret){
			perror ("FPGA Access failed");
			return 1;
		} 
	}

	if(opt_reset) {
		uint8_t dat;
		if(opt_reset == 1) {
			dat = 0x0;
			ret = fpoke(twifd, 19, &dat, 1);
		} else {
			dat = 0x3;
			ret = fpoke(twifd, 19, &dat, 1);
		}
		if(ret) {
			perror("Failed to reset the ZPU");
			return 1;
		}
	}

	if(opt_info) {
		uint8_t dat[2];
		int ret;

		ret = fpeek(twifd, 18, dat, 2);
		if(ret) {
			perror("Failed to talk to FPGA");
			return 1;
		}

		printf("zpu_in_reset=%d\n", (dat[1] & 0x3) == 0x3 ? 1 : 0);
		printf("zpu_in_break=%d\n", (dat[0] & 0x4) ? 1 : 0);
	}

	#define FIFO_PUT 8192 + 0xcd4
	#define FIFO_ADDR (FIFO_PUT + 1)
	#define FIFO_SIZE 256

	if(opt_dmesg) {
		/*
		uint8_t inbuf[8192];
		struct timeval st, en;
		int total_samples = 0;

		#define SAMPLES 100000
		gettimeofday(&st, NULL);
		do {
			fpeekstream8(twifd, inbuf, FIFO_ADDR, 256);
			total_samples += 256;
		} while (total_samples < SAMPLES);
		gettimeofday(&en, NULL);

		unsigned long long tus = ((en.tv_sec - st.tv_sec) * 1000000) + 
		  (en.tv_usec + st.tv_usec);

		fprintf(stderr, "Read %d samples in %0.3f seconds\n", 
		  total_samples,
		  (float)tus/1000000);*/

		/*
		uint8_t inbuf[8192];
		uint8_t fifo_sz;
		uint8_t get = 0;
		uint8_t put = 0;

		while(1) {
			// Skip reading put after looping back
			// to the beginning of the fifo
			//if(put == get)
				put = fpeek8(twifd, FIFO_PUT);

			if(put != get) {
				int ret, rdsz;

				// If we're about to read past fifo_sz, read up
				// to the end of it in a continuous chunk
				// and read the rest in the next loop
				if(put < get) { 
					rdsz = FIFO_SIZE - get;
					get = 0;
				} else { 
					rdsz = put - get;
					get += rdsz;
				}
				ret = fpeek(twifd, FIFO_ADDR + put, inbuf, rdsz);

				fwrite(inbuf, 1, rdsz, stdout);

				if(ret < 0) {
					return 1;
				} 
			}
		}*/
	}

	close(twifd);

	return 0;
}
