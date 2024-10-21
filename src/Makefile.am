GITCOMMIT:= $(shell git describe --abbrev=12 --dirty --always)

tshwctl_SOURCES = tshwctl.c fpga.c eval_cmdline.c
tshwctl_CPPFLAGS = -Wall -DGITCOMMIT="\"${GITCOMMIT}\""

tszpuctl_SOURCES = tszpuctl.c fpga.c tszpufifo.c
tszpuctl_LDFLAGS = -mcpu=cortex-a9
tszpuctl_CPPFLAGS = -Wall -DGITCOMMIT="\"${GITCOMMIT}\""

tsmicroctl_SOURCES = tsmicroctl.c
tsmicroctl_CPPFLAGS = -DCTL -Wall -DGITCOMMIT="\"${GITCOMMIT}\""

ts8820ctl_SOURCES = tszpufifo.c fpga.c ts8820ctl.c ts8820.c
ts8820ctl_LDFLAGS = -mcpu=cortex-a9
ts8820ctl_CPPFLAGS = -Wall -DGITCOMMIT="\"${GITCOMMIT}\""

tsmuxbusctl_SOURCES = tszpufifo.c fpga.c tsmuxbusctl.c
tsmuxbusctl_LDFLAGS = -mcpu=cortex-a9
tsmuxbusctl_CPPFLAGS = -Wall -DGITCOMMIT="\"${GITCOMMIT}\""

zpu_offload_demo_lcd_interface_SOURCES = zpu_offload_demo_lcd_interface.c tszpufifo.c fpga.c
zpu_offload_demo_lcd_interface_LDFLAGS = -mcpu=cortex-a9
zpu_offload_demo_lcd_interface_CPPFLAGS = -Wall -DGITCOMMIT="\"${GITCOMMIT}\""

zpu_offload_demo_lcd_program_SOURCES = zpu_offload_demo_lcd_program.c
zpu_offload_demo_lcd_program_LDFLAGS = -mcpu=cortex-a9
zpu_offload_demo_lcd_program_CPPFLAGS = -Wall -DGITCOMMIT="\"${GITCOMMIT}\""

bin_PROGRAMS = tshwctl tsmicroctl tszpuctl ts8820ctl tsmuxbusctl
noinst_PROGRAMS = zpu_offload_demo_lcd_interface zpu_offload_demo_lcd_program
