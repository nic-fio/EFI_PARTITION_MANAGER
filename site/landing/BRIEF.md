# EFI Partition Manager landing page — brief for the mockups

Twenty mockups of the same landing page, **same content, twenty visual directions**, for
the owner to choose one. The chosen one becomes the page at `https://efipm.nicfio.it`.

## Rules (all mockups)

- One **self-contained HTML file** per mockup: `site/mockups/NN-slug.html` (in
  `/home/nicfio/Documenti/EFI_PARTITION_MANAGER/`). No JavaScript, no external requests
  (no CDN, no Google Fonts, no remote images): CSS in a `<style>`, images inline as
  `data:` URIs, fonts embedded as `data:` URIs from `/usr/share/fonts`, subset with
  `/tmp/claude-1000/-home-nicfio/0e80c400-428a-4147-98e6-10e95186e0f4/scratchpad/ftv/bin/pyftsubset`
  to Basic Latin plus `·—–’“”→©×`; or system font stacks. Local fonts: Cantarell,
  Quicksand, URW Gothic, P052, C059, URW Bookman, Nimbus Sans, Nimbus Sans Narrow, DejaVu
  Sans/Serif/Sans Mono, Liberation Sans/Serif/Mono, Noto Mono. Each file under 1.5 MB
  (the screenshots are PNG; you may convert them to WebP or JPEG, quality ≥ 85, to save
  space, but never alter their content).
- English only. Elegant, professional, appealing; responsive at 1440 px and at 390 px,
  no horizontal scrolling of the page.
