#ifndef __TSZPUFIFO_H__
#define __TSZPUFIFO_H__

void zpu_fifo_deinit(int twifd);
int32_t zpu_fifo_init(int twifd, int flow_control);
size_t zpu_fifo_get(int twifd, uint8_t *buf, size_t size);
size_t zpu_fifo_put(int twifd, uint8_t *buf, size_t size);

#endif // __TSZPUFIFO_H__
