CFLAGS = -target x86_64-pc-none-elf -ffreestanding -masm=intel -mcmodel=kernel -mno-red-zone -mno-x87 -mno-mmx -mno-sse -mno-sse2 -fno-PIC -O2 -Wall -Wextra
LDFLAGS = -target x86_64-pc-none-elf -ffreestanding -static -nostdlib -O2

KERNEL_HEADERS = $(wildcard kernel/*.h)
KERNEL_OBJECTS = $(patsubst %.c,%.o,$(wildcard kernel/*.c)) $(patsubst %.s,%.s.o,$(wildcard kernel/*.s))

kernel.bin: $(KERNEL_OBJECTS) kernel/linker.ld
	clang $(LDFLAGS) -T kernel/linker.ld $(KERNEL_OBJECTS) -o $@
# Pad the file so its size is a multiple of 512 (sector size)
	./pad_to_multiple.sh $@ 512

kernel/%.s.o: kernel/%.s
	nasm $< -f elf64 -o $@

kernel/%.o: kernel/%.c $(KERNEL_HEADERS)
	clang $< -c $(CFLAGS) -o $@
