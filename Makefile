CFLAGS = -target x86_64-pc-none-elf -mcpu=x86_64 -ffreestanding -masm=intel -mcmodel=large -mno-red-zone -mno-mmx -mno-sse -mno-sse2 -fno-PIC -O2 -Wall -Wextra
LDFLAGS = -target x86_64-pc-none-elf -mcpu=x86_64 -ffreestanding -static -nostdlib -O2

HEADERS = $(wildcard *.h)
OBJECTS = $(patsubst %.c,%.o,$(wildcard *.c)) $(patsubst %.s,%.s.o,$(wildcard *.s))

kernel.bin: $(OBJECTS) linker.ld
	clang $(LDFLAGS) -T linker.ld $(OBJECTS) -o $@

%.s.o: %.s
	nasm $< -f elf64 -o $@

%.c.o: %.c $(HEADERS)
	clang -c $(CFLAGS) -o $@
