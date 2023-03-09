BUILD = build

KERNEL_CFLAGS = -target x86_64-pc-none-elf -ffreestanding -masm=intel -mcmodel=kernel -mno-red-zone -mno-x87 -mno-mmx -mno-sse -mno-sse2 -fno-PIC -nostdlibinc -O2 -Wall -Wextra -fomit-frame-pointer
KERNEL_LDFLAGS = -target x86_64-pc-none-elf -ffreestanding -static -nostdlib -O2

PROGRAM_CFLAGS = -target x86_64-pc-none-elf -ffreestanding -masm=intel -mcmodel=kernel -mno-red-zone -mno-x87 -mno-mmx -mno-sse -mno-sse2 -fno-PIC -nostdlibinc -O2 -Wall -Wextra -fomit-frame-pointer
PROGRAM_LDFLAGS = -target x86_64-pc-none-elf -ffreestanding -static -nostdlib -O2

KERNEL_HEADERS = $(wildcard kernel/*.h)
KERNEL_OBJECTS = $(patsubst %.c,$(BUILD)/%.o,$(wildcard kernel/*.c)) $(patsubst %.s,$(BUILD)/%.s.o,$(wildcard kernel/*.s))

PROGRAMS = program1 program2

PROGRAM_EXECUTABLES = $(foreach program,$(PROGRAMS),$(BUILD)/$(program)/$(program).bin)

$(BUILD)/image.bin: $(KERNEL_OBJECTS) kernel/linker.ld $(PROGRAM_EXECUTABLES)
	clang $(KERNEL_LDFLAGS) -T kernel/linker.ld $(KERNEL_OBJECTS) -o $@
# Pad the file so its size is a multiple of 512 (sector size)
	./pad_to_multiple.sh $@ 512

$(KERNEL_OBJECTS): | $(BUILD)/kernel

$(BUILD)/kernel/included_programs.s.o: kernel/included_programs.s $(PROGRAM_EXECUTABLES)
	nasm $< -f elf64 -o $@

$(BUILD)/kernel/%.s.o: kernel/%.s
	nasm $< -f elf64 -o $@

$(BUILD)/kernel/%.o: kernel/%.c $(KERNEL_HEADERS)
	clang $< -c $(KERNEL_CFLAGS) -o $@

$(BUILD)/kernel $(foreach program,$(PROGRAMS),$(BUILD)/$(program)):
	mkdir -p $@

define program_template =

$(1)_HEADERS = $$(wildcard $(1)/*.h)
$(1)_OBJECTS = $$(patsubst %.c,$$(BUILD)/%.o,$$(wildcard $(1)/*.c)) $$(patsubst %.s,$$(BUILD)/%.s.o,$$(wildcard $(1)/*.s))

$$($(1)_OBJECTS): | $$(BUILD)/$(1)

$$(BUILD)/$(1)/$(1).bin: $$($(1)_OBJECTS)
	clang $$(PROGRAM_LDFLAGS) $$($(1)_OBJECTS) -o $$@

$$(BUILD)/$(1)/%.s.o: $(1)/%.s
	nasm $$< -f elf64 -o $$@

$$(BUILD)/$(1)/%.o: $(1)/%.c $$($(1)_HEADERS)
	clang $$< -c $$(PROGRAM_CFLAGS) -o $$@

endef

$(foreach program,$(PROGRAMS),$(eval $(call program_template,$(program))))
