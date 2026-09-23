# EFI Partition Manager

[![CI](https://github.com/nic-fio/EFI_PARTITION_MANAGER/actions/workflows/ci.yml/badge.svg)](https://github.com/nic-fio/EFI_PARTITION_MANAGER/actions/workflows/ci.yml)
[![Licence: Apache 2.0 with the Commons Clause](https://img.shields.io/badge/licence-Apache%202.0%20with%20Commons%20Clause-blue)](LICENSE)
[Documentation](https://nic-fio.github.io/EFI_PARTITION_MANAGER/) · [Download](https://github.com/nic-fio/EFI_PARTITION_MANAGER/releases/latest)

A full-screen partition manager for UEFI firmware. One file, `partmgr.efi`:
start it from the firmware boot menu or from a UEFI shell, and prepare disks
before any operating system is there.

![The screen of a GPT disk in partmgr: three partitions, a new one marked with a star, the free space, the keys](docs/assets/partmgr-disk.png)

> **Status.** partmgr is new. It is tested automatically in QEMU with OVMF
> firmware and on disk images checked by `sfdisk` and `parted`, but it has not
> been tried on real hardware yet. Until it has, use it on disks whose contents
> you can afford to lose, and make a backup (key **B**) first.
> [Reports](https://github.com/nic-fio/EFI_PARTITION_MANAGER/issues/new?template=hardware-report.yml)
> are very welcome.

## Highlights

- **GPT and MBR**, read and written in full: MBR with primary, extended and
  logical partitions; GPT with both copies checked and, when written, repaired.
- **See first, write later**: every change is shown on the screen, marked with
  a star, and the disk changes only on **Write**, after a red warning confirmed
  with **Y**.
- **Backup and restore** of the partition table to a small file, restored
  byte for byte.
- **Wipe** a partition: random data, then zeros, with a progress bar, the speed
  and the time left.
- **Readable**: partition types chosen from a list, sizes such as `512M`,
  `1.5G` or `rest`, notes that explain damaged or unusual tables.
- **Safe by design**: the disk partmgr was started from is never written.
- **No EDK2 code**: written from scratch, with its own UEFI definitions, runtime
  and build tools.

## Documentation

| Document | For |
|---|---|
| [User manual](https://nic-fio.github.io/EFI_PARTITION_MANAGER/partmgr-user-manual.html) | Using partmgr: the screens, every key, backups, wipe, the notes about damaged tables. |
| [Technical manual](https://nic-fio.github.io/EFI_PARTITION_MANAGER/partmgr-developer-manual.html) | How partmgr reads, changes and writes tables, the backup format, the tests, how to extend it. |
| [Design decisions and history](docs/decisions-and-history.md) | Why partmgr is the way it is. |

## Build and test

```
git clone https://github.com/nic-fio/EFI_PARTITION_MANAGER.git
cd EFI_PARTITION_MANAGER
tools/setup-dev.sh --install    # packages (asks for sudo) and the git identity
make && make test && make qemu-test
```

```
make             # build/partmgr.efi (UEFI x86-64)
make test        # the partition table tests on Linux, the documentation check
make qemu-test   # partmgr.efi inside QEMU/OVMF on test disks
make docs        # the line counts of the technical manual
```

`OVMF=/path/to/OVMF.fd` selects the firmware image. [CLAUDE.md](CLAUDE.md) says how
the project is worked on.

## Install

Copy `partmgr.efi` to a FAT-formatted USB stick:

- as `\EFI\BOOT\BOOTX64.EFI`, and choose the stick in the firmware boot menu;
- or next to a UEFI shell, and start it from the shell's prompt.

With Secure Boot active, sign it with a key the machine trusts first (see the
user manual).

## Licence

Copyright (c) 2026 nic-fio. EFI Partition Manager is released under the
**Apache License 2.0 with the [Commons Clause](LICENSE)**: use it for anything,
at home or at work, free of charge; copy it, modify it and share it, keeping the
notices and saying what you changed. What you may not do is **sell** it, or sell
a product or service whose value comes substantially from it — that needs a
commercial licence ([open an issue](https://github.com/nic-fio/EFI_PARTITION_MANAGER/issues)
to ask for one).

The Commons Clause means the project is not "open source" by the OSI
definition. Third-party components keep their own licences, listed in
[NOTICE.md](NOTICE.md).