- **The logo** (never redrawn or recoloured): `_shared/efipm-logo.png` (700 px wide, the
  whole logo «EFI PARTITION MANAGER», transparent background, dark-blue text: on dark
  backgrounds put it on a light plate) and `_shared/efipm-mark.png` (only the disk with
  the pie, transparent: works on light and dark; on dark backgrounds put the words «EFI
  Partition Manager» next to it in the page's font). Logo colours: blues #0a2a6b to
  #1e88ff, green #2bb59a to #8fd16a.
- **Real screenshots** (taken in QEMU by the project's own tool, version 0.2.1, 1024×768):
  `_shared/partmgr-start.png`, `partmgr-disk.png` (a GPT disk to scale with a new
  partition), `partmgr-mbr.png`, `partmgr-type.png` (choosing a type), `partmgr-write.png`
  (the red Write question), `partmgr-wipe.png` (wipe with progress), and
  `partmgr-text-disk.png` (the text screen, 816×608). Use them as they are, in frames of
  your design; they are the strongest visual of the product. Never fake a screen.
- **Only true statements** (the facts below). Do not invent features, users, numbers,
  testimonials, benchmarks, compatibility claims. Do not write «fast», «battle-tested»,
  «production-ready», «trusted».
- **An independent project:** do not mention any other project of the author, or the
  author's other sites. Only `https://nicfio.it` may appear, small, as the author's site
  in the footer.

## What it is

**EFI Partition Manager** — a partition manager for UEFI firmware, with a window used
with the mouse or the keyboard. One file, `partmgr.efi`: start it from the firmware boot
menu or from a UEFI shell, and prepare disks before any operating system is there.
For people who set up, repair or test machines below the operating system.

Tagline ideas (choose or adapt, keep them true): «Partition disks before any OS is
there.» — «See first, write later.» — «A partition manager that runs in your firmware.»

## Highlights (all true today, version 0.2.1)

- **A window and the mouse:** a graphical window at the screen's own resolution, with
  sharp text from 640 × 480 up (tried up to 3200 × 1800), used with the mouse or the
  keyboard. It drives a USB mouse itself when the firmware cannot; where there is no
  graphics screen, text screens do the same.
- **GPT and MBR**, read and written in full: MBR with primary, extended and logical
  partitions; GPT with both copies checked and, when written, repaired.
- **See first, write later:** every change is shown on the screen, marked with a star,
  and the disk changes only on **Write**, after a red warning confirmed with **Y** or
  **Yes**.
- **Backup and restore** of the partition table to a small file, restored byte for byte.
- **Wipe** a partition: random data, then zeros, with a progress bar, the speed and the
  time left.
- **Readable:** partition types chosen from a list, sizes such as `512M`, `1.5G` or
  `rest`, notes that explain damaged or unusual tables.
- **Safe by design:** the disk partmgr was started from is never written.
- **No EDK2 code:** written from scratch, with its own UEFI definitions, runtime and
  build tools.

Keys (exact, from the window): Partition: `N` New, `D` Delete, `T` Type, `R` Rename,
`A` Active, `W` Wipe. Disk: `Enter` Write, `Z` New table, `X` Delete table, `B` Backup,
`S` Restore, `Esc` Quit, `F5` Rescan. The Write question reads exactly:
`Write the GPT table to blk3 (512.0 MiB)? (Y/N)`.

## Tested — say it honestly (keep it visible)

Tested automatically in QEMU with OVMF firmware, and on disk images whose tables are
checked by `sfdisk` and `parted` (498 partition-table checks and 34 graphics checks, all
passing). **It has not been tried on real hardware yet.** Until it has, use it on disks
whose contents you can afford to lose, and make a backup (key **B**) first.
Call to action: «Tried it on a real machine? Send a report» → `mailto:efipm@nicfio.it`
(subject «EFI Partition Manager hardware report»). A machine where everything worked is
as useful a report as one where nothing did.

## Get it

- **Download** `partmgr.efi` (version 0.2.1, about 782 KB) → `download/partmgr.efi`;
  checksums `download/SHA256SUMS`; licence files `download/LICENSE.txt`,
  `download/NOTICE.txt`.
- Copy `partmgr.efi` to a FAT-formatted USB stick: as `\EFI\BOOT\BOOTX64.EFI`, and choose
  the stick in the firmware boot menu; or next to a UEFI shell, and start it from the
  shell's prompt. With Secure Boot active, sign it with a key the machine trusts first
  (see the user manual).
- Check the download: `sha256sum -c --ignore-missing SHA256SUMS`.

## Documentation

User Manual (`User%20Manual.html`): the screens, every key, backups, wipe, the notes about
damaged tables. Technical Manual (`Technical%20Manual.html`): how partmgr reads, changes
and writes tables, the backup format, the tests. Licence (`licence.html`).

## Licence (free)

**Freeware.** Free to use, also at work and as a tool in paid work; copy it and give it
away unchanged, also in free collections of tools; sign it for Secure Boot. Not allowed
without a written licence: selling it, including it in a product or service that is sold,
modifying it, decompiling it beyond what the law allows — write to `efipm@nicfio.it`.
Small print: «Version 0.2.1 was released under the Apache License 2.0 with the Commons
Clause, and keeps it; the freeware licence applies from the next version.»
No prices, no «Buy», no «Pro».

## Sections (in this order, adapt the layout)

1. Header: logo, links (Features, Screens, Get it, Docs, Licence), a «Download» button (#get).
2. Hero: tagline, one sentence, buttons «Download partmgr.efi» (#get) and «Read the manual»,
   and a real screenshot (partmgr-disk.png) in a frame.
3. Why (short, true): one file, runs before any OS, a real window with the mouse, nothing
   written until you say so.
4. Features (the highlights).
5. Screens: a gallery of the real screenshots with captions (start, disk to scale, type
   list, the red Write question, wipe with progress, the text screen).
6. Safe by design: see first write later, the boot disk never written, backup/restore,
   the exact Write question.
7. Tested, and honest status + hardware-report call to action.
8. Get it: download, the two ways to start it, Secure Boot note, checksum.
9. Documentation.
10. Licence (short).
11. Footer: «EFI Partition Manager · Version 0.2.1 · © 2026 Nicola Fiorillo» and a small
    link to nicfio.it.

## Checking your work

Render with Chrome headless and LOOK at the images:
`google-chrome --headless=new --disable-gpu --hide-scrollbars --window-size=1440,8000 --screenshot=OUT.png file:///path/NN-slug.html`
and `--window-size=390,14000`. Fix overflow, overlaps, contrast, cut text, empty areas.
Preview: `site/mockups/previews/NN-slug.png` (`--window-size=1440,900`). Write only in
`site/mockups/` and in your own scratch subfolder
`/tmp/claude-1000/-home-nicfio/0e80c400-428a-4147-98e6-10e95186e0f4/scratchpad/pm-<letter>/`.
