#!/usr/bin/env python3
"""Renders the Inter font into src/partmgr/font_inter.bin, the glyphs partmgr
draws its graphical interface with (decision P24).

partmgr has no font engine: every glyph is rendered here once, at each size
and weight, into a grey-level image of 4 bits per pixel, and partmgr only
blends those images on the screen. The file is committed, so building
partmgr needs neither this tool nor the font; run it only to change the sizes
or the characters (needs Pillow built with FreeType and Raqm):

  tools/gen-font.py                 download Inter 4.1 (checked) and render
  tools/gen-font.py --zip FILE      use a copy of Inter-4.1.zip already here

The format, all little-endian:

  header   "PMF1", u16 faces, u16 glyphs, u32 offset of the code points
  face     u8 pixel size, u8 bold, u8 ascent, u8 descent, u8 line height,
           3 bytes of padding, u32 offset of the glyphs, u32 offset of the
           kerning pairs, u32 number of pairs
  points   u32 per glyph, ascending
  glyph    u16 advance in 1/64 pixel, i8 left, i8 top (from the baseline,
           negative above it), u8 width, u8 height, u16 padding, u32 offset
           of the pixels: rows of ceil(width / 2) bytes, the left pixel in
           the low 4 bits, 0 transparent, 15 opaque
  pair     u8 first, u8 second (ASCII), i16 adjustment in 1/64 pixel
"""
import hashlib
import io
import os
import pathlib
import struct
import sys
import urllib.request
import zipfile

from PIL import Image, ImageDraw, ImageFont, features

ROOT = pathlib.Path(__file__).resolve().parent.parent
OUT = ROOT / "src" / "partmgr" / "font_inter.bin"
URL = "https://github.com/rsms/inter/releases/download/v4.1/Inter-4.1.zip"
SHA256 = "9883fdd4a49d4fb66bd8177ba6625ef9a64aa45899767dde3d36aa425756b11e"
FILES = {False: "extras/ttf/Inter-Regular.ttf", True: "extras/ttf/Inter-SemiBold.ttf"}

# pixel sizes: the screen's size chooses one (decision P23)
SIZES = (16, 20, 26, 32)
# ASCII, Latin-1, and the punctuation and arrows the screens use
POINTS = sorted(set(range(0x20, 0x7F)) | set(range(0xA0, 0x100)) |
                {0x2013, 0x2014, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2026,
                 0x2190, 0x2191, 0x2192, 0x2193, 0x2212, 0x2264, 0x2265})


def inter_zip(argv):
    if "--zip" in argv:
        data = open(argv[argv.index("--zip") + 1], "rb").read()
    else:
        print("downloading %s" % URL)
        data = urllib.request.urlopen(URL).read()
    if hashlib.sha256(data).hexdigest() != SHA256:
        sys.exit("gen-font: Inter-4.1.zip does not match its SHA-256")
    return zipfile.ZipFile(io.BytesIO(data))


def pack4(img):
    """Rows of 4-bit grey levels, two pixels per byte, the left one low."""
    w, h = img.size
    px = img.load()
    out = bytearray()
    for y in range(h):
        row = [(px[x, y] * 15 + 127) // 255 for x in range(w)]
        row += [0] * (w % 2)
        out += bytes(row[i] | row[i + 1] << 4 for i in range(0, len(row), 2))
    return bytes(out)


def missing(font, ch):
    """True when the font has no glyph for CH: FreeType draws the same box as
    for a private-use character."""
    def picture(c):
        img = Image.new("L", (64, 64), 0)
        ImageDraw.Draw(img).text((8, 8), c, font=font, fill=255)
        return img.tobytes()
    return ch != " " and picture(ch) == picture("\uE000")


def face(ttf, size, points):
    font = ImageFont.truetype(io.BytesIO(ttf), size, layout_engine=ImageFont.Layout.BASIC)
    shaped = ImageFont.truetype(io.BytesIO(ttf), size, layout_engine=ImageFont.Layout.RAQM)
    ascent, descent = font.getmetrics()
    glyphs, pixels = [], bytearray()
    for cp in points:
        ch = chr(cp)
        # the advance as Raqm lays text out: unrounded, as in the approved mockup
        adv = round(shaped.getlength(ch) * 64)
        x0, y0, x1, y1 = font.getbbox(ch, anchor="ls")
        w, h = max(0, x1 - x0), max(0, y1 - y0)
        data = b""
        if w and h:
            img = Image.new("L", (w, h), 0)
            ImageDraw.Draw(img).text((-x0, -y0), ch, font=font, fill=255, anchor="ls")
            data = pack4(img)
        glyphs.append((adv, x0, y0, w, h, len(pixels)))
        pixels += data
    # kerning between ASCII letters, digits and punctuation, as Raqm lays out pairs
    pairs = []
    for a in range(0x21, 0x7F):
        la = shaped.getlength(chr(a))
        for b in range(0x21, 0x7F):
            k = round((shaped.getlength(chr(a) + chr(b)) - la - shaped.getlength(chr(b))) * 64)
            if abs(k) >= 8:  # an eighth of a pixel or more
                pairs.append((a, b, k))
    line = ascent + descent + max(2, size // 5)
    return (size, ascent, descent, line), glyphs, bytes(pixels), pairs


def main(argv):
    if not features.check("raqm"):
        sys.exit("gen-font: Pillow was built without Raqm (needed for kerning)")
    z = inter_zip(argv)
    # only the characters both weights have; partmgr draws "?" for the others
    probe = [ImageFont.truetype(io.BytesIO(z.read(FILES[b])), 20) for b in (False, True)]
    points = [cp for cp in POINTS if not any(missing(f, chr(cp)) for f in probe)]
    dropped = sorted(set(POINTS) - set(points))
    if dropped:
        print("not in Inter: " + " ".join("U+%04X" % cp for cp in dropped))
    faces = []
    for size in SIZES:
        for bold in (False, True):
            faces.append((bold,) + face(z.read(FILES[bold]), size, points))

    head = 12
    face_rec = 20
    off = head + face_rec * len(faces)
    points_off = off
    off += 4 * len(points)
    layout = []
    for bold, metrics, glyphs, pixels, pairs in faces:
        glyphs_off = off
        off += 12 * len(glyphs)
        pairs_off = off
        off += 4 * len(pairs)
        pixels_off = off
        off += len(pixels)
        layout.append((glyphs_off, pairs_off, pixels_off))

    out = bytearray(b"PMF1" + struct.pack("<HHI", len(faces), len(points), points_off))
    for (bold, (size, asc, desc, line), glyphs, pixels, pairs), (go, po, _) in zip(faces, layout):
        out += struct.pack("<BBBBB3xIII", size, int(bold), asc, desc, line, go, po, len(pairs))
    out += b"".join(struct.pack("<I", cp) for cp in points)
    for (bold, metrics, glyphs, pixels, pairs), (go, po, xo) in zip(faces, layout):
        assert len(out) == go
        for adv, x0, y0, w, h, p in glyphs:
            out += struct.pack("<HbbBBxxI", adv, x0, y0, w, h, xo + p)
        for a, b, k in pairs:
            out += struct.pack("<BBh", a, b, k)
        out += pixels
    OUT.write_bytes(bytes(out))
    print("%s: %d bytes, %d faces, %d glyphs each" % (OUT.relative_to(ROOT), len(out), len(faces), len(points)))


if __name__ == "__main__":
    main(sys.argv[1:])
