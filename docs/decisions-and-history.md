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
  prompt; the title bar says "EFI Partition Manager" and the version.
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

- Before every destructive operation a very readable warning, answered with
  **Y or N**. At first the disk name had to be typed; after trying it, the
  owner found it strange and asked for something more immediate: "basterebbe
  una semplice finestra di conferma con Confirm (Y/N)?". Enter does nothing
  in that window, so a key pressed twice cannot write.
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
next. After the owner tried the first release ("le voci sembrano un po'
sparpagliate"), the key bar became two fixed rows: **Partition:** (N, D, T, R,
A, W) and **Disk:** (Enter, Z, X, B, S, Esc). Keys that did not apply were at
first dimmed in place; the owner found the mix of highlighted and grey letters
inconsistent ("si riesce a fare un prodotto fatto bene?"). Now every key is
drawn the same way, white on blue, and a key that does not apply answers with
one short line saying why ("Select a partition first.").

### P11. New partition: start and size

Owner's choice over start and end. The start defaults to the beginning of the
selected free space, the size to all of it (`rest`), with 1 MiB alignment and
sizes such as `512M` or `20G`.

### P12. Professional and friendly

Owner: "voglio che l'app abbia un'interfaccia professionale ma user-friendly".
Every operation that takes time shows its progress; for the wipe a **progress
bar is mandatory**, with the pass, the percentage, the amount written, the
speed and the time left.

### P16. Terse screens for expert users

Owner: "l'utente che usa partmgr e' un utente evoluto, che sa' come entrare in
una shell efi e quindi sa' cosa sta' facendo". The screens drop every
explanation: confirmations are one red line ending in (Y/N) (for example
"Write the GPT table to blk3 (512.0 MiB)? (Y/N)"), fields have just a label
("Size (512M, 20G..., rest = 194.9 MiB):"), messages are short, and the list of
disks has no help line. The explanations live in the user manual. The Y/N
confirmation of every write, and the question when leaving with unwritten
changes, stay.

### P17. A text mode of 100 columns

Owner, after seeing the screen at 100 columns: "le 100 colonne sono molto piu'
leggibili. E se facessimo in modo che partmgr come prima cosa impostasse lo
schermo proprio in quella modalita'?". At start partmgr switches to the
narrowest text mode the firmware offers with at least 100 columns and 25 rows
(the narrowest gives the largest characters), and on leaving puts back the mode
it found. Where there is no such mode it stays as it is, and the key bars are
drawn narrower to fit 80 columns.

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

### P18. A graphical interface, text as the fallback

After release 0.1.2 the owner noted that partmgr could have had a graphical
interface: UEFI firmware offers a graphics screen (the Graphics Output
Protocol) besides the text console.

- **Options.** (a) graphics when the firmware offers it, the text screens of
  0.1.2 otherwise; (b) graphics only. **Decision:** (a). Machines without a
  graphics screen (a server reached only through a serial console) keep
  working, and so do the text-mode tests.
- **Consequences.** Two ways of drawing over the same table code and the same
  keys. The text fallback keeps the decisions made for it (P10, P16, P17).

### P19. Designed for the mouse, every command on the keyboard too

- **Options.** (a) the keyboard does everything, the mouse is an extra; (b) an
  interface designed for the mouse, with the keyboard as shortcuts; (c) no
  mouse. **Decision:** (b), the owner's choice over the recommended (a).
- **Consequences.** Every command stays reachable from the keyboard with the
  keys of 0.1.2, so a machine without a working pointer is never stuck. Keys
  and buttons that do not apply answer with a short message and are never
  greyed out (P10). Destructive operations keep the red Y/N question (P8), now
  with Yes and No buttons; Enter does nothing there. The interface stays in
  English.

### P20. One window

- **Options.** (a) one window with everything in view; (b) the two screens of
  0.1.2, drawn in graphics. **Decision:** (a).
- **The window** (see the approved mockup, `docs/assets/gui-mockup.png`): the
  disks on the left; on the right the selected disk as a bar to scale, one
  coloured block per partition and the free space hatched, above the table of
  its partitions; a message line; two fixed rows of buttons, **Partition:** and
  **Disk:**, in the order of the key bars of 0.1.2, each button showing its key.
- **Esc quits** (asking first when changes are not written): with one window
  there is no screen to go back to. **F5 reads the disks again**, since R
  already means Rename.

### P21. A mouse driver of its own

OVMF, the firmware used by the tests, has **no mouse driver** at all: no USB
mouse, no USB tablet, no PS/2 mouse (checked on 2026-09-24 in the list of
drivers inside Debian's OVMF 2025.02; QEMU's emulated mouse reaches no
program). Real firmware may lack one too.

- **Decision.** Owner: "potremmo integrare il driver del mouse proprio dentro
  l'app: se un firmware reale supporta il mouse il driver non si attiva e si
  usa quello del firmware, altrimenti si usa il nostro driver".
- **How.** Every firmware already runs the USB bus (keyboards and sticks work)
  and gives each USB interface a USB I/O protocol. partmgr looks for HID
  interfaces of the boot mouse kind, the simple 3-byte reports that every USB
  mouse offers for BIOS use. When a firmware driver already owns the
  interface, partmgr uses the firmware's pointer; otherwise it sets the boot
  protocol and reads the reports itself.
