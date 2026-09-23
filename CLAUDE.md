# Working on EFI Partition Manager

How this project is developed: the agreements, the decisions that are settled,
and what to check before committing. A fresh clone plus this file is the whole
working context.

## How we work

- **Talk to the user in Italian.** The code, the comments and the documentation
  are in English; the conversation is not.
- **Agree the design before writing code.** On a new area, propose the options
  and wait for a decision.
- **One decision at a time, in plain language.** Long lists of simultaneous
  questions do not work here.
- **Verify, do not claim.** Every statement about behaviour comes from a run:
  the tests, QEMU, a screenshot. "It should work" is not a result.
- **The documentation matters as much as the code.** A feature is not done
  until the manuals describe it.
- **Words:** to *delete* a partition removes it from the table; to *wipe* it
  destroys its contents (in the user's Italian: *eliminare* / *cancellare*).

## Settled decisions — do not reopen them

The reasoning for each is in [docs/decisions-and-history.md](docs/decisions-and-history.md).

| Decision | |
|---|---|
| **An independent project** | Own repository, releases and manuals; no reference to any other project, anywhere. |
| **One file, `partmgr.efi`** | Project name "EFI Partition Manager"; the file keeps the short name. |
| **Full-screen only** | No command line, no scripting. |
| **GPT and MBR, MBR complete** | Extended and logical partitions, active flag, CHS fields. |
| **A new table starts from zero** | New identifiers, no boot code: boot code belongs to boot loaders. |
| **Changes are written together** | Nothing reaches the disk until Write; restore and wipe run at once, each after its own warning. |
| **Y/N confirmation, no dry run, no automatic backup** | A very readable red warning, "Confirm? (Y/N)"; Enter does nothing there. Backup is the user's choice. |
| **The boot disk is read-only** | No override. |
| **Writes allowed under Secure Boot** | A partition table is a disk write, not a way around the protection. |
| **Wipe: random data, then zeros** | A progress bar is mandatory; Esc asks. Not the extended partition, not with changes pending. |
| **New partition: start + size** | Sizes in binary units, `rest` for the free space. |
| **No EDK2 code** | Own UEFI definitions, runtime, platform layer and build tools. |
| **Apache 2.0 + Commons Clause** | Free to use and share, selling needs a commercial licence, asked for by opening an issue. |
| **Light documentation only** | No dark themes in the manuals. |

## The repository

| Where | What |
|---|---|
| `src/partmgr` | The program: table code (`ptable.c`, `ptedit.c`, `ptwrite.c`), screens, disks. |
| `src/pal`, `src/lib`, `include` | Platform layer, runtime and UEFI definitions. |
| `tools/` | `elf2efi.py` (ELF → PE32+), `efi.lds`, `gen-docs.py`, `setup-dev.sh`. |
| `tests/partmgr` | `pttool.c` (the table code on disk images), `run-tests.py`, `qemu-test.py`. |
| `docs/` | The user and technical manuals, the decisions log, the assets. Published with GitHub Pages. |
| `build/` | Products only, never committed. |

## Before committing

1. `make test` — the partition table tests against sfdisk and parted, and the
   documentation check (the source map of the technical manual must list every
   file; `make docs` rewrites its line counts).
2. `make qemu-test` for anything the program does on the screen or on disks.
3. Check the exit status of every step, not a filtered pipeline: a `| tail`
   hides a failure.
4. Commits use the repository-local identity set by `tools/setup-dev.sh`.

Releases: bump `PARTMGR_VERSION` in `src/partmgr/partmgr.h`, the version in
both manuals, `docs/index.html` and the issue template, and take the
screenshots again (the title bar shows the version); then tag `vX.Y.Z`. The
annotated tag message becomes the release notes; CI builds `partmgr.efi` and
publishes it. **0.x versions are published as pre-releases**: real hardware has
not been tried yet.

## Where the project stands

Version 0.1.0, released as a pre-release (tag v0.1.0). Tested in QEMU with OVMF and on disk images
checked by sfdisk and parted. **Not tried on real hardware yet**: the user will
do it when they can. Open: real hardware, a first release.
