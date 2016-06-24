#ifndef _LOAD_FPGA_TS4100_
#define _LOAD_FPGA_TS4100_

#include <stdint.h>

void init_ts4100(void);
void restore_ts4100(void);
int readport_ts4100(void);
void writeport_ts4100(int pins, int val);
void sclock_ts4100(void);
void udelay_imx6(unsigned int us);

#endif