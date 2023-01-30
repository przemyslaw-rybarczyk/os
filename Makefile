CFLAGS = -target x86_64-pc-none-elf -ffreestanding -masm=intel -mcmodel=kernel -mno-red-zone -mno-x87 -mno-mmx -mno-sse -mno-sse2 -fno-PIC -O2 -Wall -Wextra
LDFLAGS = -target x86_64-pc-none-elf -ffreestanding -static -nostdlib -O2

HEADERS = $(wildcard *.h)
OBJECTS = $(patsubst %.c,%.o,$(wildcard *.c)) $(patsubst %.s,%.s.o,$(wildcard *.s))

kernel.bin: $(OBJECTS) linker.ld
	clang $(LDFLAGS) -T linker.ld $(OBJECTS) -o $@
# Pad the file so its size is a multiple of 512 (sector size)
	./pad_to_multiple.sh $@ 512

%.s.o: %.s
	nasm $< -f elf64 -o $@

%.o: %.c $(HEADERS)
	clang $< -c $(CFLAGS) -o $@
