# Build directory
BUILD = build

# Flags for building kernel
KERNEL_CFLAGS = -target x86_64-pc-none-elf -ffreestanding -masm=intel -fno-PIC -mcmodel=kernel -mno-red-zone -mno-x87 -mno-mmx -mno-sse -mno-sse2 -fno-PIC -nostdlibinc -O2 -Wall -Wextra -fomit-frame-pointer -mstack-alignment=8 -D_KERNEL -Ilibc/include
KERNEL_LDFLAGS = -target x86_64-pc-none-elf -ffreestanding -static -nostdlib -O2

# Flags for building programs and libraries
USER_CFLAGS = -target x86_64-pc-none-elf -ffreestanding -masm=intel -fno-PIC -nostdlibinc -O2 -Wall -Wextra -fomit-frame-pointer -Ilibc/include
USER_LDFLAGS = -target x86_64-pc-none-elf -ffreestanding -static -nostdlib -O2

# All subprojects other than the kernel are either programs or libraries
PROGRAMS = program1 program2 terminal window
LIBS = libc

# The dependencies for each subproject
program1_DEPS = libc
program2_DEPS = libc
terminal_DEPS = libc
window_DEPS = libc
libc_DEPS =

# Recursively search for files with a given extension
# Currently only searches seven levels down, but this should be enough for any purpose.
# $(1) = subfolder to search, $(2) = extension
define search =
$(wildcard $(1)/*$(2) $(1)/*/*$(2) $(1)/*/*/*$(2) $(1)/*/*/*/*$(2) $(1)/*/*/*/*/*$(2) $(1)/*/*/*/*/*/*$(2) $(1)/*/*/*/*/*/*/*$(2) $(1)/*/*/*/*/*/*/*/*$(2))
endef

# Generates the header and object list for a subproject
# $(1) = name of subproject
define defs_template =
$(1)_INCLUDE = $$(call search,$(1)/include,.h)
$(1)_HEADERS = $$(call search,$(1),.h)
$(1)_ASM_HEADERS = $$(call search,$(1),.inc)
$(1)_OBJECTS = $$(patsubst %.c,$$(BUILD)/%.o,$$(call search,$(1),.c)) $$(patsubst %.s,$$(BUILD)/%.s.o,$$(call search,$(1),.s))
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

$$(BUILD)/$(1)/%.o: $(1)/%.c $$($(1)_HEADERS) $$(foreach dep,$(2),$$($$(dep)_INCLUDE))
	$$(call c_recipe,$(3))

endef

$(BUILD)/image.bin: $(kernel_OBJECTS) kernel/linker.ld
	clang $(KERNEL_LDFLAGS) -T kernel/linker.ld $(kernel_OBJECTS) -o $@
# Pad the file so its size is large enough to run in an emulator
	truncate -s '>516096' $@

$(BUILD)/window/included_programs.s.o: window/included_programs.s $(BUILD)/program1/program1.bin $(BUILD)/program2/program2.bin $(BUILD)/terminal/terminal.bin
	$(asm_recipe)

$(BUILD)/kernel/included_programs.s.o: kernel/included_programs.s $(BUILD)/window/window.bin
	$(asm_recipe)

$(eval $(call common_template,kernel,libc,$(KERNEL_CFLAGS)))

# Defines the rules for building a library
# $(1) = name of library, $(2) = library dependencies
define library_template =

$$(eval $$(call common_template,$(1),$(2),$$(USER_CFLAGS)))

$$(BUILD)/$(1)/$(1).a: $$($(1)_OBJECTS) $$(foreach dep,$(2),$$(BUILD)/$$(dep)/$$(dep).a)
	ar rcs $$@ $$($(1)_OBJECTS) $$(foreach dep,$(2),$$(BUILD)/$$(dep)/$$(dep).a)

endef

# Defines the rules for building a program
# $(1) = name of program, $(2) = library dependencies
define program_template =

$$(BUILD)/$(1)/$(1).bin: $$($(1)_OBJECTS) $$(foreach dep,$(2),$$(BUILD)/$$(dep)/$$(dep).a)
	clang $$(USER_LDFLAGS) $$($(1)_OBJECTS) $$(foreach dep,$(2),$$(BUILD)/$$(dep)/$$(dep).a) -o $$@

$$(eval $$(call common_template,$(1),$(2),$$(USER_CFLAGS)))

endef

# Generate the rules for all programs and libraries
$(foreach lib,$(LIBS),$(eval $(call library_template,$(lib),$($(lib)_DEPS))))
$(foreach program,$(PROGRAMS),$(eval $(call program_template,$(program),$($(program)_DEPS))))
