#ifndef __FPGA_H_
#define __FPGA_H_

int fpga_init();
int fpeekstream8(int twifd,  uint8_t *data, uint16_t addr, int size);
int fpokestream8(int twifd, uint8_t *data, uint16_t addr, int size);
uint8_t fpeek8(int twifd, uint16_t addr);
void fpoke8(int twifd, uint16_t addr, uint8_t data);

#endif