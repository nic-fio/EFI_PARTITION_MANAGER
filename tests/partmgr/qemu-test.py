#!/usr/bin/env python3
r"""partmgr.efi inside QEMU/OVMF: the program as firmware runs it.

Boots partmgr.efi from a virtual FAT disk (as \EFI\BOOT\BOOTX64.EFI, so the
firmware starts it), attaches test disks made with sfdisk (a GPT one and an MBR
one with logical partitions) and drives partmgr with keys sent through the
QEMU monitor. What partmgr draws reaches the serial console too; the test
waits there for the lines it expects. Parts 1 and 2 run QEMU without a video
card: no graphics screen, so partmgr shows the text screens (its fallback).

1. Looking: the disks numbered by block device,
   the disk partmgr was started from marked read-only, the partitions and free
   areas of each disk, the return to the firmware - and the disks unchanged,
   byte for byte.
2. Changing: the boot disk refuses changes; on the GPT disk a new partition,
   a rename and a delete, leaving with changes asks first, Write asks Y/N
   (Enter does nothing there, N cancels); then the new partition is wiped, and the wipe of a bigger one is
   stopped with Esc (the GPT disk's writes are slowed down so that there is
   time); on the MBR disk a logical partition and the active flag, a backup
   to a file on a writable FAT disk, Write, delete the table, restore.
   Afterwards, outside QEMU, sfdisk must find exactly the result - the GPT
   disk changed as asked, the MBR disk as it was before (the backup was taken
   before anything was written) - and the data must be right: the wiped
   partition all zeros, the stopped one overwritten only at its start, the
   others untouched.
3. Drawing: gfxdemo.efi draws the test picture of the graphical interface on
   the graphics screen, at the firmware's resolution and at 1024 x 768; QEMU's
   screen must be the picture gfxtool draws on Linux, pixel for pixel. The
   picture is announced on the serial port, which the interface writes to.
4. Pointing: with QEMU's USB mouse, which OVMF has no driver for, partmgr's
   own driver must take it; the mouse, moved through the monitor, must bring
   the pointer exactly where expected (clicks are reported with the position,
   and the screen must be the test picture with the arrow there), and stop at
   the edges. A USB tablet (left out, decision P21) and no USB device at all
   must find no pointer.
5. The window: partmgr with a graphics screen and a USB mouse shows its
   window, described on the serial port: the disks (the boot disk read only,
   the first one it may change opened), the rows of each disk, the buttons.
   Keys: the rows, Tab between the disks and the rows, Page Up and Page Down,
   F5, an action that changes nothing. Mouse: a click on a disk, on
   a row, on a block of the bar, on a button; a button under the pointer is
   highlighted. Esc quits; the disks are unchanged, byte for byte.
6. Changing in the window: the journey of part 2 again, with the mouse and
   the keys and the window's dialogs - the boot disk refuses; on the GPT disk a
   new partition, a rename, a delete; quitting and changing the disk with
   changes ask; Write asks (Enter does nothing, No cancels, Yes writes); a wipe,
   and a second one stopped with the Stop button; on the MBR disk a logical
   partition chosen from the list with a click, the active flag, a backup,
   Write, delete the table, restore. Then the same checks as part 2.
7. Sizes: the window at 640 x 480, 1024 x 768, 1920 x 1080 and 3200 x 1800 -
   the font chosen by the height, the buttons inside the screen, the colours
   on the screen, a click on a disk; at 3200 x 1800 the window is drawn at
   half the resolution and every pixel doubled.

    tests/partmgr/qemu-test.py OVMF.fd build/partmgr.efi build/tests/gfxdemo.efi build/tests/gfxtool
"""
import hashlib
import json
import os
import re
import shutil
import socket
import subprocess
import sys
import tempfile
import time

OVMF, PARTMGR, GFXDEMO, GFXTOOL = sys.argv[1:5]
# the version the title bar must show, from the source
VERSION = re.search(r'PARTMGR_VERSION "([^"]+)"',
                    open(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "src", "partmgr",
                                      "partmgr.h")).read()).group(1)
SFDISK = shutil.which("sfdisk") or "/sbin/sfdisk"
WORK = tempfile.mkdtemp(prefix="pmqemu-")
failures = []
count = 0
KEYS = {' ': 'spc', '\n': 'ret', '\\': 'backslash', '.': 'dot', ':': 'shift-semicolon', '-': 'minus'}


def check(name, cond, detail=""):
    global count
    count += 1
    if not cond:
        failures.append("%s: %s" % (name, detail))


def sfdisk_json(path):
    out = subprocess.run([SFDISK, "--json", path], capture_output=True, text=True, check=True)
    t = json.loads(out.stdout)["partitiontable"]
    t.pop("device", None)
    for p in t.get("partitions", []):
        p["node"] = p["node"][len(path):]
    return t


