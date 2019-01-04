#include <assert.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <getopt.h>
#include <asm-generic/ioctls.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/select.h>
#include <errno.h>
#include <stdint.h>
#include <linux/types.h>
#include <linux/limits.h>
#include <math.h>
#include <sys/time.h>
#include <termios.h>
#include <signal.h>

#include "fpga.h"
#include "tszpufifo.h"

static int twifd;

static struct termios tios_orig;
static struct sigaction sa;
volatile uint32_t tick;
void alarmsig(int x) {
	tick++;
}

volatile uint32_t term;
void termsig(int x) {
	term = 1;
}

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
	  "  -l, --load <path>  Compile, load, and run the specified \".c\"\n"
	  "                       or binary file in the ZPU\n"
	  "  -s, --save         Reset ZPU and output entire ZPU RAM to stdout\n"
	  "  -x, --connect      Connect stdin/stdout to ZPU\n"
	  "  -c, --compile      Output a <filename>.bin in the same path\n"
	  "  -i, --info         Print execution status of the ZPU\n"
	  "  -r, --reset <1|0>  Reset ZPU (1 off, 0 on)\n"
	  "  -h, --help         This message\n"
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

	sprintf(cmd,
	  "zpu-elf-gcc -abel -Os -Wl,-relax -Wl,-gc-sections %s -o %s",
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
	int c, i;
	int opt_info = 0;
	int opt_reset = 0;
	int opt_connect = 0;
	int opt_save = 0;
	char *compile_path = 0;
	char *opt_load = 0;
	int model;
	uint8_t buf[8192];

	static struct option long_options[] = {
		{ "save", 0, 0, 's' },
		{ "connect", 0, 0, 'x' },
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

	while((c = getopt_long(argc, argv, "xsir:l:c:h",
	  long_options, NULL)) != -1) {
		switch(c) {
		case 'i':
			opt_info = 1;
			break;
		case 'r':
			opt_reset = strtoull(optarg, NULL, 0);
			opt_reset++;
			opt_info = 1;
			break;
		case 'x':
			opt_connect = 1;
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
		char *binfile;
		char tempfile[] = "/tmp/zpu-bin-XXXXXX";

		memset(buf, 0, 8192);
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
			fprintf(stderr,
			  "Error: File over 8192 bytes (%d)\n", sz);
			fclose(f);
			unlink(tempfile);
			return 1;
		}
		fseek(f, 0, SEEK_SET);
		fprintf(stderr, "Code RAM usage: (%d/8192)\n", sz);

		fread(buf, 1, 8192, f);
		fclose(f);

		/* Put ZPU in reset, program, take it out of reset */
		fpoke8(twifd, 19, 0x3);

		/* 4094 is the max size so pokes must be broken up */
		fpokestream8(twifd, buf, 8192, 4094);
		fpokestream8(twifd, &buf[4094], 12286, 4094);
		fpokestream8(twifd, &buf[8188], 16380, 4);
		fpoke8(twifd, 19, 0x0);
		unlink(tempfile);
	}

	if(opt_save) {
		uint8_t reset_state;

		if (isatty(fileno(stdout))) {
			fprintf(stderr,
			  "Refusing to write binary to the terminal.\n");
			fprintf(stderr,
			  "Did you mean \"%s --save | hexdump -C\"?\n",argv[0]);
			close(twifd);
			return 1;
		}
		/* Need to save the ZPU state, and put it in reset while we read
		 * memory. Make sure to restore state after rather than just
		 * un-resetting blindly.
		 */
		reset_state = fpeek8(twifd, 19);
		fpoke8(twifd, 19, 0x3);
		int ret = fpeekstream8(twifd, buf, 8192, 8192);

		fpoke8(twifd, 19, reset_state);
		fwrite(buf, 1, 8192, stdout);
		if(ret != 0) return 1;
	}

	if(opt_reset) {
		if(opt_reset == 1) {
			fpoke8(twifd, 19, 0x0);
		} else {
			fpoke8(twifd, 19, 0x3);
		}
	}

	if(opt_info) {
		uint8_t rst = fpeek8(twifd, 19);
		uint8_t brk = fpeek8(twifd, 18);

		printf("zpu_in_reset=%d\n", (rst & 0x3) == 0x3 ? 1 : 0);
		printf("zpu_in_break=%d\n", (brk & 0x4) ? 1 : 0);
	}

	if(opt_connect) {
		int irqfd;
		ssize_t r;
		size_t wrsz;
		int rdsz;
		fd_set rfds, efds;

		irqfd = zpu_fifo_init(twifd, 1);
		if (irqfd == -1) {
			fprintf(stderr, "Unable to communicate with ZPU!\n");
			return 1;
		}

		/* Catch any signals that might be received.
		 * Later used to gracefully shutdown the FIFO pipe
		 */
		sa.sa_handler = termsig;
		sigemptyset(&sa.sa_mask);
		sa.sa_flags = 0;
		sigaction(SIGHUP, &sa, NULL);
		sigaction(SIGINT, &sa, NULL);
		sigaction(SIGTERM, &sa, NULL);
		sigaction(SIGABRT, &sa, NULL);
		sigaction(SIGUSR1, &sa, NULL);
		sigaction(SIGUSR2, &sa, NULL);

		/* Set up stdin if it is a terminal, to be nonblocking, raw,
		 * with signal generation.
		 */
		fcntl(STDIN_FILENO, F_SETFL,
		  fcntl(STDIN_FILENO, F_GETFL) | O_NONBLOCK);
		if (isatty(STDIN_FILENO)) {
			struct termios tios;
			tcgetattr(STDIN_FILENO, &tios);
			tios_orig = tios;
			cfmakeraw(&tios);
			tios.c_lflag |= ISIG;
			tcsetattr(STDIN_FILENO, TCSANOW, &tios);
		}

		/* Set stdout to be unbuffered */
		setvbuf(stdout, NULL, _IONBF, 0);

		FD_ZERO(&rfds);
		FD_ZERO(&efds);

		while(1) {
			/* When there is an interrupt from the ZPU, read the
			 * current FIFO tail; this clears the IRQ from FPGA. */
			if (FD_ISSET(irqfd, &efds)) {
				do {
					rdsz = zpu_fifo_get(twifd, buf, 256);
					fwrite(buf, 1, rdsz, stdout);
				} while (rdsz && !term);
			}

			/* Read data from stdin and write it out to the FIFO.
			 * Since it is possible to not do a full write, need to
			 * loop until all data is written out.
			 */
			if (FD_ISSET(0, &rfds)) {
				wrsz = 0;
				r = read(0, buf, 16);
				if (r > 0) {
					do {
						wrsz = wrsz + zpu_fifo_put(twifd,
						  buf + wrsz, r - wrsz);
					} while (r != wrsz);
				} else if (r == 0) { /* EOF */
					zpu_fifo_deinit(twifd);
					break;
				}
			}

			/* This process recevied a signal of some kind */
			if (term) {
				zpu_fifo_deinit(twifd);
				break;
			}

			/* Prioritize IRQs from ZPU.
			 *
			 * Above, the GPIO is set up with rising edge polarity.
			 * When this is set, select() is used on the GPIO value
			 * file to indicate when a change has happened.
			 *
			 * At this point, the interrupt should have been cleared
			 * and if it is not, that means there is another IRQ
			 * pending. The value file is read, if it is a 1, then
			 * it means another interrupt is already ready. In order
			 * to ensure data gets in and out quickly, continue the
			 * while loop to get that data read out.
			 *
			 * If the value read is a 0, then there is no other IRQ
			 * pending, the FDs are reset, and select() is eval'ed
			 * again.
			 */
			if (FD_ISSET(irqfd, &efds)) {
				char x = '?';
				lseek(irqfd, 0, 0);
				read(irqfd, &x, 1);
				assert (x == '0' || x == '1');
				if (x == '1') continue;	
			} else {
				FD_SET(irqfd, &efds);
			}

			FD_SET(0, &rfds);

			i = select(irqfd + 1, &rfds, NULL, &efds, NULL);
			if (i == -1) {
				FD_CLR(0, &rfds);
				FD_CLR(irqfd, &efds);
			} 
		}

		/* Upon exit of this main loop, restore term settings if we were
		 * using a TTY.
		 */
		if (isatty(0)) {
			tcsetattr(0, TCSANOW, &tios_orig);
		}
	}

	close(twifd);

	return 0;
}
