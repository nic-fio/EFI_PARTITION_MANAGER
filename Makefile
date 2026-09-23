# EFI Partition Manager
#
#   make            build build/partmgr.efi (UEFI x86_64)
#   make test       the partition table tests on Linux (disk images, sfdisk and
#                   parted as references), and the documentation check
#   make qemu-test  partmgr.efi inside QEMU/OVMF on test disks
#   make docs       rewrite the line counts of the technical manual

CC      ?= gcc
LD      ?= ld
PYTHON  ?= python3
# OVMF firmware image (a single-file OVMF.fd): the first one found, or OVMF=...
OVMF    ?= $(firstword $(wildcard /usr/share/ovmf/OVMF.fd /usr/share/OVMF/OVMF.fd) OVMF.fd)

BUILD   := build

# the partition table code: portable, also compiled into the Linux test tool
TABLE_SRC := src/partmgr/ptable.c src/partmgr/ptedit.c src/partmgr/ptwrite.c src/partmgr/units.c \
	src/lib/crc32.c src/lib/util.c
EFI_SRC := $(TABLE_SRC) src/partmgr/main.c src/partmgr/diskview.c src/partmgr/ui.c src/partmgr/disks_efi.c \
	src/lib/libc.c src/lib/fmt.c src/pal/pal_efi.c src/pal/pal_common.c

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

all: $(BUILD)/partmgr.efi

$(BUILD)/efi/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(EFI_CFLAGS) -MMD -c $< -o $@

$(BUILD)/partmgr.so: $(EFI_OBJ) tools/efi.lds
	$(LD) $(EFI_LDFLAGS) $(EFI_OBJ) -o $@

$(BUILD)/partmgr.efi: $(BUILD)/partmgr.so tools/elf2efi.py
	$(PYTHON) tools/elf2efi.py $< $@
	@ls -l $@ | awk '{print "partmgr.efi: " $$5 " bytes"}'

# drives the table code on a disk image (tests/partmgr/run-tests.py)
$(BUILD)/tests/pttool: tests/partmgr/pttool.c $(TABLE_SRC) src/partmgr/ptable.h src/partmgr/ptint.h \
		src/partmgr/units.h src/lib/crc32.h src/lib/rt.h
	@mkdir -p $(dir $@)
	$(CC) $(HOST_CFLAGS) tests/partmgr/pttool.c $(TABLE_SRC) -o $@

test: $(BUILD)/tests/pttool
	@$(PYTHON) tests/partmgr/run-tests.py $(BUILD)/tests/pttool
	@$(PYTHON) tools/gen-docs.py --check

docs:
	@$(PYTHON) tools/gen-docs.py

# partmgr.efi started by the firmware, driven with keys, on disks made with sfdisk
qemu-test: $(BUILD)/partmgr.efi
	@$(PYTHON) tests/partmgr/qemu-test.py $(OVMF) $(BUILD)/partmgr.efi

clean:
	rm -rf $(BUILD)

.PHONY: all test docs qemu-test clean

-include $(EFI_OBJ:.o=.d)
