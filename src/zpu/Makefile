CC = zpu-elf-gcc
OBJCOPY = zpu-elf-objcopy

CFLAGS = -abel -Os
LDFLAGS = -Wl,-relax -Wl,-gc-sections

all: zpu_muxbus.bin zpu_demo.bin

%.o: %.c
	$(CC) $(CFLAGS) -c $<

zpu_muxbus.bin: zpu_fifo.o zpu_muxbus.o
	$(CC) $(CFLAGS) $(LDFLAGS) $^ -o $@
	$(OBJCOPY) -S -O binary $@

zpu_demo.bin: zpu_fifo.o zpu_strings.o zpu_demo.o
	$(CC) $(CFLAGS) $(LDFLAGS) $^ -o $@
	$(OBJCOPY) -S -O binary $@

clean:
	rm -f *.o *.bin
