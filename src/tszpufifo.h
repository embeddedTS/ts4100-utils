#ifndef __TSZPUFIFO_H__
#define __TSZPUFIFO_H__

enum flowcontrol {
	NO_FLOW_CTRL = 0,
	FLOW_CTRL = 1,
};

void zpu_fifo_deinit(int twifd);
int32_t zpu_fifo_init(int twifd, int flow_control);
size_t zpu_fifo_get(int twifd, uint8_t *buf, size_t size);
size_t zpu_fifo_put(int twifd, uint8_t *buf, size_t size);

uint16_t zpu_muxbus_peek16(int twifd, uint16_t adr);
void zpu_muxbus_poke16(int twifd, uint16_t adr, uint16_t dat);

#endif // __TSZPUFIFO_H__
