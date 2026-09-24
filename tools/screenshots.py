#!/usr/bin/env python3
"""Take the screenshots of the manuals again: boot partmgr.efi in QEMU/OVMF on
test disks, type the keys of each scene through the QEMU monitor and save the
screen, cropped to the 100 x 31 text console, as a PNG.

    make
    tools/screenshots.py [OVMF.fd [OUTDIR]]    # default: docs/assets

Run by hand before a release (the title bar shows the version). Needs
qemu-system-x86_64, OVMF, sfdisk and mkfs.fat; nothing else. The wipe scene
uses a slowed-down disk so that the progress bar is caught half-way; its
speed and time left change from run to run.
"""
import os
import shutil
import socket
import struct
import subprocess
import sys
import tempfile
import time
import zlib

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OVMF = sys.argv[1] if len(sys.argv) > 1 else "/usr/share/ovmf/OVMF.fd"
OUT = sys.argv[2] if len(sys.argv) > 2 else os.path.join(ROOT, "docs", "assets")
EFI = os.path.join(ROOT, "build", "partmgr.efi")
SFDISK = shutil.which("sfdisk") or "/sbin/sfdisk"
MKFS = shutil.which("mkfs.fat") or "/sbin/mkfs.fat"

# At 1024x768 the firmware offers 100 x 31 (8 x 19 pixel cells), centred:
# this box is the text console with a margin of one cell.
CROP = (104, 80, 920, 688)

# Scenes: (disk size of the GPT test disk in MiB, slow disk, steps). A step is
# "key:NAME" (a QEMU sendkey name), "type:TEXT", "sleep:SECONDS" or "shot:NAME".
SCENES = [
    (512, False, ["key:down", "shot:partmgr-list", "key:ret", "key:end", "key:n", "shot:partmgr-start",
                  "key:ret", "type:20M\n"] + ["key:down"] * 7 + ["shot:partmgr-type", "key:ret",
                  "type:Test part\n", "shot:partmgr-disk", "key:ret", "shot:partmgr-write", "key:n",
                  "key:esc", "key:y", "key:down", "key:ret", "shot:partmgr-mbr"]),
    (1300, True, ["key:down", "key:ret", "key:down", "key:down", "key:w", "key:y", "sleep:8",
                  "shot:partmgr-wipe"]),
]

KEYS = {" ": "spc", "\n": "ret", ".": "dot"}


def disk(path, mb, script):
    with open(path, "wb") as f:
        f.truncate(mb * 1024 * 1024)
    subprocess.run([SFDISK, "-q", path], input=script.encode(), check=True, stdout=subprocess.DEVNULL)


def save_png(ppm, png):
    """Crop a binary PPM (what QEMU's screendump writes) and save it as PNG."""
    data = open(ppm, "rb").read()
    fields, pos = [], 0
    while len(fields) < 4:  # magic, width, height, maxval
        while data[pos:pos + 1].isspace():
            pos += 1
        if data[pos:pos + 1] == b"#":
            pos = data.index(b"\n", pos)
            continue
        end = pos
        while not data[end:end + 1].isspace():
            end += 1
        fields.append(data[pos:end])
        pos = end
    pos += 1
    width = int(fields[1])
    x0, y0, x1, y1 = CROP
    rows = b"".join(b"\0" + data[pos + (y * width + x0) * 3:pos + (y * width + x1) * 3] for y in range(y0, y1))

    def chunk(kind, body):
        return struct.pack(">I", len(body)) + kind + body + struct.pack(">I", zlib.crc32(kind + body))

    with open(png, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", x1 - x0, y1 - y0, 8, 2, 0, 0, 0)) +
                chunk(b"IDAT", zlib.compress(rows, 9)) + chunk(b"IEND", b""))


def run(work, gpt_mb, slow, steps):
    esp = os.path.join(work, "esp", "EFI", "BOOT")
    os.makedirs(esp)
    shutil.copy(EFI, os.path.join(esp, "BOOTX64.EFI"))
    gpt, mbr, fat = (os.path.join(work, n) for n in ("gpt.img", "mbr.img", "fat.img"))
    third = "size=1G" if gpt_mb > 1000 else "size=200M"
    disk(gpt, gpt_mb, 'label: gpt\nsize=100M, type=U, name="EFI system"\nsize=16M, '
         'type=E3C9E316-0B5C-4DB8-817D-F92DF00215AE\n%s, type=L, name="Linux root"\n' % third)
    disk(mbr, 512, "label: dos\n1 : start=2048, size=102400, type=c, bootable\n"
         "2 : start=104448, size=614400, type=5\n5 : start=106496, size=204800, type=83\n"
         "6 : start=397312, size=81920, type=82\n")
    subprocess.run([MKFS, "-C", "-n", "WORK", fat, "65536"], check=True, stdout=subprocess.DEVNULL)
    mon, log = os.path.join(work, "m"), os.path.join(work, "serial.log")
    throttle = ",throttling.bps-write=60000000" if slow else ""
    qemu = subprocess.Popen(
        ["qemu-system-x86_64", "-m", "512", "-machine", "q35", "-bios", OVMF,
         "-drive", "if=virtio,format=raw,readonly=on,file=fat:" + os.path.join(work, "esp"),
         "-drive", "if=virtio,format=raw,file=" + gpt + throttle,
         "-drive", "if=virtio,format=raw,file=" + mbr,
         "-drive", "if=virtio,format=raw,file=" + fat,
         "-net", "none", "-display", "none", "-serial", "file:" + log,
         "-vga", "none", "-device", "VGA,edid=on,xres=1024,yres=768",
         "-monitor", "unix:%s,server,nowait" % mon, "-no-reboot"]
        + (["-accel", "kvm"] if os.access("/dev/kvm", os.W_OK) else []),
        stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    try:
        while not os.path.exists(mon):
            time.sleep(0.1)
        s = socket.socket(socket.AF_UNIX)
        s.connect(mon)
        s.setblocking(False)

        def cmd(line):
            s.sendall((line + "\n").encode())
            time.sleep(0.08)
            try:
                while s.recv(65536):
                    pass
            except BlockingIOError:
                pass

        def key(k):
            cmd("sendkey " + k)
            time.sleep(0.15)

        start = time.time()
        while "Select a disk" not in (open(log, "rb").read().decode("utf-8", "replace") if os.path.exists(log) else ""):
            if time.time() - start > 60:
                sys.exit("partmgr did not start (see %s)" % log)
            time.sleep(0.3)
        time.sleep(1.0)
        for step in steps:
            kind, _, arg = step.partition(":")
            if kind == "key":
                key(arg)
            elif kind == "type":
                for ch in arg:
                    key("shift-" + ch.lower() if ch.isupper() else KEYS.get(ch, ch))
            elif kind == "sleep":
                time.sleep(float(arg))
            elif kind == "shot":
                time.sleep(1.0)
                ppm = os.path.join(work, arg + ".ppm")
                cmd("screendump " + ppm)
                time.sleep(0.5)
                save_png(ppm, os.path.join(OUT, arg + ".png"))
                print(os.path.join(OUT, arg + ".png"))
    finally:
        qemu.kill()
        qemu.wait()


def main():
    if not os.path.exists(EFI):
        sys.exit("build/partmgr.efi not found: run make first")
    os.makedirs(OUT, exist_ok=True)
    for gpt_mb, slow, steps in SCENES:
        # a short directory: unix socket paths must stay under 108 bytes
        work = tempfile.mkdtemp(prefix="pmshot")
        try:
            run(work, gpt_mb, slow, steps)
        finally:
            shutil.rmtree(work, ignore_errors=True)


if __name__ == "__main__":
    main()
