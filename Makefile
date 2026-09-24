# EFI Partition Manager
#
#   make            build build/partmgr.efi (UEFI x86_64)
#   make test       the partition table tests on Linux (disk images, sfdisk and
#                   parted as references), and the documentation check
#   make qemu-test  partmgr.efi inside QEMU/OVMF on test disks
#   make docs       rewrite the line counts of the technical manual
#   make font       render src/partmgr/font_inter.bin again (tools/gen-font.py)

CC      ?= gcc
LD      ?= ld
PYTHON  ?= python3
# OVMF firmware image (a single-file OVMF.fd): the first one found, or OVMF=...
OVMF    ?= $(firstword $(wildcard /usr/share/ovmf/OVMF.fd /usr/share/OVMF/OVMF.fd) OVMF.fd)

BUILD   := build

# the partition table code: portable, also compiled into the Linux test tool
TABLE_SRC := src/partmgr/ptable.c src/partmgr/ptedit.c src/partmgr/ptwrite.c src/partmgr/units.c \
	src/lib/crc32.c src/lib/util.c
PAL_SRC := src/lib/libc.c src/lib/fmt.c src/lib/util.c src/lib/crc32.c src/pal/pal_efi.c src/pal/pal_common.c
EFI_SRC := $(TABLE_SRC) src/partmgr/main.c src/partmgr/diskview.c src/partmgr/ui.c src/partmgr/disks_efi.c \
	src/lib/libc.c src/lib/fmt.c src/pal/pal_efi.c src/pal/pal_common.c
# the drawing code of the graphical interface and its font
GFX_SRC := src/partmgr/gfx.c src/partmgr/font_inter.S src/pal/pal_gfx_efi.c src/pal/pal_mouse_efi.c
# gfxdemo.efi: the test picture on the screen, for make qemu-test
DEMO_SRC := tests/partmgr/gfxdemo.c tests/partmgr/gfxscene.c $(GFX_SRC) $(PAL_SRC)

WARN := -Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers

EFI_CFLAGS := -std=gnu11 -O2 $(WARN) -ffreestanding -fno-stack-protector -fno-stack-check \
	-mno-red-zone -fshort-wchar -fPIC -fvisibility=hidden -fno-asynchronous-unwind-tables \
	-fno-builtin -fno-tree-loop-distribute-patterns -fno-strict-aliasing -mgeneral-regs-only \
	-Iinclude
EFI_LDFLAGS := -nostdlib -znocombreloc -shared -Bsymbolic --no-undefined --build-id=none \
	-z noexecstack -T tools/efi.lds

HOST_CFLAGS := -std=gnu11 -O1 -g $(WARN) -DPARTMGR_HOST -D_FILE_OFFSET_BITS=64 -Iinclude \
	-fsanitize=address,undefined

EFI_OBJ := $(EFI_SRC:%.c=$(BUILD)/efi/%.o)
DEMO_OBJ := $(patsubst %.S,$(BUILD)/efi/%.o,$(DEMO_SRC:%.c=$(BUILD)/efi/%.o))

all: $(BUILD)/partmgr.efi

$(BUILD)/efi/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(EFI_CFLAGS) -MMD -c $< -o $@

# the glyphs are included by the assembler: rebuild when they change
$(BUILD)/efi/%.o: %.S src/partmgr/font_inter.bin
	@mkdir -p $(dir $@)
	$(CC) -c $< -o $@

$(BUILD)/partmgr.so: $(EFI_OBJ) tools/efi.lds
	$(LD) $(EFI_LDFLAGS) $(EFI_OBJ) -o $@

$(BUILD)/partmgr.efi: $(BUILD)/partmgr.so tools/elf2efi.py
	$(PYTHON) tools/elf2efi.py $< $@
	@ls -l $@ | awk '{print "partmgr.efi: " $$5 " bytes"}'

$(BUILD)/tests/gfxdemo.so: $(DEMO_OBJ) tools/efi.lds
	@mkdir -p $(dir $@)
	$(LD) $(EFI_LDFLAGS) $(DEMO_OBJ) -o $@

$(BUILD)/tests/gfxdemo.efi: $(BUILD)/tests/gfxdemo.so tools/elf2efi.py
	$(PYTHON) tools/elf2efi.py $< $@

# the drawing code on Linux: the checks and the test picture
$(BUILD)/tests/gfxtool: tests/partmgr/gfxtool.c tests/partmgr/gfxscene.c src/partmgr/gfx.c src/partmgr/gfx.h \
		src/partmgr/font_inter.S src/partmgr/font_inter.bin src/lib/util.c src/lib/rt.h
	@mkdir -p $(dir $@)
	$(CC) $(HOST_CFLAGS) tests/partmgr/gfxtool.c tests/partmgr/gfxscene.c src/partmgr/gfx.c \
		src/partmgr/font_inter.S src/lib/util.c -o $@

# drives the table code on a disk image (tests/partmgr/run-tests.py)
$(BUILD)/tests/pttool: tests/partmgr/pttool.c $(TABLE_SRC) src/partmgr/ptable.h src/partmgr/ptint.h \
		src/partmgr/units.h src/lib/crc32.h src/lib/rt.h
	@mkdir -p $(dir $@)
	$(CC) $(HOST_CFLAGS) tests/partmgr/pttool.c $(TABLE_SRC) -o $@

test: $(BUILD)/tests/pttool $(BUILD)/tests/gfxtool
	@$(PYTHON) tests/partmgr/run-tests.py $(BUILD)/tests/pttool
	@$(BUILD)/tests/gfxtool check
	@$(PYTHON) tools/gen-docs.py --check

docs:
	@$(PYTHON) tools/gen-docs.py

# partmgr.efi started by the firmware, driven with keys, on disks made with sfdisk
qemu-test: $(BUILD)/partmgr.efi $(BUILD)/tests/gfxdemo.efi $(BUILD)/tests/gfxtool
	@$(PYTHON) tests/partmgr/qemu-test.py $(OVMF) $(BUILD)/partmgr.efi $(BUILD)/tests/gfxdemo.efi \
		$(BUILD)/tests/gfxtool

font:
	@$(PYTHON) tools/gen-font.py

clean:
	rm -rf $(BUILD)

.PHONY: all test docs qemu-test font clean

-include $(EFI_OBJ:.o=.d) $(DEMO_OBJ:.o=.d)
