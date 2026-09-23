# EFI Partition Manager — Design decisions and project history

This document records **why** EFI Partition Manager is the way it is: the
decisions taken while designing and building it, the alternatives that were
considered, and the history of the project. It complements the
[user manual](partmgr-user-manual.html) (what partmgr does) and the
[technical manual](partmgr-developer-manual.html) (how it is built).

Decisions were discussed with the project owner one at a time, in Italian;
where a decision came from the owner, the original words are quoted.

**Format of each decision:** context → options → decision → consequences.
Status is *accepted* unless noted otherwise.

---

## Contents

1. [Origin](#1-origin)
2. [Decisions](#2-decisions)
3. [History](#3-history)
4. [Open questions](#4-open-questions)

---

## 1. Origin

The owner wanted a partition manager that works in the UEFI firmware, before
any operating system: people who prepare disks and service sticks usually boot
a whole Linux live system just to partition. A first design as a command with
subcommands (`partmgr create blk2 gpt esp:512M linux:rest`) was rejected as
"decisamente criptico"; a `parted`-like form with one `add` per partition
followed, and it kept growing. The owner chose a different shape: a program of
its own with a full-screen interface (P1).

---

## 2. Decisions

### P1. A program with a full-screen interface

- **Decision.** Owner: "potremmo creare un partmgr.efi e dotare questo di una
  comoda tui".
- **No command line.** Only the full-screen interface: partmgr is a utility,
  not a scripting tool. A proposal for `list`/`backup`/`restore` on the command
  line, for scripts, was declined.

### P2. An independent project

- partmgr began inside a larger repository and moved to one of its own when
  its releases had to follow its own version.
- **Options.** (a) stay, with independent release tags; (b) a repository of
  its own. **Decision:** (b). The history of partmgr's files was carried over
  with `git filter-branch`.
- **Consequences.** The project is independent: its code, documentation and
  releases refer to no other project (owner: "i 2 progetti sono
  indipendenti"). Its platform layer, runtime, UEFI definitions and build tools
  are its own code.

### P3. Name, repository, visibility, licence

- **Name.** Owner: "EFI PARTITION MANAGER", for the project and the
  repository. GitHub names cannot contain spaces: the repository is
  `EFI_PARTITION_MANAGER`, with underscores (owner's choice).
- **The file** keeps the short name `partmgr.efi`, easy to type at a shell
  prompt; the title bar says "EFI Partition Manager 0.1.0".
- **Public** from the start (owner's choice), with a clear note in the README
  that real hardware has not been tried yet.
- **Licence:** Apache 2.0 with the Commons Clause: free to use, share and
  modify, not to sell; commercial licences by opening an issue.

### P4. Functions

- Show disks and partitions; delete the whole table; create an empty GPT or MBR
  table; add a partition in any free space; delete one partition; change a
  partition's type and name; set the MBR active flag; back up the partition
  table to a file and restore it; wipe a partition.
- Out of scope: resizing, moving, formatting, and any change to data inside a
  partition other than wiping.
- **Words.** To *delete* a partition removes it from the table; to *wipe* it
  destroys its contents — in the owner's Italian *eliminare* and *cancellare*.
  "Zap", the first name proposed for wiping, was dropped: in `gdisk`/`sgdisk`
  it means destroying the partition *table*.

### P5. GPT and MBR, MBR complete

Both formats are read and created. Owner, on creating MBR tables without
extended partitions: "sarebbe una contraddizione". So MBR includes extended and
logical partitions (adding on an MBR disk asks Primary or Logical and creates
the extended partition when needed), the active flag and the CHS fields.

### P6. A new table starts from zero

Creating a table is destructive: the whole MBR sector is rebuilt, its 440-byte
boot code area is zeroed (the GPT protective MBR included), and the disk gets
new identifiers. Owner: "la gestione dell'mbr e' roba da bootloader, che non
riguarda partmgr". Keeping the old boot code, or writing an own MBR boot
program, were rejected.

### P7. Changes are written all together

As in `cfdisk` and `gdisk`, the screen shows the table as it will be and marks
unwritten changes; nothing reaches the disk until **Write**. Quitting without
Write leaves the disk untouched. Restore and wipe cannot be staged: they run at
once, each after its own warning.

### P8. Safety

- Before every destructive operation a very readable warning, then the disk
  name must be typed.
- **No dry run**: the confirmation already shows what will be written.
- **No automatic backup**: owner, "se uno vuole fa' prima il backup". Backup is
  an explicit function.
- The disk the program was started from is shown but always read-only, with no
  override.
- **Secure Boot:** writes are allowed. Writing a partition table is a disk
  write and does not get around the protection of the firmware.

### P9. Wipe

Two passes over the whole partition: random data, then zeros (owner's choice).
It runs at once, with its own warning and confirmation, a progress bar and Esc
to stop. The manual says that on SSD, NVMe and USB flash overwriting does not
guarantee that the old data is gone; the drives' own erase commands work only
on whole disks and are left out. The extended partition cannot be wiped (it
holds the logical partitions), and no partition can while the table has
unwritten changes.

### P10. Interface

Screen 1 lists the disks; screen 2 shows the partitions and the free space in
disk order, with a key bar at the bottom. Types are chosen from a list, never
typed as codes. Disks are named `blkN`, N counting every block device of the
firmware sorted by device path, so a disk keeps its name from one start to the
next.

### P11. New partition: start and size

Owner's choice over start and end. The start defaults to the beginning of the
selected free space, the size to all of it (`rest`), with 1 MiB alignment and
sizes such as `512M` or `20G`.

### P12. Professional and friendly

Owner: "voglio che l'app abbia un'interfaccia professionale ma user-friendly".
Every operation that takes time shows its progress; for the wipe a **progress
bar is mandatory**, with the pass, the percentage, the amount written, the
speed and the time left.

### P13. Two manuals of its own

Owner: partmgr "merita 2 manuali dedicati a lui": a user manual and a
technical manual, complete, with the same care as the code.

### P14. Distribution

`partmgr.efi` is published on this repository's releases, with its checksum,
on tags `vX.Y.Z` of its own version.

### P15. Tests against independent tools

The table code knows no firmware: it reaches disks through read and write
functions, so it runs on disk image files on Linux, where every table it
writes is read back by `sfdisk` and `parted`, damaged images are read, backups
are restored byte for byte, and an independent Python check reads MBR chains
and CHS fields. The program itself is driven with keys inside QEMU/OVMF and the
disks are checked afterwards. The tests were verified by breaking the code on
purpose.

---

## 3. History

All the work took place on 2026-09-23.

| Phase | What happened |
|---|---|
| **1. Design** | A command-line design, dropped; `partmgr.efi` designed with a full-screen interface (P1, P4–P12). |
| **2. Reading** | `ptable.c`: GPT with both copies checked, MBR with the chain of logical partitions, notes about damaged tables; tested against `sfdisk --json` on images, then on 13 kinds of damage. CI's `sfdisk` (util-linux 2.39) lacked `--sector-size`: the 4096-byte cases are skipped there. |
| **3. Writing** | `ptedit.c` and `ptwrite.c`: changes, free space, GPT and MBR writing, backup and restore. A real defect was found by the tests: space freed by deleting the first logical partition could not be used again; the rule for the records of logical partitions was changed. |
| **4. The program** | `partmgr.efi` with its platform layer, the disks found through block I/O, the screens read-only; tested in QEMU through the serial console. |
| **5. Interface** | Every change, Write, backup and restore on the screen; two manuals. |
| **6. Wipe** | Two passes with a progress bar; tested on images and, with a slowed-down disk, in QEMU. |
| **7. Own repository** | The history of partmgr's files carried into this repository, the project named EFI Partition Manager (P2, P3). |

---

## 4. Open questions

| Topic | Status |
|---|---|
| Real hardware | Not tried yet. The owner will try it when possible; reports are asked for with an issue template. |
| First release | 0.1.0, published as a pre-release: releases stay pre-releases while real hardware has not been tried (0.x versions). |
| Disk names | The kind comes from the device path (NVMe, SATA, USB...); the model name of the disk is not shown yet. |
