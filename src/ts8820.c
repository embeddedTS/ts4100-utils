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

#include <stdio.h>
#include <assert.h>
#include <unistd.h>
#include <stdlib.h>
#include <strings.h>
#include <string.h>
#include <fcntl.h>
#include <stdint.h>
#include "ts8820.h"
#include "tszpufifo.h"

/* ts8820_x functions provide access to functionality on the TS-8820.
 * These are directly portable to any module that has the MUXBUS directly
 * memory mapped.  The TS-4500 would require using sbus.h winpeek/winpoke
 * instead of the peek and poke below.
 *
 * The ADC functions send data directly to stdout.  In many cases the
 * application developer can use them as is and stream the data to a 
 * separate binary of his own design.  For sophisticated high-performance
 * applications, this code may need to be improved or customized.
 */

/* Most TS-8820 features are straightforward, but there are some details to
 * ADC acquisition that the programmer should be aware of.  ADC channels have
 * a hardware numbering and a software numbering.  Hardware numbering refers
 * to the numbering given to the channels on the schematic.  Due to hardware
 * limitations, the TS-8820 does not spit out data in hardware order.  Software
 * numbering is based on the order that data is in when it is read from the
 * TS-8820.  This array converts hardware numbering to software numbering:
 */

int hw2sw[16] = {0, 4, 8, 12, 2, 6, 10, 14, 1, 5, 9, 13, 3, 7, 11, 15};

/* The ts8820_adc_sam() function gives a simple example of usage of this array.
 * Note that to add to the confusion, the numbering on the schematic is 1-16
 * but the indexes to hw2sw are 0-15.
 *
 * For programmers concerned with maximizing sampling bandwidth, it is helpful
 * to understand the design of the TS-8820 ADC system.  There are two ADC
 * chips.  Channels 1-8 (HW numbering) are on chip 1, and channels 9-16 are on
 * chip 2.  Chip 2 can be disabled to save bandwidth, but chip 1 cannot. The
 * FPGA provides a buffer of 4K samples per chip.  The 8 bit channel mask
 * specifying which channels are sampled applies to both chips.  In order to
 * maximize bandwidth, you need to minimize the number of channels activated
 * in the 8 bit channel mask.  As a result, high speed applications should be
 * designed to use mirroring channels on each chip.  Consider the following 
 * function calls, each of which attempt to stream 8 channels at 50kHz for
 * 5 minutes, creating 160MB of data:
 *
 * // This one works:
 * ts8820_adc_acq(50000, 15000000, 0x0f0f);
 *
 * // This one suffers an overflow in about 15 seconds:
 * ts8820_adc_acq(50000, 15000000, 0xf00f);
 *
 * // This one suffers an overflow in about 15 seconds:
 * ts8820_adc_acq(50000, 15000000, 0x00ff);
 *
 * The maximum sampling speed is 100000Hz.  Specifying a higher speed will only
 * result in slightly faster but imperfectly paced sampling and is not
 * recommended.
 */

int g_twifd;

#define peek16(adr) zpu_muxbus_peek16(g_twifd, adr)
#define poke16(adr, val) zpu_muxbus_poke16(g_twifd, adr, val)
#if 0
#define srampeek16(adr) PEEK16((unsigned long)&sram[(adr)/2])
#define srampoke16(adr, val) POKE16((unsigned long)&sram[(adr)/2],(val))
#endif

int ts8820_init(int twifd)
{
	g_twifd = twifd;
	zpu_fifo_init(g_twifd, 1);
        if (0 == (peek16(2) & 0xf)) {
                fprintf(stderr, "Obsolete TS-8820 FPGA version!\n");
                return 1;
        }
        if (0x8820 == peek16(0)) return 0;
        else return 1;
}

