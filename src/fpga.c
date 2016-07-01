#include <stdio.h>
#include <unistd.h>
#include <dirent.h> 
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include "i2c-dev.h"
#include "fpga.h"

int fpga_init()
{
	static int fd = -1;

	if(fd != -1)
		return fd;

	fd = open("/dev/i2c-2", O_RDWR);
	if(fd != -1) {
		if (ioctl(fd, I2C_SLAVE_FORCE, 0x28) < 0) {
			perror("FPGA did not ACK 0x28\n");
			return -1;
		}
	}

	return fd;
}

void fpoke8(int twifd, uint16_t addr, uint8_t data)
{
	int ret;
	ret = fpokestream8(twifd, &data, addr, 1);
	if(ret) {
		perror("Failed to write to FPGA");
	}
}

uint8_t fpeek8(int twifd, uint16_t addr)
{
	uint8_t data = 0;
	int ret;
	ret = fpeekstream8(twifd, &data, addr, 1);

	if(ret) {
		perror("Failed to read from FPGA");
	}
	return data;
}

int fpeekstream8(int twifd, uint8_t *data, uint16_t addr, int bytes)
{
	struct i2c_rdwr_ioctl_data packets;
	struct i2c_msg msgs[2];
	char busaddr[2];

	busaddr[0] = ((addr >> 8) & 0xff);
	busaddr[1] = (addr & 0xff);

	msgs[0].addr = 0x28;
	msgs[0].flags = 0;
	msgs[0].len	= 2;
	msgs[0].buf	= busaddr;

	msgs[1].addr = 0x28;
	msgs[1].flags = I2C_M_RD;
	msgs[1].len	= bytes;
	msgs[1].buf	= data;

    packets.msgs  = msgs;
    packets.nmsgs = 2;

    if(ioctl(twifd, I2C_RDWR, &packets) < 0) {
        perror("Unable to send data");
        return 1;
    }
    return 0;
}

int fpokestream8(int twifd, uint8_t *data, uint16_t addr, int size)
{
	struct i2c_rdwr_ioctl_data packets;
	struct i2c_msg msg;
	char busaddr[2];
	uint8_t outdata[4096];

	/* Linux only supports 4k transactions at a time, and we need
	 * two bytes for the address */
	assert(size <= 4094);

	outdata[0] = ((addr >> 8) & 0xff);
	outdata[1] = (addr & 0xff);
	memcpy(&outdata[2], data, size);

	msg.addr = 0x28;
	msg.flags = 0;
	msg.len	= 2 + size;
	msg.buf	= (char *)outdata;

	packets.msgs  = &msg;
	packets.nmsgs = 1;

	if(ioctl(twifd, I2C_RDWR, &packets) < 0) {
		return 1;
	}
	return 0;
}
