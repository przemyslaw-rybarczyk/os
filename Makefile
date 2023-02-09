KERNEL_CFLAGS = -target x86_64-pc-none-elf -ffreestanding -masm=intel -mcmodel=kernel -mno-red-zone -mno-x87 -mno-mmx -mno-sse -mno-sse2 -fno-PIC -nostdlibinc -O2 -Wall -Wextra
KERNEL_LDFLAGS = -target x86_64-pc-none-elf -ffreestanding -static -nostdlib -O2

PROGRAM_CFLAGS = -target x86_64-pc-none-elf -ffreestanding -masm=intel -mcmodel=kernel -mno-red-zone -mno-x87 -mno-mmx -mno-sse -mno-sse2 -fno-PIC -nostdlibinc -O2 -Wall -Wextra
PROGRAM_LDFLAGS = -target x86_64-pc-none-elf -ffreestanding -static -nostdlib -O2

KERNEL_HEADERS = $(wildcard kernel/*.h)
KERNEL_OBJECTS = $(patsubst %.c,%.o,$(wildcard kernel/*.c)) $(patsubst %.s,%.s.o,$(wildcard kernel/*.s))

PROGRAM_HEADERS = $(wildcard program/*.h)
PROGRAM_OBJECTS = $(patsubst %.c,%.o,$(wildcard program/*.c)) $(patsubst %.s,%.s.o,$(wildcard program/*.s))

kernel.bin: $(KERNEL_OBJECTS) kernel/linker.ld program/program.bin
	clang $(KERNEL_LDFLAGS) -T kernel/linker.ld $(KERNEL_OBJECTS) -o $@
# Pad the file so its size is a multiple of 512 (sector size)
	./pad_to_multiple.sh $@ 512

kernel/included_programs.s.o: kernel/included_programs.s program/program.bin
	nasm $< -f elf64 -o $@

kernel/%.s.o: kernel/%.s
	nasm $< -f elf64 -o $@

kernel/%.o: kernel/%.c $(KERNEL_HEADERS)
	clang $< -c $(KERNEL_CFLAGS) -o $@

program/program.bin: $(PROGRAM_OBJECTS)
	clang $(PROGRAM_LDFLAGS) $(PROGRAM_OBJECTS) -o $@

program/%.s.o: program/%.s
	nasm $< -f elf64 -o $@

program/%.o: program/%.c $(PROGRAM_HEADERS)
	clang $< -c $(PROGRAM_CFLAGS) -o $@
