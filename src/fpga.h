#ifndef __FPGA_H_
#define __FPGA_H_

int fpga_init();
int fpeek(int twifd, uint16_t addr, uint8_t* data, int size);
int fpoke(int twifd, uint16_t addr, uint8_t *data, int size);

#endif