int ts8820_adc_acq(int hz, int n, unsigned short mask) {
        unsigned short buf[0x8000];
        unsigned short status, config, *p, *q;
        unsigned int pacing, cycle_in, cycle_out;
        unsigned int m1, m2, m3, m4;
        int written, acquired, chip1, i, cyc, goal;

        config = (mask & 0xff00) | (mask << 8);
        m1 = config;
        chip1 = (mask & 0xff00) ? 1 : 0;
        if (chip1) config |= 0x40;
        m1 = m1 | (m1 >> 8);
        if (!chip1) m1 &= 0xff;
        // m1 is now a mask of all channels that the FPGA will sample
        m2 = 0;
        m3 = 0;
        for (i=0; i<16; i++) {
                if (mask & (1 << i)) m2 |= 1 << hw2sw[i];
                if (m1 & (1 << i)) m3 |= 1 << hw2sw[i];
        }
        // m2 is now a mask of all channels desired in sw order
        // m3 is now a mask of all channels actually sampled in sw order
        cycle_in = 0;
        cycle_out = 0;
        m4 = 0;
        for (i=0; i < 16; i++) {
                if (m3 & (1 << (15 - i))) {
                        cycle_in ++;
                        m4 = m4 << 1;
                        if (m2 & (1 << (15 - i))) {
                                m4 |= 1;
                                cycle_out++;
                        }
                }
        }
        // cycle_in is now the number of channels sampled by the FPGA
        // cycle_out is now the number of channels desired by the user
        // m4 is now a mask of length cycle saying which of those to output
        //fprintf(stderr, "m1=0x%x\n", m1);
        //fprintf(stderr, "m2=0x%x\n", m2);
        //fprintf(stderr, "m3=0x%x\n", m3);
        //fprintf(stderr, "m4=0x%x\n", m4);
        //fprintf(stderr, "cycle_in=%d\n", cycle_in);
        //fprintf(stderr, "cycle_out=%d\n", cycle_out);

        pacing = 100000000/hz;
        poke16(0x82, config | 0x1); // put ADC chips in reset
        poke16(0x8a, pacing >> 16); // pacing clock MSB
        poke16(0x88, pacing & 0xffff); // pacing clock LSB
        poke16(0x82, config); // take ADC chips out of reset
        poke16(0x82, config | 0x2); // start sampling

        written = 0;
        p = buf;
        q = buf;
        acquired = 0;
        cyc = 0;
        goal = n * cycle_out;
        while (written < goal) {
                status = peek16(0x84);
                //fprintf(stderr, "status=0x%x\n", status);
                if (status & 0x8000) {// overflow
                        poke16(0x82, config); // stop sampling
                        goal = acquired;
                }
                status = status & 0x7fff;
                /* priority 0: output, if buffer is getting full */
                /* priority 1: input (100 limit is arbitrary) */
                if ((status > 100) && (acquired - written < 0x3800)
                                   && (goal > acquired)) {
                        if (goal - acquired < status) status = goal - acquired;
                        if (status > 0x800) status = 0x800;
                        for (i = 0; i < status; i++) {
                                *q = peek16(0x86);
                                // test if data is actually desired:
                                if (m4 & (1 << cyc)) {
                                        q++;
                                        if (q == buf + 0x8000) q = buf;
                                        acquired++;
                                }
                                cyc++;
                                if (cyc == cycle_in) cyc = 0;
                        }
                        if (acquired > goal) poke16(0x82, config); // stop
                /* priority 2: output */
                } else if (acquired - written >= 0x800) {
                        fwrite(p, 0x1000, 1, stdout);
                        p += 0x800;
                        if (p == buf + 0x8000) p = buf;
                        written += 0x800;
                } else if (acquired >= goal) {
                        fwrite(p, (goal - written)*2, 1, stdout);
                        written = goal;
                } //else usleep(1);
        }
        fprintf(stderr, "Acquired %d samples.\n", written);

        return written;
}

int ts8820_adc_sam(int hz, int n) {
        unsigned short *results;
        unsigned short status, ready;
        unsigned int pacing, i, j, collected;
        int x;

        results = malloc(n*32);
        assert (results != NULL);
        bzero(results, n*32);

        pacing = 100000000/hz;
        poke16(0x82, 0xff41); // put ADC chips in reset
        poke16(0x8a, pacing >> 16); // pacing clock MSB
        poke16(0x88, pacing & 0xffff); // pacing clock LSB
        poke16(0x82, 0xff40); // take ADC chips out of reset
        usleep(100000); // allow time for ADC chip to come out of reset
        poke16(0x82, 0xff42); // start sampling

        i = 0;
        while (i < n*16) {
                status = peek16(0x84);
                if (status & 0x8000) break;
                ready = status & 0x7fff;
                if (ready >= n*16 - i) ready = n*16 - i;
                for (j=0; j<ready; j++) {
                        results[i] = peek16(0x86);
                        i++;
                }
        }
        poke16(0x82, 0xff40); // stop sampling
        printf("\n");
        if (i != n*16)
                printf("Sampling stopped due to overflow.\n");
        collected = i;
        printf("Collected %d samples total.\n\n", collected);

        for (i=0; i<16; i++) printf("Ch%2d ", i+1);
        printf("\n");
        for (i=0; i<16; i++) printf("---- ");
        printf("\n");
        for (i=0; i<n; i++) {
                for (j=0; j<16; j++) {
                        x = (signed short)results[i*16+hw2sw[j]];
                        x = (x*10000)/0x8000;
                        printf("%4d ", x);
                }
                printf("\n");
        }

        return collected;
}

