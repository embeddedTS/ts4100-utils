CC = zpu-elf-gcc
OBJCOPY = zpu-elf-objcopy

CFLAGS = -abel -Os
LDFLAGS = -Wl,-relax -Wl,-gc-sections

all: zpu_muxbus.bin zpu_demo.bin zpu_offload_demo.bin

%.o: %.c
	$(CC) $(CFLAGS) -c $<

zpu_demo.bin: fifo.o strings.o zpu_demo.o
	$(CC) $(CFLAGS) $(LDFLAGS) $^ -o $@
	$(OBJCOPY) -S -O binary $@

%.bin: fifo.o muxbus.o %.o
	$(CC) $(CFLAGS) $(LDFLAGS) $^ -o $@
	$(OBJCOPY) -S -O binary $@

clean:
	rm -f *.o *.bin