def digest(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def disk(name, mb, script):
    p = os.path.join(WORK, name)
    with open(p, "wb") as f:
        f.truncate(mb * 1024 * 1024)
    subprocess.run([SFDISK, "-q", p], input=script.encode(), check=True, stdout=subprocess.DEVNULL)
    return p


class Qemu:
    def __init__(self, name, disks, slow=(), efi=PARTMGR, extra=()):
        esp = os.path.join(WORK, name)
        os.makedirs(os.path.join(esp, "EFI", "BOOT"))
        shutil.copy(efi, os.path.join(esp, "EFI", "BOOT", "BOOTX64.EFI"))
        self.log = os.path.join(WORK, name + ".log")
        mon = os.path.join(WORK, name + ".monitor")
        drives = ["-drive", "if=virtio,format=raw,readonly=on,file=fat:" + esp]
        for d in disks:
            drives += ["-drive", "if=virtio,format=raw,file=" + d +
                       (",throttling.bps-write=60000000" if d in slow else "")]
        kvm = ["-accel", "kvm"] if os.access("/dev/kvm", os.W_OK) else []
        self.proc = subprocess.Popen(
            ["qemu-system-x86_64"] + kvm + ["-m", "512", "-machine", "q35", "-bios", OVMF] + drives +
            ["-net", "none", "-display", "none", "-serial", "file:" + self.log,
             "-monitor", "unix:%s,server,nowait" % mon, "-no-reboot"] + list(extra),
            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        for _ in range(300):
            if os.path.exists(mon):
                break
            time.sleep(0.1)
        self.mon = socket.socket(socket.AF_UNIX)
        self.mon.connect(mon)
        self.mon.setblocking(False)
        self.seen = 0

    def text(self):
        """The serial output so far, without the terminal control sequences and
        with every run of spaces and line breaks made one space, so that a
        dialog text wrapped over two lines still matches."""
        try:
            raw = open(self.log, "rb").read().decode("utf-8", "replace")
        except FileNotFoundError:
            return ""
        return re.sub(r"\s+", " ", re.sub(r"\x1b\[[0-9;?]*[A-Za-z]", " ", raw))

    def wait(self, what, timeout=30):
        """Waits until WHAT (a regular expression) appears after the last match.
        The screen is drawn top to bottom: rows of the list come before the
        message line below them."""
        end = time.time() + timeout
        while time.time() < end:
            t = self.text()
            m = re.search(what, t[self.seen:])
            if m:
                self.seen += m.end()
                return True
            time.sleep(0.2)
        return False

    def key(self, name):
        self.mon.sendall(("sendkey %s\n" % name).encode())
        time.sleep(0.15)
        try:
            while self.mon.recv(65536):
                pass
        except BlockingIOError:
            pass

    def monitor(self, command):
        self.mon.sendall((command + "\n").encode())
        time.sleep(0.1)
        try:
            while self.mon.recv(65536):
                pass
        except BlockingIOError:
            pass

    def screendump(self, path):
        """QEMU's screen as a PPM file; waits until it is written."""
        self.mon.sendall(("screendump %s\n" % path).encode())
        end = time.time() + 20
        size = -1
        while time.time() < end:
            if os.path.exists(path) and os.path.getsize(path) == size and size > 0:
                break
            size = os.path.getsize(path) if os.path.exists(path) else -1
            time.sleep(0.3)
        try:
            while self.mon.recv(65536):
                pass
        except BlockingIOError:
            pass

    def type(self, text):
        for ch in text:
            self.key("shift-" + ch.lower() if ch.isupper() else KEYS.get(ch, ch))

    def stop(self):
        self.proc.kill()
        self.proc.wait()



def verify_changes(prefix, gpt, mbr, mbr_before, P1, P3, P4):
    """After the journey of changes, outside QEMU: the GPT disk as asked, its
    data right, the MBR disk as it was before (restored from its backup)."""
    def c(name, cond, detail=""):
        check(prefix + name, cond, detail)
    # the GPT disk as asked
    g = sfdisk_json(gpt)
    got = [(p["node"], p["start"], p["size"], p.get("name", "")) for p in g["partitions"]]
    c("gpt result", got == [("1", 2048, 204800, "EFI system"), ("3", 239616, 409600, "Root"),
                                ("4", 649216, 40960, "Test part")], str(got))
    c("gpt type", g["partitions"][2]["type"].upper() == "0FC63DAF-8483-4772-8E79-3D69D8477DE4", "")
    for d in (gpt, mbr):
        out = subprocess.run([SFDISK, "--verify", d], capture_output=True, text=True)
        c("verify " + os.path.basename(d), out.returncode == 0 and "No errors detected" in out.stdout,
              out.stdout + out.stderr)
    # the data: partition 4 all zeros, partition 3 overwritten only at its start, partition 1 untouched
    def blocks(first, blocks_n):
        with open(gpt, "rb") as f:
            f.seek(first * 512)
            return f.read(blocks_n * 512)
    c("wiped partition is zeros", blocks(*P4) == bytes(P4[1] * 512), "partition 4 is not all zeros")
    head, tail = blocks(P3[0], 2048), blocks(P3[0] + P3[1] - 2048, 2048)
    c("stopped wipe: start overwritten", head != bytes([0x33]) * len(head), "partition 3 was not touched")
    c("stopped wipe: end untouched", tail == bytes([0x33]) * len(tail), "the wipe was not stopped")
    c("other partition untouched", blocks(*P1) == bytes([0x11]) * (P1[1] * 512), "partition 1 changed")
    # the MBR disk as it was: the backup was made before writing
    after = sfdisk_json(mbr)
    c("mbr restored", after == mbr_before, "\n%s\n%s" % (mbr_before, after))

try:
    gpt = disk("gpt.img", 512, 'label: gpt\nsize=100M, type=U, name="EFI system"\n'
               'size=16M, type=E3C9E316-0B5C-4DB8-817D-F92DF00215AE\nsize=200M, type=L, name="Linux root"\n')
    mbr = disk("mbr.img", 512, "label: dos\n1 : start=2048, size=102400, type=c, bootable\n"
               "2 : start=104448, size=614400, type=5\n5 : start=106496, size=204800, type=83\n"
               "6 : start=397312, size=81920, type=82\n")
    before = {d: digest(d) for d in (gpt, mbr)}
    TEXT = ("-vga", "none")  # no graphics screen: the text screens
    q = Qemu("look", [gpt, mbr], extra=TEXT)
    try:
        # screen 1: the boot disk (the FAT one: blk0 or blk1, as the PCI slots fall
        # without a video card), blk3 and blk7 the test disks
        check("disk list", q.wait(r"EFI Partition Manager " + re.escape(VERSION) + r" +Select a disk", 90), "partmgr did not start")
        check("boot disk", q.wait(r"blk\d +disk +504\.0 MiB +MBR +1 partition +started from here: read only"),
              "the boot disk is not marked read-only")
        check("gpt disk listed", q.wait(r"blk3 +disk +512\.0 MiB +GPT +3 partitions"), "blk3 missing")
        check("mbr disk listed", q.wait(r"blk7 +disk +512\.0 MiB +MBR +3 partitions"), "blk7 missing")
        # screen 2 on the GPT disk
        q.key("down")
        q.key("ret")
        check("gpt title", q.wait(r"blk3 +disk +512\.0 MiB +GPT"), "no title")
        check("gpt partition 1", q.wait(r"1 +1\.0 MiB +100\.0 MiB +EFI system +EFI system"), "")
        check("gpt partition 2", q.wait(r"2 +101\.0 MiB +16\.0 MiB +Microsoft reserved"), "")
        check("gpt partition 3", q.wait(r"3 +117\.0 MiB +200\.0 MiB +Linux filesystem +Linux root"), "")
        check("gpt free space", q.wait(r"- +317\.0 MiB +194\.9 MiB +free space"), "")
        # screen 2 on the MBR disk
        q.key("esc")
        q.key("down")
        q.key("ret")
        check("mbr title", q.wait(r"blk7 +disk +512\.0 MiB +MBR"), "no title")
        check("mbr active", q.wait(r"1 +1\.0 MiB +50\.0 MiB +FAT32 \(LBA\) +active"), "")
        check("mbr extended", q.wait(r"2 +51\.0 MiB +300\.0 MiB +Extended"), "")
        check("mbr logical 5", q.wait(r"5 +52\.0 MiB +100\.0 MiB +Linux +logical"), "")
        check("mbr free inside", q.wait(r"- +153\.0 MiB +40\.9 MiB +free space \(for logical partitions\)"), "")
        check("mbr logical 6", q.wait(r"6 +194\.0 MiB +40\.0 MiB +Linux swap +logical"), "")
        check("mbr free outside", q.wait(r"- +351\.0 MiB +161\.0 MiB +free space(?! \(for)"), "")
        # back to the list, quit, and the firmware goes on (to its setup menu)
        q.key("esc")
        q.key("q")
        check("back to the firmware", q.wait(r"starting Boot\d+ \"UiApp\""), "partmgr did not return")
    finally:
        q.stop()
    # looking does not write: the disks are identical, byte for byte
    for d in (gpt, mbr):
        check("untouched", digest(d) == before[d], os.path.basename(d) + " changed")

    # ------------------------------------------------------------ changing
    mbr_before = sfdisk_json(mbr)
    work = os.path.join(WORK, "work.img")          # a writable FAT volume for the backup file
    mkfs = shutil.which("mkfs.fat") or "/sbin/mkfs.fat"
    subprocess.run([mkfs, "-C", "-n", "WORK", work, "65536"], check=True, stdout=subprocess.DEVNULL)
    # patterns in the data of the GPT disk: partition 1, partition 3, and the free
    # space where the new partition 4 will be
    P1, P3, P4 = (2048, 204800), (239616, 409600), (649216, 40960)
    for (first, blocks_n), byte in ((P1, 0x11), (P3, 0x33), (P4, 0x44)):
        with open(gpt, "r+b") as f:
            f.seek(first * 512)
            f.write(bytes([byte]) * (blocks_n * 512))
    q = Qemu("change", [gpt, mbr, work], slow=(gpt,), extra=TEXT)
    try:
        check("list", q.wait(r"Select a disk", 90), "partmgr did not start")
        # the boot disk (first row) cannot be changed
        q.key("ret")
        check("boot disk opened", q.wait(r"blk\d +disk +504\.0 MiB"), "")
        q.key("z")
        check("boot disk refuses", q.wait(r"cannot be changed"), "no refusal")
        q.key("spc")
        q.key("esc")
        # GPT disk: a new partition in the free space at the end
        q.key("down")
        q.key("ret")
        check("gpt opened", q.wait(r"blk3 +disk +512\.0 MiB +GPT"), "")
        q.key("end")
        q.key("n")
        check("start asked", q.wait(r"Start \(free:"), "")
        q.key("ret")                                  # the proposed start, 317 MiB
        check("size asked", q.wait(r"Size \(512M, 20G\.\.\., rest = "), "")
        q.type("20M\n")
        check("type asked", q.wait(r"Type EFI system BIOS boot"), "")
        for _ in range(7):                            # EFI system ... Linux filesystem
            q.key("down")
        q.key("ret")
        check("name asked", q.wait(r"Name \(optional"), "")
        q.type("Test part\n")
        check("marked", q.wait(r"4\* +317\.0 MiB +20\.0 MiB +Linux filesystem +Test part"), "no * on the new partition")
        check("added", q.wait(r"Partition 4 added"), "")
        # rename partition 3, delete partition 2
        q.key("up")
        q.key("r")
        check("rename asked", q.wait(r"Rename"), "")
        q.type("Root\n")
        check("renamed", q.wait(r"Partition 3 renamed"), "")
        q.key("up")
        q.key("d")
        check("deleted", q.wait(r"Partition 2 deleted"), "")
        # leaving with changes asks; No stays
        q.key("esc")
        check("leave asks", q.wait(r"Discard the unwritten changes\? \(Y/N\)"), "")
        q.key("n")
        # Write asks Y/N: Enter does nothing there, N cancels, Y writes
        q.key("ret")
        check("write asks", q.wait(r"Write the GPT table to blk3 \(512\.0 MiB\)\? \(Y/N\)"), "")
        q.key("ret")
        q.key("n")
        check("n cancels", q.wait(r"Nothing was written\."), "Enter or N wrote the table")
        q.key("ret")
        check("write asks again", q.wait(r"Write the GPT table to blk3"), "")
        q.key("y")
        check("written", q.wait(r"Written\."), "")
        # wipe partition 4 (rows: 1, free, 3, 4, free)
        q.key("home")
        for _ in range(3):
            q.key("down")
        q.key("w")
        check("wipe asks", q.wait(r"Wipe partition 4 of blk3 \(Linux filesystem, 20\.0 MiB, \"Test part\"\)\? \(Y/N\)"), "")
        q.key("y")
        check("pass 1 shown", q.wait(r"Pass 1 of 2: random data"), "")
        check("pass 2 shown", q.wait(r"Pass 2 of 2: zeros"), "")
        check("progress shown", q.wait(r"\d+\.\d MiB of 20\.0 MiB"), "")
        check("wiped", q.wait(r"Partition 4 wiped \(20\.0 MiB, 2 passes"), "")
        # wipe partition 3 (200 MiB) and stop it with Esc
        q.key("up")
        q.key("w")
        check("second wipe warns", q.wait(r"Wipe partition 3 of blk3"), "")
        q.key("y")
        check("second wipe runs", q.wait(r"Pass 1 of 2: random data"), "")
        q.key("esc")
        check("stop asks", q.wait(r"Stop the wipe"), "")
        q.key("y")
        check("stopped", q.wait(r"Wipe of partition 3 stopped in pass 1"), "")
        q.key("esc")
        # MBR disk: a logical partition in the free space after partition 5
        check("list again", q.wait(r"Select a disk"), "")
        q.key("down")
        q.key("ret")
        check("mbr opened", q.wait(r"blk7 +disk +512\.0 MiB +MBR"), "")
        for _ in range(3):                            # 1, 2 (extended), 5, free
            q.key("down")
        q.key("n")
        check("mbr start asked", q.wait(r"Start \(free:"), "")
        q.key("ret")
        q.type("30M\n")
        check("mbr type asked", q.wait(r"Type FAT32 \(LBA\) NTFS/exFAT"), "")
        q.key("down")
        q.key("down")                                 # FAT32 (LBA), NTFS/exFAT, Linux
        q.key("ret")
        check("renumbered", q.wait(r"7\* +194\.0 MiB +40\.0 MiB +Linux swap +logical"), "the old 6 is not 7")
        check("logical added", q.wait(r"Partition 6 added"), "")
        # partition 5 becomes the active one
        q.key("up")
        q.key("a")
        check("active", q.wait(r"Partition 5 active\."), "")
        # backup of the disk as it is now (before writing), to the writable FAT disk
        q.key("b")
        check("backup asks", q.wait(r"Volumes: fs0 \(ro\), fs1"), "")
        q.key("ret")
        check("backup saved", q.wait(r"Saved fs1:\\partmgr-blk7\.bin \(\d+ bytes\)"), "")
        q.key("ret")
        check("mbr write asks", q.wait(r"Write the MBR table to blk7"), "")
        q.key("y")
        check("mbr written", q.wait(r"Written\."), "")
        # delete the table, write, then restore the backup
        q.key("x")
        check("table gone", q.wait(r"Partition table deleted\."), "")
        q.key("ret")
        check("delete asks", q.wait(r"Delete the partition table of blk7 \(512\.0 MiB\)\? \(Y/N\)"), "")
        q.key("y")
        check("no table shown", q.wait(r"No partition table"), "")
        check("deleted written", q.wait(r"Written\."), "")
        q.key("s")
        check("restore asks for the file", q.wait(r"Restore from file:"), "")
        q.key("ret")
        check("restore warns", q.wait(r"Restore fs1:\\partmgr-blk7\.bin to blk7\?"), "")
        q.key("y")
        check("restored", q.wait(r"Restored from fs1:\\partmgr-blk7\.bin"), "")
        q.key("esc")
        q.key("q")
        check("back to the firmware again", q.wait(r"starting Boot\d+ \"UiApp\""), "")
    finally:
        q.stop()
    verify_changes("", gpt, mbr, mbr_before, P1, P3, P4)

    # ------------------------------------------------------------ drawing
    def ppm(path):
        """Width, height and the RGB bytes of a binary PPM."""
        data = open(path, "rb").read()
        fields, pos = [], 0
        while len(fields) < 4:
            while data[pos:pos + 1].isspace():
                pos += 1
            start = pos
            while not data[pos:pos + 1].isspace():
                pos += 1
            fields.append(data[start:pos])
        return int(fields[1]), int(fields[2]), data[pos + 1:]

    for name, extra, size in (("gfx-default", (), None),
                              ("gfx-1024", ("-vga", "none", "-device", "VGA,edid=on,xres=1024,yres=768"),
                               (1024, 768))):
        q = Qemu(name, [], efi=GFXDEMO, extra=extra)
        try:
            shown = q.wait(r"gfxdemo: shown (\d+)x(\d+)", 90)
            check(name + ": shown", shown, "gfxdemo did not show its picture")
            m = re.findall(r"gfxdemo: shown (\d+)x(\d+)", q.text())
            if shown and m:
                w, h = int(m[-1][0]), int(m[-1][1])
                if size:
                    check(name + ": resolution", (w, h) == size, "%dx%d" % (w, h))
                time.sleep(0.5)
                screen = os.path.join(WORK, name + "-screen.ppm")
                q.screendump(screen)
                ref = os.path.join(WORK, name + "-ref.ppm")
                subprocess.run([GFXTOOL, "scene", str(w), str(h), ref], check=True)
                sw, sh, sp = ppm(screen)
                rw, rh, rp = ppm(ref)
                diff = sum(1 for i in range(0, min(len(sp), len(rp)), 3) if sp[i:i + 3] != rp[i:i + 3])
                check(name + ": the screen is the picture", (sw, sh) == (rw, rh) and sp == rp,
                      "screen %dx%d, picture %dx%d, %d pixels differ" % (sw, sh, rw, rh, diff))
                q.key("ret")
                check(name + ": closed", q.wait(r"gfxdemo: closed"), "gfxdemo did not end")
        finally:
            q.stop()

    # ------------------------------------------------------------ pointing
    usb = ("-device", "qemu-xhci")
    q = Qemu("mouse", [], efi=GFXDEMO, extra=usb + ("-device", "usb-mouse"))
    try:
        check("mouse: taken by partmgr's driver",
              q.wait(r"gfxdemo: pointer: firmware 0, partmgr's USB driver 1", 90), q.text()[-300:])
        m = re.findall(r"gfxdemo: shown (\d+)x(\d+)", q.text())
        w, h = (int(m[-1][0]), int(m[-1][1])) if m else (1280, 800)
        # from the middle, 11 steps of (-40, -25)
        for _ in range(11):
            q.monitor("mouse_move -40 -25")
        q.monitor("mouse_button 1")
        x, y = w // 2 - 440, h // 2 - 275
        check("mouse: click where expected", q.wait(r"gfxdemo: click at %d,%d" % (x, y), 20), q.text()[-300:])
        q.monitor("mouse_button 0")
        check("mouse: release", q.wait(r"gfxdemo: release at %d,%d" % (x, y), 20), "")
        time.sleep(0.5)
        screen, ref = os.path.join(WORK, "mouse-screen.ppm"), os.path.join(WORK, "mouse-ref.ppm")
        q.screendump(screen)
        subprocess.run([GFXTOOL, "scene", str(w), str(h), ref, str(x), str(y)], check=True)
        sw, sh, sp = ppm(screen)
        rw, rh, rp = ppm(ref)
        diff = sum(1 for i in range(0, min(len(sp), len(rp)), 3) if sp[i:i + 3] != rp[i:i + 3])
        check("mouse: the arrow on the picture", (sw, sh) == (rw, rh) and sp == rp, "%d pixels differ" % diff)
        # far past the top left corner: the pointer stops at 0,0
        for _ in range(12):
            q.monitor("mouse_move -127 -127")
        q.monitor("mouse_button 1")
        check("mouse: stops at the edge", q.wait(r"gfxdemo: click at 0,0", 20), q.text()[-300:])
        q.monitor("mouse_button 0")
        q.key("ret")
        check("mouse: closed", q.wait(r"gfxdemo: closed"), "")
    finally:
        q.stop()
    for name, extra in (("tablet", usb + ("-device", "usb-tablet")), ("no-usb", ())):
        q = Qemu(name, [], efi=GFXDEMO, extra=extra)
        try:
            check(name + ": no pointer", q.wait(r"gfxdemo: pointer: firmware 0, partmgr's USB driver 0", 90),
                  q.text()[-300:])
            q.key("ret")
            check(name + ": closed", q.wait(r"gfxdemo: closed"), "")
        finally:
            q.stop()

    # ------------------------------------------------------------ the window
    gpt = disk("gpt-gui.img", 512, 'label: gpt\nsize=100M, type=U, name="EFI system"\n'
               'size=16M, type=E3C9E316-0B5C-4DB8-817D-F92DF00215AE\nsize=200M, type=L, name="Linux root"\n')
    mbr = disk("mbr-gui.img", 512, "label: dos\n1 : start=2048, size=102400, type=c, bootable\n"
               "2 : start=104448, size=614400, type=5\n5 : start=106496, size=204800, type=83\n"
               "6 : start=397312, size=81920, type=82\n")
    before = {d: digest(d) for d in (gpt, mbr)}
    q = Qemu("window", [gpt, mbr], extra=usb + ("-device", "usb-mouse"))

    def frame():
        """The last complete description of the window."""
        t = q.text()
        end = t.rfind("gui: shown")
        start = t.rfind("gui: window", 0, end)
        return t[start:end] if start >= 0 and end >= 0 else ""

    def where(pattern):
        """The position logged after PATTERN in the last frame ("... at X,Y")."""
        m = re.search(pattern + r".*? at (\d+),(\d+)", frame())
        return (int(m.group(1)), int(m.group(2))) if m else None

    def move_to(x, y):
        """Moves QEMU's mouse until partmgr's pointer is at X, Y (in the
        window's pixels: the mouse moves in the screen's, SCALE times as many)."""
        m = re.findall(r"gui: pointer (\d+),(\d+)", q.text())
        px, py = (int(m[-1][0]), int(m[-1][1])) if m else (0, 0)
        sc = re.findall(r"gui: window \d+x\d+ font \d+ scale (\d+)", q.text())
        scale = int(sc[-1]) if sc else 1
        while (px, py) != (x, y):
            dx, dy = max(-60, min(60, x - px)), max(-60, min(60, y - py))
            q.monitor("mouse_move %d %d" % (dx * scale, dy * scale))
            px, py = px + dx, py + dy
        return q.wait(r"gui: pointer %d,%d" % (x, y), 10)

    def click_at(pos):
        if not pos or not move_to(*pos):
            return False
        q.monitor("mouse_button 1")
        q.monitor("mouse_button 0")
        return True

    try:
        check("window: pointer", q.wait(r"gui: pointer devices: firmware 0, partmgr's USB driver 1", 90), q.text()[-300:])
        check("window: shown", q.wait(r"gui: window 1280x800 font 20"), "")
        check("window: boot disk", q.wait(r"gui: disk blk1 disk 504\.0 MiB MBR, 1 partition, "
                                          r"started from here: read only at"), "")
        check("window: gpt disk opened", q.wait(r"gui: disk blk3 disk 512\.0 MiB GPT, 3 partitions < at"), "")
        check("window: mbr disk", q.wait(r"gui: disk blk7 disk 512\.0 MiB MBR, 3 partitions at"), "")
        check("window: shows gpt", q.wait(r"gui: shows blk3 disk 512\.0 MiB GPT"), "")
        check("window: row 1", q.wait(r"gui: row 1 1\.0 MiB 100\.0 MiB EFI system EFI system < at"), "")
        check("window: row 2", q.wait(r"gui: row 2 101\.0 MiB 16\.0 MiB Microsoft reserved at"), "")
        check("window: row 3", q.wait(r"gui: row 3 117\.0 MiB 200\.0 MiB Linux filesystem Linux root at"), "")
        check("window: free", q.wait(r"gui: row - 317\.0 MiB 194\.9 MiB free space at"), "")
        check("window: buttons", q.wait(r"gui: button Esc Quit at \d+,\d+ .*gui: button F5 Rescan at"), "")
        check("window: shown whole", q.wait(r"gui: shown"), "")
        # what is on the screen: the title bar and the selected row
        time.sleep(0.5)
        screen = os.path.join(WORK, "window.ppm")
        q.screendump(screen)
        sw, sh, sp = ppm(screen)
        pixel = lambda x, y: tuple(sp[3 * (y * sw + x):3 * (y * sw + x) + 3])
        row1 = where(r"gui: row 1 ")
        check("window: title bar on the screen", (sw, sh) == (1280, 800) and pixel(5, 5) == (0x1B, 0x4F, 0x9C),
              str(pixel(5, 5)))
        check("window: selected row on the screen", row1 and pixel(row1[0], row1[1]) == (0x1F, 0x6F, 0xEB), str(row1))
        # keys
        q.key("down")
        check("key: down", q.wait(r"gui: row 2 101\.0 MiB 16\.0 MiB Microsoft reserved < at"), "")
        q.key("tab")
        check("key: tab to the disks", q.wait(r"gui: focus disks"), "")
        q.key("down")
        check("key: next disk", q.wait(r"gui: shows blk7 disk 512\.0 MiB MBR"), "")
        check("mbr: logical", q.wait(r"gui: row 5 52\.0 MiB 100\.0 MiB Linux logical at"), "")
        check("mbr: free inside", q.wait(r"gui: row - 153\.0 MiB 40\.9 MiB free space \(for logical partitions\) at"), "")
        check("mbr: swap", q.wait(r"gui: row 6 194\.0 MiB 40\.0 MiB Linux swap logical at"), "")
        q.key("up")
        check("key: previous disk", q.wait(r"gui: shows blk3 disk"), "")
        q.key("tab")
        check("key: tab to the rows", q.wait(r"gui: focus partitions"), "")
        q.key("pgdn")
        check("key: page down", q.wait(r"gui: shows blk7 disk"), "")
        q.key("pgup")
        check("key: page up", q.wait(r"gui: shows blk3 disk"), "")
        q.key("f5")
        check("key: F5", q.wait(r"gui: message Disks read again\."), "")
        q.key("n")                                    # on a partition: nothing to add there
        check("key: an action", q.wait(r"gui: message Select a free area\."), "")
        # the mouse
        check("click: a disk", click_at(where(r"gui: disk blk7 ")) and q.wait(r"gui: shows blk7 disk"), "")
        check("click: focus on the disks", q.wait(r"gui: focus disks"), "")
        check("click: a row", click_at(where(r"gui: row 6 ")) and
              q.wait(r"gui: row 6 194\.0 MiB 40\.0 MiB Linux swap logical < at"), "")
        check("click: focus on the rows", q.wait(r"gui: focus partitions"), "")
        m = re.search(r"gui: row 5 .*? bar (\d+),(\d+)", frame())
        check("click: a block of the bar", m and click_at((int(m.group(1)), int(m.group(2)))) and
              q.wait(r"gui: row 5 52\.0 MiB 100\.0 MiB Linux logical < at"), "")
        btn = where(r"gui: button N New")
        # the window is drawn again, the button highlighted, as soon as the pointer
        # enters it: the last complete description says so
        check("hover: a button", btn and move_to(*btn) and
              re.search(r"gui: button N New at \d+,\d+ hover", frame()), frame()[-400:])
        q.monitor("mouse_button 1")
        q.monitor("mouse_button 0")
        check("click: a button is its key", q.wait(r"gui: message Select a free area\."), "")
        check("click: Quit", click_at(where(r"gui: button Esc Quit")) and q.wait(r"gui: closed"), "")
        check("window: back to the firmware", q.wait(r"starting Boot\d+ \"UiApp\""), "")
    finally:
        q.stop()
    for d in (gpt, mbr):
        check("window: untouched", digest(d) == before[d], os.path.basename(d) + " changed")

    # ------------------------------------------------------------ changing in the window
    gpt = disk("gpt-w.img", 512, 'label: gpt\nsize=100M, type=U, name="EFI system"\n'
               'size=16M, type=E3C9E316-0B5C-4DB8-817D-F92DF00215AE\nsize=200M, type=L, name="Linux root"\n')
    mbr = disk("mbr-w.img", 512, "label: dos\n1 : start=2048, size=102400, type=c, bootable\n"
               "2 : start=104448, size=614400, type=5\n5 : start=106496, size=204800, type=83\n"
               "6 : start=397312, size=81920, type=82\n")
    mbr_before = sfdisk_json(mbr)
    work = os.path.join(WORK, "work-w.img")
    subprocess.run([mkfs, "-C", "-n", "WORK", work, "65536"], check=True, stdout=subprocess.DEVNULL)
    for (first, blocks_n), byte in ((P1, 0x11), (P3, 0x33), (P4, 0x44)):
        with open(gpt, "r+b") as f:
            f.seek(first * 512)
            f.write(bytes([byte]) * (blocks_n * 512))
    q = Qemu("gui-change", [gpt, mbr, work], slow=(gpt,), extra=usb + ("-device", "usb-mouse"))

    def dbutton(label):
        """Where the last dialog's button LABEL is."""
        m = re.findall(r"gui: dbutton \S+ %s at (\d+),(\d+)" % label, q.text())
        return (int(m[-1][0]), int(m[-1][1])) if m else None

    W = "w: "
    try:
        check(W + "started", q.wait(r"gui: shows blk3 disk 512\.0 MiB GPT", 90), q.text()[-300:])
        q.wait(r"gui: shown")
        # the boot disk refuses
        check(W + "boot disk opened", click_at(where(r"gui: disk blk1 ")) and q.wait(r"gui: shows blk1 disk"), "")
        q.key("z")
        check(W + "boot disk refuses", q.wait(r"gui: dialog Read only: Partmgr was started from this disk: "
                                             r"it cannot be changed\."), "")
        check(W + "OK closes", click_at(dbutton("OK")) and q.wait(r"gui: dialog closed"), "")
        check(W + "gpt opened", click_at(where(r"gui: disk blk3 ")) and q.wait(r"gui: shows blk3 disk"), "")
        q.key("tab")                                  # the keys to the rows
        # a new partition in the free space at the end, from the N button
        q.key("end")
        check(W + "free selected", q.wait(r"gui: row - 317\.0 MiB 194\.9 MiB free space <"), "")
        check(W + "start asked", click_at(where(r"gui: button N New")) and
              q.wait(r"gui: dialog New partition: Start \(free: 317 MiB to 511\.9 MiB\):"), "")
        check(W + "start proposed", q.wait(r"gui: field 317 MiB"), "")
        q.key("ret")
        check(W + "size asked", q.wait(r"gui: dialog New partition: Size \(512M, 20G\.\.\., rest = 194\.9 MiB\):"), "")
        q.type("20M\n")
        check(W + "type asked", q.wait(r"gui: dialog Type: .*gui: item EFI system at \d+,\d+ <"), "")
        for _ in range(7):                            # EFI system ... Linux filesystem
            q.key("down")
        q.key("ret")
        check(W + "name asked", q.wait(r"gui: dialog New partition: Name \(optional, 36 characters\):"), "")
        q.type("Test part\n")
        check(W + "marked", q.wait(r"gui: row 4\* 317\.0 MiB 20\.0 MiB Linux filesystem Test part <"), "")
        check(W + "added", q.wait(r"gui: message Partition 4 added\."), "")
        # rename partition 3 (the proposed name replaced), delete partition 2 with the D button
        check(W + "row 3", click_at(where(r"gui: row 3 ")) and q.wait(r"gui: row 3 .*<"), "")
        q.key("r")
        check(W + "rename asked", q.wait(r"gui: dialog Rename: Name \(36 characters\):.*gui: field Linux root"), "")
        q.type("Root\n")
        check(W + "renamed", q.wait(r"gui: message Partition 3 renamed\."), "")
        check(W + "row 2", click_at(where(r"gui: row 2 ")) and q.wait(r"gui: row 2 .*<"), "")
        check(W + "deleted", click_at(where(r"gui: button D Delete")) and q.wait(r"gui: message Partition 2 deleted\."), "")
        # quitting and changing the disk with changes ask; No stays
        q.key("esc")
        check(W + "quit asks", q.wait(r"gui: dialog Quit \(warning\): Discard the unwritten changes\? \(Y/N\)"), "")
        check(W + "No stays", click_at(dbutton("No")) and q.wait(r"gui: dialog closed") and
              q.wait(r"gui: shows blk3 disk 512\.0 MiB GPT \*"), "")
        q.key("pgdn")
        check(W + "change asks", q.wait(r"gui: dialog Change disk \(warning\): Discard the unwritten changes"), "")
        q.key("n")
        check(W + "still the gpt disk", q.wait(r"gui: shows blk3 disk 512\.0 MiB GPT \*"), "")
        # Write asks: Enter does nothing, No cancels, Yes writes
        check(W + "write asks", click_at(where(r"gui: button Enter Write")) and
              q.wait(r"gui: dialog Write \(warning\): Write the GPT table to blk3 \(512\.0 MiB\)\? \(Y/N\)"), "")
        q.key("ret")
        time.sleep(1)
        check(W + "Enter does nothing", "gui: dialog closed" not in q.text()[q.text().rfind("gui: dialog Write"):], "")
        q.key("n")
        check(W + "N cancels", q.wait(r"gui: message Nothing was written\."), "")
        q.key("ret")
        check(W + "write asks again", q.wait(r"gui: dialog Write \(warning\): Write the GPT table"), "")
        check(W + "Yes writes", click_at(dbutton("Yes")) and q.wait(r"gui: message Written\."), "")
        # wipe partition 4
        check(W + "row 4", click_at(where(r"gui: row 4 317")) and q.wait(r"gui: row 4 317.*<"), "")
        q.key("w")
        check(W + "wipe asks", q.wait(r'gui: dialog Wipe \(warning\): Wipe partition 4 of blk3 \(Linux filesystem, '
                                      r'20\.0 MiB, "Test part"\)\? \(Y/N\)'), "")
        q.key("y")
        check(W + "pass 1 shown", q.wait(r"gui: wipe Wiping blk3 partition 4 \(20\.0 MiB\) pass 1"), "")
        check(W + "pass 2 done", q.wait(r"gui: wipe Wiping blk3 partition 4 \(20\.0 MiB\) pass 2 100% 20\.0 MiB of 20\.0 MiB"), "")
        check(W + "wiped", q.wait(r"gui: message Partition 4 wiped \(20\.0 MiB, 2 passes"), "")
        # wipe partition 3 (200 MiB, writes slowed down) and stop it with the Stop button
        check(W + "row 3 again", click_at(where(r"gui: row 3 ")) and q.wait(r"gui: row 3 .*<"), "")
        check(W + "second wipe asks", click_at(where(r"gui: button W Wipe")) and
              q.wait(r"gui: dialog Wipe \(warning\): Wipe partition 3 of blk3"), "")
        check(W + "Yes wipes", click_at(dbutton("Yes")) and
              q.wait(r"gui: wipe Wiping blk3 partition 3 \(200\.0 MiB\) pass 1"), "")
        m = re.findall(r"gui: wipe Wiping blk3 partition 3 .*? stop at (\d+),(\d+)", q.text())
        check(W + "Stop asks", m and click_at((int(m[-1][0]), int(m[-1][1]))) and
              q.wait(r"gui: dialog Wipe \(warning\): Stop the wipe\? \(Y/N\)"), "")
        q.key("y")
        check(W + "stopped", q.wait(r"gui: message Wipe of partition 3 stopped in pass 1"), "")
        # MBR disk: a logical partition in the free space after partition 5, its type clicked
        q.key("pgdn")
        check(W + "mbr opened", q.wait(r"gui: shows blk7 disk 512\.0 MiB MBR"), "")
        q.wait(r"gui: shown")
        check(W + "free inside", click_at(where(r"gui: row - 153\.0 MiB")) and q.wait(r"gui: row - 153\.0 MiB.*<"), "")
        q.key("n")
        check(W + "mbr start asked", q.wait(r"gui: dialog New partition: Start \(free:"), "")
        q.key("ret")
        q.type("30M\n")
        check(W + "mbr type asked", q.wait(r"gui: dialog Type: .*gui: item Linux at"), "")
        q.wait(r"gui: dialog shown")
        m = re.findall(r"gui: item Linux at (\d+),(\d+)", q.text())
        check(W + "type clicked", m and click_at((int(m[-1][0]), int(m[-1][1]))) and
              q.wait(r"gui: row 7\* 194\.0 MiB 40\.0 MiB Linux swap logical"), "the old 6 is not 7")
        check(W + "logical added", q.wait(r"gui: message Partition 6 added\."), "")
        check(W + "row 5", click_at(where(r"gui: row 5 ")) and q.wait(r"gui: row 5 .*<"), "")
        q.key("a")
        check(W + "active", q.wait(r"gui: message Partition 5 active\."), "")
        # backup before writing, write, delete the table, restore
        check(W + "backup asks", click_at(where(r"gui: button B Backup")) and
              q.wait(r"gui: dialog Backup: Backup of the table on the disk, to file: Volumes: fs0 \(ro\), fs1"), "")
        q.key("ret")
        check(W + "backup saved", q.wait(r"gui: message Saved fs1:\\partmgr-blk7\.bin \(\d+ bytes\)"), "")
        q.key("ret")
        check(W + "mbr write asks", q.wait(r"gui: dialog Write \(warning\): Write the MBR table to blk7"), "")
        q.key("y")
        check(W + "mbr written", q.wait(r"gui: message Written\."), "")
        check(W + "table gone", click_at(where(r"gui: button X Delete table")) and
              q.wait(r"gui: message Partition table deleted\."), "")
        q.key("ret")
        check(W + "delete asks", q.wait(r"gui: dialog Write \(warning\): Delete the partition table of blk7 "
                                        r"\(512\.0 MiB\)\? \(Y/N\)"), "")
        q.key("y")
        check(W + "deleted written", q.wait(r"gui: shows blk7 disk 512\.0 MiB no table.*gui: message Written\."), "")
        check(W + "restore asks", click_at(where(r"gui: button S Restore")) and
              q.wait(r"gui: dialog Restore: Restore from file:"), "")
        q.key("ret")
        check(W + "restore warns", q.wait(r"gui: dialog Restore \(warning\): Restore fs1:\\partmgr-blk7\.bin to blk7\?"), "")
        check(W + "restored", click_at(dbutton("Yes")) and q.wait(r"gui: message Restored from fs1:\\partmgr-blk7\.bin"), "")
        check(W + "quit", click_at(where(r"gui: button Esc Quit")) and q.wait(r"gui: closed"), "")
        check(W + "back to the firmware", q.wait(r"starting Boot\d+ \"UiApp\""), "")
    finally:
        q.stop()
    verify_changes(W, gpt, mbr, mbr_before, P1, P3, P4)

    # ------------------------------------------------------------ sizes
    gpt = disk("gpt-s.img", 512, 'label: gpt\nsize=100M, type=U, name="EFI system"\n')
    mbr = disk("mbr-s.img", 512, "label: dos\n1 : start=2048, size=102400, type=c, bootable\n")
    for xres, yres, win, font, scale in ((640, 480, "640x480", 16, 1), (1024, 768, "1024x768", 20, 1),
                                        (1920, 1080, "1920x1080", 26, 1), (3200, 1800, "1600x900", 20, 2)):
        S = "%dx%d: " % (xres, yres)
        vga = "VGA,edid=on,xres=%d,yres=%d%s" % (xres, yres, ",vgamem_mb=64" if xres * yres > 4000000 else "")
        q = Qemu("size-%d" % xres, [gpt, mbr], extra=("-vga", "none", "-device", vga) + usb + ("-device", "usb-mouse"))
        try:
            check(S + "window", q.wait(r"gui: window %s font %d scale %d" % (win, font, scale), 90), q.text()[-200:])
            q.wait(r"gui: shown")
            width = int(win.split("x")[0])
            m = re.search(r"gui: buttons (\d+) rows, right edge (\d+)", frame())
            check(S + "buttons inside the screen", m and int(m.group(2)) <= width, m.group(0) if m else "")
            time.sleep(0.5)
            screen = os.path.join(WORK, "size-%d.ppm" % xres)
            q.screendump(screen)
            sw, sh, sp = ppm(screen)
            pixel = lambda x, y: sp[3 * (y * sw + x):3 * (y * sw + x) + 3]
            check(S + "the screen", (sw, sh) == (xres, yres) and pixel(5, 5) == bytes((0x1B, 0x4F, 0x9C)),
                  "%dx%d %s" % (sw, sh, pixel(5, 5)))
            if scale == 2:
                doubled = all(pixel(x, y) == pixel(x + 1, y) == pixel(x, y + 1) == pixel(x + 1, y + 1)
                              for y in range(0, sh, 14) for x in range(0, sw, 6))
                check(S + "every pixel doubled", doubled, "")
            # the MBR test disk, whatever its number (blkN counts the partitions too)
            m = re.search(r"gui: disk (blk\d+) disk 512\.0 MiB MBR, 1 partition at", frame())
            name = m.group(1) if m else "?"
            check(S + "a click on a disk", click_at(where(r"gui: disk %s " % name)) and
                  q.wait(r"gui: shows %s disk 512\.0 MiB MBR" % name), frame()[-300:])
            q.key("esc")
            check(S + "closed", q.wait(r"gui: closed"), "")
        finally:
            q.stop()
finally:
    shutil.rmtree(WORK, ignore_errors=True)

for f in failures:
    print("FAIL " + f)
print("partmgr qemu tests: %d checks, %d failed" % (count, len(failures)))
sys.exit(1 if failures else 0)
