GITCOMMIT:= $(shell git describe --abbrev=12 --dirty --always)

tshwctl_SOURCES = tshwctl.c fpga.c eval_cmdline.c
tshwctl_CPPFLAGS = -Wall -DGITCOMMIT="\"${GITCOMMIT}\""

load_fpga_SOURCES = load_fpga-ts4100.c load_fpga.c -o load_fpga gpiolib.c ispvm.c
load_fpga_LDFLAGS = -mcpu=cortex-a9
load_fpga_CPPFLAGS = -Wall -DGITCOMMIT="\"${GITCOMMIT}\""

tszpuctl_SOURCES = tszpuctl.c fpga.c tszpufifo.c gpiolib.c
tszpuctl_LDFLAGS = -mcpu=cortex-a9
tszpuctl_CPPFLAGS = -Wall -DGITCOMMIT="\"${GITCOMMIT}\""

tsmicroctl_SOURCES = tsmicroctl.c
tsmicroctl_CPPFLAGS = -DCTL -Wall -DGITCOMMIT="\"${GITCOMMIT}\""

ts8820ctl_SOURCES = tszpufifo.c gpiolib.c fpga.c ts8820ctl.c ts8820.c
ts8820ctl_LDFLAGS = -mcpu=cortex-a9
ts8820ctl_CPPFLAGS = -Wall -DGITCOMMIT="\"${GITCOMMIT}\""

bin_PROGRAMS = tshwctl tsmicroctl tszpuctl ts8820ctl
noinst_PROGRAMS = load_fpga
