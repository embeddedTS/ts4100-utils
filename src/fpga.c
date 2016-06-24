#include <stdio.h>
#include <unistd.h>
#include <dirent.h> 
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "i2c-dev.h"

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

int fpeek(int twifd, uint16_t addr, uint8_t* data, int size)
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
	msgs[1].flags = I2C_M_RD | I2C_M_NOSTART;
	msgs[1].len	= size;
	msgs[1].buf	= (char *)data;

	packets.msgs  = msgs;
	packets.nmsgs = 2;

	if(ioctl(twifd, I2C_RDWR, &packets) < 0) {
		return 1;
	}
	return 0;
}

int fpoke(int twifd, uint16_t addr, uint8_t *data, int size)
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
	msgs[1].flags = I2C_M_NOSTART;
	msgs[1].len	= size;
	msgs[1].buf	= (char *)data;

	packets.msgs  = msgs;
	packets.nmsgs = 2;

	if(ioctl(twifd, I2C_RDWR, &packets) < 0) {
		return 1;
	}
	return 0;
}
