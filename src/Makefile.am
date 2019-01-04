tshwctl_SOURCES = tshwctl.c fpga.c
tshwctl_LDFLAGS = -lm
tshwctl_CPPFLAGS = -Wall

load_fpga_SOURCES = load_fpga-ts4100.c load_fpga.c -o load_fpga gpiolib.c ispvm.c
load_fpga_LDFLAGS = -mcpu=cortex-a9
load_fpga_CPPFLAGS = -Wall

tszpuctl_SOURCES = tszpuctl.c fpga.c tszpufifo.c gpiolib.c
tszpuctl_LDFLAGS = -mcpu=cortex-a9
tszpuctl_CPPFLAGS = -Wall

tsmicroctl_SOURCES = tsmicroctl.c
tsmicroctl_CPPFLAGS = -DCTL -Wall

bin_PROGRAMS = tshwctl load_fpga tsmicroctl tszpuctl
