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

static int twifd;

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
		struct termios tios_orig;
 		struct sigaction sa;
		int irqfd;
		fd_set rfds, efds;
		uint32_t fifo_adr;
		uint32_t fifo_flags;
		/* RX and TX naming is from the ZPU's point of view */
		uint16_t txfifo_sz, txfifo_put_adr;
		uint16_t txfifo_dat_adr, txfifo_get_adr;
		uint16_t rxfifo_sz, rxfifo_put_adr;
		uint16_t rxfifo_dat_adr, rxfifo_get_adr;
		uint8_t txget, rxget, rxfifo_spc;
		uint8_t txput = 0, rxput = 0;
		const char *irq_preamble_cmd =
		  "(echo 129 >/sys/class/gpio/export;"
		  "echo in >/sys/class/gpio/gpio129/direction;"
		  "echo rising >/sys/class/gpio/gpio129/edge) 2>/dev/null";

		system(irq_preamble_cmd);

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

		/*
		 * Set up FIFO link addresses
		 */

		/* The ZPU stores the FIFO struct start address at 0x203C in
		 * FPGA I2C address map. However from the ZPU context it is at
		 * 0x3C. Acquire the struct address, byteswap, check it, put it
		 * in FPGA I2C address context.
		 */
		fpeekstream8(twifd, (unsigned char *)&fifo_adr, 8192 + 0x3c, 4);
		fifo_adr = ntohl(fifo_adr);
		if (fifo_adr == 0 || fifo_adr >= 8192) {
			fprintf(stderr, "ZPU connection refused\n");
			close(twifd);
			return 1;
		}
		fifo_adr += 8192;

		/* Now that we have the start of the FIFO struct in the ZPU,
		 * start getting flags and other data addresses from it.
		 * ZPU FIFO struct looks like:
		 *
		 * static struct zpu_fifo {
		 *   uint32_t flags;				// sizes, opt
		 *   uint32_t txput;				// TX FIFO head
		 *   volatile uint32_t txget;			// TX FIFO tail
		 *   uint8_t txdat[ZPU_TXFIFO_SIZE];		// TX buffer
		 *   volatile uint32_t rxput;			// RX FIFO head
		 *   uint32_t rxget;				// RX FIFO tail
		 *   volatile uint8_t rxdat[ZPU_RXFIFO_SIZE];	// RX buffer
		 * } fifo;
		 */
		fpeekstream8(twifd, (unsigned char *)&fifo_flags, fifo_adr, 4);
		fifo_flags = ntohl(fifo_flags);
		fifo_flags &= ~(1<<25); /* Enable TX fifo flow control */
		fpoke8(twifd, fifo_adr, fifo_flags >> 24);

		txfifo_sz = fifo_flags & 0xfff;
		assert(txfifo_sz <= 256);
		txfifo_put_adr = fifo_adr + 7;
		txfifo_get_adr = txfifo_put_adr + 4;
		txfifo_dat_adr = fifo_adr + 12;

		rxfifo_sz = (fifo_flags >> 12) & 0xfff;
		assert(rxfifo_sz <= 256);
		rxfifo_put_adr = txfifo_dat_adr + txfifo_sz + 3;
		rxfifo_get_adr = rxfifo_put_adr + 4;
		rxfifo_dat_adr = rxfifo_get_adr + 1;
		if (rxfifo_sz) {
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
		}

		/* Get current RX FIFO position.
		 * Zero out TX FIFO by setting tail to head.
		 */
		rxput = fpeek8(twifd, rxfifo_put_adr);
		txget = txput = fpeek8(twifd, txfifo_put_adr);
		fpoke8(twifd, txfifo_get_adr, txget);
		rxfifo_spc = 0;


		/* Set stdout to be unbuffered */
		setvbuf(stdout, NULL, _IONBF, 0);

		/* ZPU drives the FPGA IRQ line. */
		irqfd = open("/sys/class/gpio/gpio129/value", O_RDONLY);
		assert(irqfd != -1);

		FD_ZERO(&rfds);
		FD_ZERO(&efds);

		while(1) {
			/* When there is an interrupt from the ZPU, read the
			 * current FIFO tail; this clears the IRQ from FPGA. */
			if (FD_ISSET(irqfd, &efds)) {
				txput = fpeek8(twifd, txfifo_put_adr);
			}

			/* ZPU sending data to host
			 *
			 * If head pos. is behind the tail pos., then host will
			 * pull data out through the end of the FIFO in one
			 * contiguous chunk. rdsz0 is used in this case as an
			 * offset for later calculating the full count.
			 *
			 * Host pull out data from the tail through the current
			 * head.
			 *
			 * Update ZPU RAM and our local var with new tail pos.
			 */
			if (txput != txget) {
				int rdsz, rdsz0;

				if (txput < txget) { 
					rdsz0 = txfifo_sz - txget;
					fpeekstream8(twifd, buf,
					  txfifo_dat_adr + txget, rdsz0);
					txget = 0;
				} else rdsz0 = 0;

				rdsz = txput - txget;
				if (rdsz) {
					fpeekstream8(twifd, buf + rdsz0,
					  txfifo_dat_adr + txget, rdsz);
					txget = txput;
				}

				rdsz += rdsz0;
				fpoke8(twifd, txfifo_get_adr, txget); 
				fwrite(buf, 1, rdsz, stdout);
			}

			/* ZPU recv. data from host
			 *
			 * If the RX buffer has free space, and there is data
			 * waiting to be written (from this application's stdin)
			 * then write to RX buffer.
			 *
			 * Send data from host to ZPU RX buffer, up to the max
			 * amount of available free space. Then adjust free
			 * space counter and update FIFO head in ZPU.
			 *
			 * If an EOF is received from stdin, disable ZPU TX FIFO
			 * flow control and break from the main loop.
			 */
			if (rxfifo_spc && FD_ISSET(0, &rfds)) {
				ssize_t r;

				r = read(0, buf, rxfifo_spc);

				if (r > 0) {
					/* XXX: This should use stream i2c
					 * writes, not byte writes. -JO */
					for(i = 0; i < r; i++) {
						fpoke8(twifd,
						  rxfifo_dat_adr + rxput,
						  buf[i]); 
						rxput++;
						if (rxput == rxfifo_sz) {
							rxput = 0;
						}
					}
					rxfifo_spc -= r;
					fpoke8(twifd, rxfifo_put_adr, rxput);
				} else if (r == 0) { /* EOF */
					/* Disable TX fifo flow control */
					fifo_flags |= (1<<25);
					fpoke8(twifd, fifo_adr,
					  (fifo_flags >> 24));
					break;
				}
			}

			/* Recalculate the ZPU RX buffer free space */
			if (rxfifo_spc != (rxfifo_sz - 1)) {
				rxget = fpeek8(twifd, rxfifo_get_adr);
				if (rxget <= rxput) {
					rxfifo_spc =
					  rxfifo_sz - (rxput - rxget) - 1;
				} else {
					rxfifo_spc =
					  rxfifo_sz -
					  (rxput + (rxfifo_sz - rxget)) - 1;
				}
			}

			/* This process recevied a signal of some kind */
			if (term) {
				/* Disable TX fifo flow control */
				fifo_flags |= (1<<25);
				fpoke8(twifd, fifo_adr, fifo_flags >> 24);
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

			if (rxfifo_spc) FD_SET(0, &rfds);
			else FD_CLR(0, &rfds);

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