- **Verified** on 2026-09-24 with a probe program in QEMU/OVMF: QEMU's USB
  mouse, on xHCI and on EHCI, moved by the test script, reached the probe, and
  the click fell exactly where expected.
- **Left out.** Tablets and touchscreens (they need the full HID report
  parser): only through the firmware's own driver. PS/2 mice (owner: "il mouse
  ps/2 non si usa piu' da almeno 15 anni").
- **Not testable in QEMU:** the case of a firmware that owns the mouse, since
  OVMF has no mouse driver. Real hardware will tell.

### P22. Tests of the graphical interface

The tests keep driving partmgr inside QEMU/OVMF: keys through the monitor,
mouse moves and clicks through QEMU's USB mouse, which exercises partmgr's own
driver (P21). What partmgr draws is checked on screen captures (QEMU's
`screendump`), and partmgr writes what it shows as text to the serial port,
which the tests read as they do today. A first idea, a test-only pointer
driver fed through a second serial port, worked but was dropped: with P21 the
real code is tested instead.

### P23. The firmware's resolution

- **Options.** (a) keep the resolution the firmware set, usually the native
  one of the monitor, and adapt the layout; (b) always a fixed resolution such
  as 1024×768. **Decision:** (a): a sharp picture on every monitor.
- **Consequences.** The layout is computed from the screen size, and the text
  grows on large screens (sizes of the font chosen by resolution). The tests
  try more than one resolution.

### P24. The font: Inter

- Text in graphics is drawn by partmgr itself, so it carries a font.
  Choosing an existing free font was preferred over drawing one or using the
  firmware's (one small size, not always present).
- **Tried on the mockup:** Spleen and Terminus (bitmap fonts made for
  terminals), DejaVu Sans and Inter (smooth outlines). Owner, on Spleen:
  "certo che i caratteri non sono proprio il massimo"; on the four versions:
  "il mockup-inter mi sembra il migliore tra tutti". **Decision:** Inter.
- **How.** The glyphs are rendered once, at a few sizes, into small grey-level
  images stored in partmgr; partmgr only blends them on the screen, with no
  font engine inside. The glyphs take about 280 KB (four sizes, two weights,
  kerning pairs), so `partmgr.efi` grows from about 70 KB to about 350 KB, more
  than the 200–300 KB first estimated. Letters have different widths: columns
  are aligned by pixel position.
- **Licence.** SIL Open Font License 1.1: it may be bundled with software; its
  copyright notice and licence go in `NOTICE.md`.

### P25. A light look

- **Options.** (a) light; (b) dark; (c) white on blue, as the text screens.
  **Decision:** (a): white and light grey, dark text, blue for the selection
  and the buttons, soft colours for the partitions in the disk bar. It matches
  the manuals, which are light only.
- The mockup was approved with it (owner: "si, approvo").

---

## 3. History

The work took place on 2026-09-23 (phases 1–9) and 2026-09-24 (phase 10).

| Phase | What happened |
|---|---|
| **1. Design** | A command-line design, dropped; `partmgr.efi` designed with a full-screen interface (P1, P4–P12). |
| **2. Reading** | `ptable.c`: GPT with both copies checked, MBR with the chain of logical partitions, notes about damaged tables; tested against `sfdisk --json` on images, then on 13 kinds of damage. CI's `sfdisk` (util-linux 2.39) lacked `--sector-size`: the 4096-byte cases are skipped there. |
| **3. Writing** | `ptedit.c` and `ptwrite.c`: changes, free space, GPT and MBR writing, backup and restore. A real defect was found by the tests: space freed by deleting the first logical partition could not be used again; the rule for the records of logical partitions was changed. |
| **4. The program** | `partmgr.efi` with its platform layer, the disks found through block I/O, the screens read-only; tested in QEMU through the serial console. |
| **5. Interface** | Every change, Write, backup and restore on the screen; two manuals. |
| **6. Wipe** | Two passes with a progress bar; tested on images and, with a slowed-down disk, in QEMU. |
| **7. Own repository** | The history of partmgr's files carried into this repository, the project named EFI Partition Manager (P2, P3). |
| **8. After the first try** | Release 0.1.0 as a pre-release. The owner tried it in QEMU: the key bar was reorganised in two fixed groups, the typed disk name gave way to a Y/N confirmation, and the screens became terse for expert users (P8, P10, P16). Release 0.1.1. |
| **9. A finished look** | Every key drawn the same way, with a message for the keys that do not apply; a text mode of 100 columns at start (P10, P17). Release 0.1.2. |
| **10. A graphical interface, designed** | 2026-09-24. The graphical interface was designed (P18–P25): the graphics screen, the pointer and screen captures were checked in QEMU/OVMF first, which showed that OVMF has no mouse driver; partmgr will carry its own (P21). A mockup, drawn with four fonts, was approved with Inter. |

---

## 4. Open questions

| Topic | Status |
|---|---|
| Real hardware | Not tried yet. The owner will try it when possible; reports are asked for with an issue template. |
| Releases | 0.1.0, 0.1.1 and 0.1.2, published as pre-releases: releases stay pre-releases while real hardware has not been tried (0.x versions). |
| Disk names | The kind comes from the device path (NVMe, SATA, USB...); the model name of the disk is not shown yet. |
| Graphical interface | Designed (P18–P25), not built yet. A firmware that owns the mouse can only be tried on real hardware (P21). |
