/* A test picture for the drawing code: every primitive of gfx.c and every
 * face of the font, laid out like the window of the graphical interface.
 * gfxtool draws it on Linux and gfxdemo.efi on the screen in QEMU; the test
 * compares the two, pixel by pixel. */
#include "../../src/partmgr/gfx.h"

void gfx_scene(GfxCanvas *c);

enum {
    BG = 0xF3F4F6, WHITE = 0xFFFFFF, TEXT = 0x1F2328, MUTED = 0x6E7781, LINE = 0xD0D7DE,
    BLUE = 0x1F6FEB, BLUE_DARK = 0x1B4F9C, AMBER = 0x9A5B00, GREEN = 0x1A7F37,
};

static int button(GfxCanvas *c, const GfxFont *f, int x, int y, const char *key, const char *label, bool hover)
{
    int kw = gfx_text_width(f, key) + 8, lw = gfx_text_width(f, label);
    int w = kw + lw + 26, h = gfx_line_height(f) + 6;
    gfx_round_rect(c, x, y, w, h, 5, hover ? 0xDDEBFF : WHITE, hover ? 2 : 1, hover ? BLUE : LINE);
    gfx_round_rect(c, x + 6, y + 4, kw, h - 8, 3, BLUE_DARK, 0, 0);
    gfx_text(c, x + 10, y + 3, f, WHITE, key);
    gfx_text(c, x + 6 + kw + 8, y + 3, f, TEXT, label);
    return x + w + 8;
}