void ts8820_dac_set(int dac, int mv) {
        
        unsigned int adr;
        unsigned short val;
        adr = 0xa0 + (dac-1)*2;
        val = (unsigned short)((mv*0xfff)/10375) | 0x8000;
        //printf("mv=%d\nadr=0x%x\nval=0x%x\n", mv, adr, val);
        poke16(adr,val);
}

void ts8820_pwm_disable(int n) {
        if (n < 7) poke16(0x8, peek16(0x8) & ~(1 << (n + 5)));
}

void ts8820_pwm_set(int n, int prescalar, int val) {
        poke16(0x10 + 2*(n-1), (prescalar << 13) | (val & 0x1fff));
        if (n < 7) poke16(0x8, peek16(0x8) | (1 << (n + 5)));
}


void ts8820_hb_set(int n, int dir){
        unsigned short x;
        x = peek16(0x2);
        x |= (1 << (n + 5));
        if (!dir) x |= (1 << (n + 3));
        else x &= ~(1 << (n + 3));
        poke16(0x2, x);
}

void ts8820_hb_disable(int n){
        poke16(0x2, peek16(0x2) & ~(1 << (n + 5)));
}

int ts8820_counter(int n) {
        return (int)peek16(0x20 + 2*(n-1));
}

void ts8820_do_set(unsigned int lval) {
        poke16(0x8, lval & 0x3f);
}

unsigned int ts8820_di_get(void) {
        return peek16(0x4) & 0x3fff;
}

#if 0

void ts8820_sram_write_4700(int bytes) {
        	unsigned short x;
	        unsigned int i = 0;
        	poke16(0x4, 0x401);
	        while (i < bytes) {
                	if((i & 0xfff) == 0) poke16(0x6, (i >> 12));
                	x = getchar();
                	i++;
                	if (i < bytes) x |= (unsigned short)getchar() << 8;
                	srampoke16(i & 0xfff, x);
               		i++;
        	}
}

void ts8820_sram_write_4800(int bytes) {
		unsigned short x;
		unsigned int i = 0;
		//volatile unsigned short dummy;
		int windowed = (*syscon == 0x4800);
		//if (windowed) srampoke16(0x2, 0x8800);
		//poke16(0x4, 0x401);
		while (i < bytes) {
			if((i & 0xfff) == 0) poke16(0x6, (i >> 12));
			x = getchar();
			i++;
			if (i < bytes) x |= (unsigned short)getchar() << 8;
			if (windowed) {
				srampoke16(0x0, (((i-1) >> 11) & 1) + 2);
				srampoke16(0x2, 0x8000 | ((i-1) & 0x7ff));
				srampoke16(0x4, x);
				if (x != srampeek16(0x4)) {
					fprintf(stderr, "adr=0x%x\n", i);
					fprintf(stderr, "dat=0x%x\n", x);
					assert(0);
				}
			}
			else srampoke16(i & 0xfff, x);
			i++;
		}
}

void ts8820_sram_write(int bytes) {
    if (is_ts4700())
		ts8820_sram_write_4700(bytes);
	else if (is_ts4800())
		ts8820_sram_write_4800(bytes); 
}

void ts8820_sram_read_4700(int bytes) {
        unsigned short x;
        unsigned char c;
        unsigned int i = 0;
        poke16(0x4, 0x401);
        while (i < bytes) {
                if((i & 0xfff) == 0) poke16(0x6, (i >> 12));
                x = srampeek16(i & 0xfff);
                c = x & 0xff;
                putchar(c);
                i++;
                c = x >> 8;
                if (i < bytes) putchar(c);
                i++;
        }
}

void ts8820_sram_read_4800(int bytes) {
        unsigned short x;
        unsigned char c;
        unsigned int i = 0;
	int windowed = (*syscon == 0x4800);
        while (i < bytes) {
                if((i & 0xfff) == 0) poke16(0x6, (i >> 12));
		if(((i & 0x7ff) == 0) && windowed) {
			srampoke16(0x0, ((i >> 11) & 1) + 2);
			srampoke16(0x2, 0x8800); // 16 bit, auto-increment
		}
		if (windowed) x = srampeek16(0x6);
                else x = srampeek16(i & 0xfff);
                c = x & 0xff;
                putchar(c);
                i++;
                c = x >> 8;
                if (i < bytes) putchar(c);
                i++;
        }
}

void ts8820_sram_read(int bytes) {
	if (is_ts4700())
		ts8820_sram_read_4700(bytes);
	else if (is_ts4800())
		ts8820_sram_read_4800(bytes);
}
#endif
