# Build directory
BUILD = build

# Flags for building kernel
KERNEL_CFLAGS = -target x86_64-pc-none-elf -ffreestanding -masm=intel -mcmodel=kernel -mno-red-zone -mno-x87 -mno-mmx -mno-sse -mno-sse2 -fno-PIC -nostdlibinc -O2 -Wall -Wextra -fomit-frame-pointer -mstack-alignment=8
KERNEL_LDFLAGS = -target x86_64-pc-none-elf -ffreestanding -static -nostdlib -O2

# Flags for building programs and libraries
USER_CFLAGS = -target x86_64-pc-none-elf -ffreestanding -masm=intel -mno-red-zone -mno-x87 -mno-mmx -mno-sse -mno-sse2 -fno-PIC -nostdlibinc -O2 -Wall -Wextra -fomit-frame-pointer -Ilibc
USER_LDFLAGS = -target x86_64-pc-none-elf -ffreestanding -static -nostdlib -O2

# All subprojects other than the kernel are either programs or libraries
PROGRAMS = program1 program2
LIBS = libc

# The dependencies for each subproject
program1_DEPS = libc
program2_DEPS = libc
libc_DEPS =

# Generates the header and object list for a subproject
# $(1) = name of subproject
define defs_template =
$(1)_HEADERS = $$(wildcard $(1)/*.h)
$(1)_ASM_HEADERS = $$(wildcard $(1)/*.inc)
$(1)_OBJECTS = $$(patsubst %.c,$$(BUILD)/%.o,$$(wildcard $(1)/*.c)) $$(patsubst %.s,$$(BUILD)/%.s.o,$$(wildcard $(1)/*.s))
endef

# Generate all header and object lists
$(foreach dir,kernel $(PROGRAMS) $(LIBS),$(eval $(call defs_template,$(dir))))

# Template for C recipes
# $(1) = C compiler flags
define c_recipe =
clang $< -c $(1) -o $@
endef

# Template for assembly recipes
define asm_recipe =
nasm $< -f elf64 -o $@
endef

# Defines the common rules for building a subproject
# $(1) = name of subproject, $(2) = library dependencies, $(3) = C compiler flags
define common_template =

$$(BUILD)/$(1):
	mkdir -p $$@

$$($(1)_OBJECTS): | $$(BUILD)/$(1)

$$(BUILD)/$(1)/%.s.o: $(1)/%.s $$($(1)_ASM_HEADERS) $$(foreach dep,$(2),$$($$(dep)_ASM_HEADERS))
	$$(asm_recipe)

$$(BUILD)/$(1)/%.o: $(1)/%.c $$($(1)_HEADERS) $$(foreach dep,$(2),$$($$(dep)_HEADERS))
	$$(call c_recipe,$(3))

endef

PROGRAM_EXECUTABLES = $(foreach program,$(PROGRAMS),$(BUILD)/$(program)/$(program).bin)

$(BUILD)/image.bin: $(kernel_OBJECTS) kernel/linker.ld
	clang $(KERNEL_LDFLAGS) -T kernel/linker.ld $(kernel_OBJECTS) -o $@
# Pad the file so its size is a multiple of 512 (sector size)
	./pad_to_multiple.sh $@ 512

$(BUILD)/kernel/included_programs.s.o: kernel/included_programs.s $(PROGRAM_EXECUTABLES)
	$(asm_recipe)

$(eval $(call common_template,kernel,,$(KERNEL_CFLAGS)))

# Defines the rules for building a library
# $(1) = name of library, $(2) = library dependencies
define library_template =
$$(eval $$(call common_template,$(1),$(2),$$(USER_CFLAGS)))
endef

# Defines the rules for building a program
# $(1) = name of program, $(2) = library dependencies
define program_template =

$$(BUILD)/$(1)/$(1).bin: $$($(1)_OBJECTS) $$(foreach dep,$(2),$$($$(dep)_OBJECTS))
	clang $$(USER_LDFLAGS) $$($(1)_OBJECTS) $$(foreach dep,$(2),$$($$(dep)_OBJECTS)) -o $$@

$$(eval $$(call common_template,$(1),$(2),$$(USER_CFLAGS)))

endef

# Generate the rules for all programs and libraries
$(foreach lib,$(LIBS),$(eval $(call library_template,$(lib),$($(lib)_DEPS))))
$(foreach program,$(PROGRAMS),$(eval $(call program_template,$(program),$($(program)_DEPS))))