void gfx_scene(GfxCanvas *c)
{
    const GfxFont *f = gfx_font(20, false), *fb = gfx_font(20, true);
    int W = c->w, H = c->h, line = gfx_line_height(f);
    gfx_fill(c, 0, 0, W, H, BG);

    /* title bar */
    gfx_fill(c, 0, 0, W, 36, BLUE_DARK);
    gfx_text(c, 14, 3, fb, WHITE, "EFI Partition Manager");
    const char *note = "* = changes not written";
    gfx_text(c, W - 14 - gfx_text_width(f, note), 3, f, 0xFFE08A, note);

    /* the disks */
    int px = 300;
    gfx_fill(c, 0, 37, px, H - 37 - 110, WHITE);
    gfx_fill(c, px, 37, 1, H - 37 - 110, LINE);
    gfx_text(c, 14, 48, fb, TEXT, "Disks");
    static const char *const disks[][3] = { { "blk0", "USB", "7.5 GiB" }, { "blk1", "NVMe", "476.9 GiB" },
                                            { "blk3", "disk", "512.0 MiB" } };
    for (int i = 0, y = 92; i < 3; i++, y += 2 * line + 8) {
        bool sel = i == 2;
        if (sel)
            gfx_fill(c, 0, y, px, 2 * line + 4, BLUE);
        GfxColor fg = sel ? WHITE : TEXT;
        gfx_text(c, 14, y + 2, fb, fg, disks[i][0]);
        gfx_text(c, 80, y + 2, fb, fg, disks[i][1]);
        gfx_text(c, px - 14 - gfx_text_width(fb, disks[i][2]), y + 2, fb, fg, disks[i][2]);
        gfx_text(c, 14, y + 2 + line, f, sel ? 0xDCE8FF : i == 0 ? AMBER : MUTED,
                 i == 0 ? "boot disk, read only" : "GPT, 4 partitions");
    }

    /* the disk bar: blocks, the hatched free space, the selection frame */
    int x0 = px + 16, x1 = W - 16, by = 86, bh = 64;
    gfx_fill(c, x0, by, x1 - x0, bh, 0xE6E8EB);
    gfx_hatch(c, x0, by, x1 - x0, bh, 10, 0xD5D9DE);
    static const struct { int start, size; GfxColor col; const char *label; } parts[] = {
        { 1, 100, 0xF6C28B, "1 EFI system" }, { 101, 16, 0xC9C3E6, "2" }, { 117, 200, 0xA8D5A2, "3 Linux" },
        { 317, 20, 0xA8D5A2, "4*" },
    };
    for (size_t i = 0; i < ARRAY_SIZE(parts); i++) {
        int a = x0 + parts[i].start * (x1 - x0) / 512, b = x0 + (parts[i].start + parts[i].size) * (x1 - x0) / 512;
        gfx_fill(c, a, by, b - a, bh, parts[i].col);
        gfx_frame(c, a, by, b - a, bh, 1, 0x8C959F);
        gfx_clip(c, a, by, b - a, bh); /* a label never leaves its block */
        gfx_text(c, a + 4, by + 4, f, TEXT, parts[i].label);
        gfx_unclip(c);
    }
    int sa = x0 + 317 * (x1 - x0) / 512, sb = x0 + 337 * (x1 - x0) / 512;
    gfx_frame(c, sa - 2, by - 4, sb - sa + 4, bh + 8, 4, BLUE);

    /* the table, with a selected row */
    int ty = 172;
    gfx_fill(c, x0, ty, x1 - x0, line + 4, 0xE5E9F0);
    static const char *const head[] = { "#", "Start", "Size", "Type", "Name" };
    static const int col[] = { 0, 60, 200, 340, 600 };
    for (int i = 0; i < 5; i++)
        gfx_text(c, x0 + 10 + col[i], ty + 2, fb, TEXT, head[i]);
    static const char *const rows[][5] = {
        { "1", "1.0 MiB", "100.0 MiB", "EFI system", "Système — “EFI” …" },
        { "4*", "317.0 MiB", "20.0 MiB", "Linux filesystem", "Test part ÀÉÎÕÜ ñ ß" },
        { "-", "337.0 MiB", "174.9 MiB", "free space", "" },
    };
    for (int r = 0, y = ty + line + 4; r < 3; r++, y += line + 4) {
        bool sel = r == 1;
        gfx_fill(c, x0, y, x1 - x0, line + 4, sel ? BLUE : WHITE);
        for (int i = 0; i < 5; i++)
            gfx_text(c, x0 + 10 + col[i], y + 2, f, sel ? WHITE : r == 2 ? MUTED : TEXT, rows[r][i]);
    }

    /* every face of the font, and characters it lacks (drawn as \"?\") */
    int y = ty + 5 * (line + 4) + 10;
    static const int sizes[] = { 16, 20, 26, 32 };
    for (int i = 0; i < 4 && y < H - 160; i++) {
        const GfxFont *r = gfx_font(sizes[i], false), *b = gfx_font(sizes[i], true);
        int x = gfx_text(c, x0, y, r, TEXT, "AVATAR Tomorrow 0123456789 ");
        x = gfx_text(c, x, y, b, GREEN, "Wave, To, Yes. ");
        gfx_text(c, x, y, r, AMBER, "Ω中 ← →");
        y += gfx_line_height(r);
    }

    /* message line and buttons, anchored to the bottom */
    gfx_fill(c, 0, H - 110, W, 1, LINE);
    gfx_text(c, 14, H - 106, f, GREEN, "Partition 4 added.");
    gfx_fill(c, 0, H - 76, W, 76, 0xE9ECEF);
    int x = 14 + 130;
    gfx_text(c, 14, H - 70, f, MUTED, "Partition:");
    static const char *const pk[][2] = { { "N", "New" }, { "D", "Delete" }, { "T", "Type" }, { "W", "Wipe" } };
    for (int i = 0; i < 4; i++)
        x = button(c, f, x, H - 72, pk[i][0], pk[i][1], false);
    x = 14 + 130;
    gfx_text(c, 14, H - 34, f, MUTED, "Disk:");
    x = button(c, f, x, H - 36, "Enter", "Write", true);
    button(c, f, x, H - 36, "Esc", "Quit", false);
    /* a round rectangle cut by the edge of the screen */
    gfx_round_rect(c, W - 40, H - 40, 80, 80, 20, BLUE, 3, AMBER);
}
