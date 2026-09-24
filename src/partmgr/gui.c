/* partmgr: the graphical window (decisions P18-P25; the approved mockup is
 * docs/assets/gui-mockup.png). One window: the disks on the left; on the
 * right the selected disk as a bar to scale above the table of its rows;
 * a message line; two rows of buttons. Every button is a key of the text
 * screens, and a click on it is that key. Tab moves the keys between the
 * disks and the rows; Page Up and Page Down change the disk at any time.
 *
 * Sizes come from the screen: the font is chosen by the screen's height and
 * every measure from the font's line height. The whole window is drawn on a
 * canvas and copied to the screen; the pointer is drawn over it without
 * changing it. After every drawing the window is described, line by line,
 * on the serial port of the console (pal_serial_log): the QEMU test reads
 * it, as it reads the text screens. */
#include "gui.h"
#include "gfx.h"
#include "view.h"

enum {
    C_BG = 0xF3F4F6, C_PANEL = 0xFFFFFF, C_TEXT = 0x1F2328, C_MUTED = 0x6E7781, C_LINE = 0xD0D7DE,
    C_BLUE = 0x1F6FEB, C_TITLE = 0x1B4F9C, C_KEY = 0x1B4F9C, C_AMBER = 0x9A5B00, C_GREEN = 0x1A7F37,
    C_RED = 0xCF222E, C_DIM = 0xDCE6F5, C_HEAD = 0xE5E9F0, C_ALT = 0xF8F9FA, C_BAR = 0xE9ECEF,
    C_HOVER = 0xDDEBFF, C_FREE = 0xE6E8EB, C_HATCH = 0xD5D9DE, C_TITLE_NOTE = 0xFFE08A, C_EXT = 0xEFE7DA,
    C_EDGE = 0x8C959F,
};

typedef struct {
    int x, y, w, h;
} Rect;

static bool inside(const Rect *r, int x, int y)
{
    return x >= r->x && y >= r->y && x < r->x + r->w && y < r->y + r->h;
}

/* The buttons, in the order of the key bars of 0.1.2; F5 is in the list. */
typedef struct {
    const char *key, *label;
    uint32_t ch;
    uint16_t scan;
    int row; /* 0 Partition, 1 Disk, 2 the list of disks */
} ButtonDef;

static const ButtonDef buttons[] = {
    { "N", "New", 'n', 0, 0 },           { "D", "Delete", 'd', 0, 0 },      { "T", "Type", 't', 0, 0 },
    { "R", "Rename", 'r', 0, 0 },        { "A", "Active", 'a', 0, 0 },      { "W", "Wipe", 'w', 0, 0 },
    { "Enter", "Write", '\r', 0, 1 },    { "Z", "New table", 'z', 0, 1 },   { "X", "Delete table", 'x', 0, 1 },
    { "B", "Backup", 'b', 0, 1 },        { "S", "Restore", 's', 0, 1 },     { "Esc", "Quit", 0, KEY_ESC, 1 },
    { "F5", "Rescan", 0, KEY_F1 + 4, 2 },
};
#define NBUTTONS ((int)ARRAY_SIZE(buttons))

typedef struct {
    int W, H;
    const GfxFont *f, *fb;
    int L, pad, title_h;
    int bl, bg, br; /* inside a button: left margin, key to label, right margin */
    Rect list, head, bar, table, msg, keys;
    int list_top, disk_h, row_h, nvis, notes_y;
    int col[5];
    int group_w;
    Rect btn[NBUTTONS];
} Layout;

typedef struct {
    int kind; /* PT_*, or -1 when the disk cannot be read */
    int nparts;
} Summary;

typedef struct {
    GfxCanvas c, patch;
    int arrow;       /* the pointer's size: twice as large on very large screens */
    Layout l;
    PmDisk *disks;
    int ndisks, dsel, dtop;
    Summary *sum;
    View v;
    bool focus_disks;
    bool pointer, down;
    int px, py, hover;
} Gui;

/* ---- layout ---- */

/* The font by the screen's height: the text grows with the screen, in the
 * same proportion from 1080 rows (26 pixels) to 2160 (52) (P23). */
static int font_size(int h)
{
    return h <= 600 ? 16 : h <= 900 ? 20 : h <= 1300 ? 26 : h <= 1700 ? 32 : h <= 2000 ? 40 : 52;
}

static int button_w(const Layout *l, int i)
{
    return l->bl + gfx_text_width(l->f, buttons[i].key) + 8 + l->bg + gfx_text_width(l->f, buttons[i].label) + l->br;
}

/* The rows of buttons: the two rows of the key bars, with the group names
 * when they fit, else without, else with narrower buttons and gaps; on a
 * screen too narrow even for that, the buttons of each group go on as many
 * rows as they need. */
static void layout_buttons(Layout *l)
{
    int bh = l->L + 6, gap = 8, right = l->W - 8;
    int gw = MAX(gfx_text_width(l->f, "Partition:"), gfx_text_width(l->f, "Disk:")) + 20;
    int rows = 2;
    for (int pass = 0; pass < 4; pass++) {
        l->group_w = pass == 0 ? gw : 0;
        gap = pass < 2 ? 8 : 4;
        l->bl = pass < 2 ? 6 : 4, l->bg = pass < 2 ? 8 : 5, l->br = pass < 2 ? 12 : 6;
        bool fits = true;
        for (int row = 0; row < 2; row++) {
            int x = 14 + l->group_w;
            for (int i = 0; i < NBUTTONS; i++)
                if (buttons[i].row == row)
                    x += button_w(l, i) + gap;
            fits &= x - gap <= right;
        }
        if (fits || pass == 3)
            break;
    }
    /* place them, starting a new row where one is full */
    int x = 14 + l->group_w, row = 0, placed_row[NBUTTONS];
    for (int i = 0, group = 0; i < NBUTTONS; i++) {
        if (buttons[i].row == 2)
            continue;
        if (buttons[i].row != group) {
            group = buttons[i].row;
            row++, x = 14 + l->group_w;
        } else if (x > 14 + l->group_w && x + button_w(l, i) > right)
            row++, x = 14 + l->group_w;
        placed_row[i] = row;
        l->btn[i] = (Rect){ x, 0, button_w(l, i), bh };
        x += l->btn[i].w + gap;
    }
    rows = row + 1;
    int y0 = l->H - rows * (bh + 6) - 2;
    l->keys = (Rect){ 0, y0 - 4, l->W, l->H - y0 + 4 };
    for (int i = 0; i < NBUTTONS; i++)
        if (buttons[i].row != 2)
            l->btn[i].y = y0 + placed_row[i] * (bh + 6);
}

static void layout(Gui *g, int nnotes)
{
    Layout *l = &g->l;
    l->W = g->c.w, l->H = g->c.h;
    int fs = font_size(l->H);
    l->f = gfx_font(fs, false), l->fb = gfx_font(fs, true);
    l->L = gfx_line_height(l->f);
    l->pad = l->L / 2;
    l->title_h = l->L + 7;
    layout_buttons(l);
    l->msg = (Rect){ 0, l->keys.y - l->L - 8, l->W, l->L + 8 };
    int body_y = l->title_h + 1, body_h = l->msg.y - body_y;
    /* the list of disks: as in the mockup, narrower on a small screen */
    int pw = MAX((l->W < 800 ? 8 : 10) * l->L, MIN(14 * l->L, l->W * 300 / 1280));
    l->list = (Rect){ 0, body_y, pw, body_h };
    int hb = l->L + 16; /* the header of the list: "Disks" and F5 */
    int f5w = button_w(l, NBUTTONS - 1);
    l->btn[NBUTTONS - 1] = (Rect){ pw - 10 - f5w, body_y + (hb - l->L - 6) / 2, f5w, l->L + 6 };
    l->list_top = body_y + hb;
    l->disk_h = 2 * l->L + 8;
    int x0 = pw + l->pad + 2, x1 = l->W - l->pad - 2;
    l->head = (Rect){ x0, body_y + 10, x1 - x0, l->L };
    l->bar = (Rect){ x0, l->head.y + l->L + 8, x1 - x0, 2 * l->L + 6 };
    l->row_h = l->L + 4;
    l->table = (Rect){ x0, l->bar.y + l->bar.h + l->pad + 8, x1 - x0, 0 };
    l->notes_y = l->msg.y - 6 - nnotes * l->L;
    l->nvis = MAX(1, (l->notes_y - 6 - l->table.y - l->row_h) / l->row_h);
    l->table.h = l->row_h * (1 + l->nvis);
    /* the columns at the proportions of the mockup, never narrower than
     * what they hold: a number, a start and a size, a type */
    static const int frac[5] = { 0, 63, 215, 367, 658 };
    int num = gfx_text_width(l->f, "00*") + 14, size = gfx_text_width(l->f, "000.0 MiB") + 14;
    int least[5] = { 0, num, num + size, num + 2 * size, num + 2 * size + gfx_text_width(l->f, "Linux filesystem") + 14 };
    least[4] = MIN(least[4], l->table.w - 60);
    for (int i = 0; i < 5; i++)
        l->col[i] = l->table.x + 10 + MAX(l->table.w * frac[i] / 1000, least[i]);
}

/* ---- the disks ---- */

static void summarise(Gui *g)
{
    free(g->sum);
    g->sum = xcalloc((size_t)MAX(g->ndisks, 1), sizeof(Summary));
    for (int i = 0; i < g->ndisks; i++) {
        PtTable t;
        g->sum[i].kind = -1;
        if (!pt_read(&g->disks[i].dev, &t)) {
            g->sum[i].kind = t.kind;
            for (int j = 0; j < t.nparts; j++)
                g->sum[i].nparts += t.parts[j].role != PT_EXTENDED;
            pt_free(&t);
        }
    }
}

static void open_disk(Gui *g, int i)
{
    pm_view_free(&g->v);
    g->v = (View){ 0 };
    g->dsel = i;
    if (i < 0 || i >= g->ndisks)
        return;
    g->v.d = &g->disks[i];
    if (!pm_view_load(&g->v))
        pm_say(&g->v, true, "The disk could not be read.");
}

/* Reads the disks again, keeping the selected one when it is still there. */
static void scan(Gui *g)
{
    char name[16] = "";
    if (g->dsel >= 0 && g->dsel < g->ndisks)
        snprintf(name, sizeof(name), "%s", g->disks[g->dsel].name);
    pm_view_free(&g->v);
    g->ndisks = pm_disks(&g->disks);
    summarise(g);
    int sel = -1;
    for (int i = 0; i < g->ndisks; i++)
        if (name[0] && !strcmp(g->disks[i].name, name))
            sel = i;
    /* at first the first disk partmgr may change */
    for (int i = 0; i < g->ndisks && sel < 0; i++)
        if (!g->disks[i].boot && !g->disks[i].readonly)
            sel = i;
    open_disk(g, sel < 0 ? 0 : sel);
}

/* ---- drawing ---- */

static void text_right(GfxCanvas *c, int x_end, int y, const GfxFont *f, GfxColor col, const char *s)
{
    gfx_text(c, x_end - gfx_text_width(f, s), y, f, col, s);
}

static void draw_button(Gui *g, int i)
{
    const Layout *l = &g->l;
    const Rect *r = &l->btn[i];
    bool hover = g->hover == i;
    gfx_round_rect(&g->c, r->x, r->y, r->w, r->h, 5, hover ? C_HOVER : C_PANEL, hover ? 2 : 1, hover ? C_BLUE : C_LINE);
    int kw = gfx_text_width(l->f, buttons[i].key) + 8;
    gfx_round_rect(&g->c, r->x + l->bl, r->y + 4, kw, r->h - 8, 3, C_KEY, 0, 0);
    gfx_text(&g->c, r->x + l->bl + 4, r->y + 3, l->f, C_PANEL, buttons[i].key);
    gfx_text(&g->c, r->x + l->bl + kw + l->bg, r->y + 3, l->f, C_TEXT, buttons[i].label);
}

static void disk_lines(Gui *g, int i, char *l2, size_t n2, const char **note)
{
    const Summary *s = &g->sum[i];
    if (s->kind < 0)
        snprintf(l2, n2, "unreadable");
    else if (s->kind == PT_NONE)
        snprintf(l2, n2, "no table");
    else
        snprintf(l2, n2, "%s, %d partition%s", pm_table_name(s->kind), s->nparts, s->nparts == 1 ? "" : "s");
    *note = g->disks[i].boot ? "started from here: read only" : g->disks[i].readonly ? "write-protected" : NULL;
}

/* The note under a disk takes two lines when it does not fit in one: it
 * is split after its colon. */
static bool note_split(const Gui *g, const char *note)
{
    return note && strchr(note, ':') && 14 + gfx_text_width(g->l.f, note) > g->l.list.w - 6;
}

static int disk_height(const Gui *g, int i)
{
    const char *note = g->disks[i].boot ? "started from here: read only" : g->disks[i].readonly ? "write-protected" : NULL;
    return g->l.disk_h + (note ? g->l.L : 0) + (note_split(g, note) ? g->l.L : 0);
}

static void draw_disks(Gui *g)
{
    const Layout *l = &g->l;
    const Rect *r = &l->list;
    gfx_fill(&g->c, r->x, r->y, r->w, r->h, C_PANEL);
    gfx_fill(&g->c, r->x + r->w, r->y, 1, r->h, C_LINE);
    gfx_text(&g->c, 14, r->y + 8, l->fb, C_TEXT, "Disks");
    draw_button(g, NBUTTONS - 1);
    gfx_clip(&g->c, r->x, l->list_top, r->w, r->y + r->h - l->list_top);
    if (!g->ndisks)
        gfx_text(&g->c, 14, l->list_top + 4, l->f, C_MUTED, "No disks found.");
    /* scrolled so that the selected disk is in view */
    if (g->dsel < g->dtop)
        g->dtop = g->dsel;
    for (;;) {
        int y = l->list_top;
        for (int i = g->dtop; i <= g->dsel && i < g->ndisks; i++)
            y += disk_height(g, i) + 4;
        if (y <= r->y + r->h || g->dtop >= g->dsel)
            break;
        g->dtop++;
    }
    int y = l->list_top;
    for (int i = MAX(g->dtop, 0); i < g->ndisks && y < r->y + r->h; i++) {
        int h = disk_height(g, i);
        bool sel = i == g->dsel;
        bool strong = sel && g->focus_disks;
        if (sel)
            gfx_fill(&g->c, 0, y, r->w, h, strong ? C_BLUE : C_DIM);
        GfxColor fg = strong ? C_PANEL : C_TEXT, sub = strong ? 0xDCE8FF : C_MUTED;
        char size[32], l2[64];
        const char *note;
        pm_fmt_size(size, sizeof(size), g->disks[i].size);
        disk_lines(g, i, l2, sizeof(l2), &note);
        if (sel && g->v.t.changed)
            strcat(l2, " *");
        gfx_text(&g->c, 14, y + 4, l->fb, fg, g->disks[i].name);
        int kx = 14 + gfx_text_width(l->fb, "blk00") + l->pad;
        bool kind_fits = kx + gfx_text_width(l->fb, g->disks[i].kind) + l->pad <= r->w - 14 - gfx_text_width(l->fb, size);
        if (kind_fits)
            gfx_text(&g->c, kx, y + 4, l->fb, fg, g->disks[i].kind);
        text_right(&g->c, r->w - 14, y + 4, l->fb, fg, size);
        if (!kind_fits) {
            /* a narrow list: the kind of disk leads the second line */
            char both[96];
            snprintf(both, sizeof(both), "%s, %s", g->disks[i].kind, l2);
            snprintf(l2, sizeof(l2), "%.63s", both);
        }
        gfx_text(&g->c, 14, y + 4 + l->L, l->f, sub, l2);
        GfxColor nc = strong ? C_TITLE_NOTE : C_AMBER;
        if (note && note_split(g, note)) {
            const char *colon = strchr(note, ':');
            char first[64];
            snprintf(first, sizeof(first), "%.*s", (int)(colon - note + 1), note);
            gfx_text(&g->c, 14, y + 4 + 2 * l->L, l->f, nc, first);
            gfx_text(&g->c, 14, y + 4 + 3 * l->L, l->f, nc, colon + 2);
        } else if (note)
            gfx_text(&g->c, 14, y + 4 + 2 * l->L, l->f, nc, note);
        y += h + 4;
    }
    gfx_unclip(&g->c);
}

static GfxColor type_colour(const View *v, const PtPart *p)
{
    char t[48];
    pm_type_text(v, p, t, sizeof(t));
    if (p->role == PT_EXTENDED)
        return C_EXT;
    if (strstr(t, "EFI"))
        return 0xF6C28B;
    if (strstr(t, "swap"))
        return 0xF4B5B0;
    if (strstr(t, "Linux"))
        return 0xA8D5A2;
    if (strstr(t, "Microsoft") || strstr(t, "Windows") || strstr(t, "FAT") || strstr(t, "NTFS"))
        return 0x9EC5F8;
    return 0xC9C3E6;
}

/* Where a row lies in the bar. */
static void row_span(const Gui *g, const Row *r, int *a, int *b)
{
    const View *v = &g->v;
    uint64_t start = r->free ? r->f.start : v->t.parts[r->part].start;
    uint64_t size = r->free ? r->f.size : v->t.parts[r->part].size;
    uint64_t n = v->t.nblocks ? v->t.nblocks : 1;
    const Rect *bar = &g->l.bar;
    *a = bar->x + (int)(start * (uint64_t)bar->w / n);
    *b = bar->x + (int)((start + size) * (uint64_t)bar->w / n);
    if (*b - *a < 3)
        *b = *a + 3; /* a tiny partition stays visible */
}

static void row_label(const View *v, const Row *r, char *out, size_t n, bool with_type)
{
    if (r->free) {
        char s[32];
        pm_fmt_size(s, sizeof(s), r->f.size * v->d->dev.bsize);
        snprintf(out, n, "free %s", s);
        return;
    }
    const PtPart *p = &v->t.parts[r->part];
    char t[48] = "";
    if (with_type)
        pm_type_text(v, p, t, sizeof(t));
    snprintf(out, n, "%d%s%s%s", p->num, p->changed ? "*" : "", t[0] ? " " : "", t);
}

static void draw_bar(Gui *g)
{
    const Layout *l = &g->l;
    const View *v = &g->v;
    const Rect *bar = &l->bar;
    gfx_fill(&g->c, bar->x, bar->y, bar->w, bar->h, C_FREE);
    gfx_frame(&g->c, bar->x, bar->y, bar->w, bar->h, 1, C_LINE);
    if (!v->loaded || v->t.kind == PT_NONE) {
        gfx_text(&g->c, bar->x + 10, bar->y + (bar->h - l->L) / 2, l->f, C_MUTED,
                 v->loaded ? "No partition table." : "The disk could not be read.");
        return;
    }
    /* the extended partition first: a band around its logical partitions */
    for (int i = 0; i < v->t.nparts; i++) {
        const PtPart *p = &v->t.parts[i];
        if (p->role != PT_EXTENDED)
            continue;
        Row r = { false, i, { 0 }, p->start };
        int a, b;
        row_span(g, &r, &a, &b);
        gfx_fill(&g->c, a, bar->y, b - a, bar->h, C_EXT);
        gfx_frame(&g->c, a, bar->y, b - a, bar->h, 1, C_EDGE);
    }
    for (int i = 0; i < v->nrows; i++) {
        const Row *r = &v->rows[i];
        const PtPart *p = r->free ? NULL : &v->t.parts[r->part];
        if (p && p->role == PT_EXTENDED)
            continue;
        bool in_ext = r->free ? r->f.logical : p->role == PT_LOGICAL;
        int a, b, y = bar->y + (in_ext ? 5 : 0), h = bar->h - (in_ext ? 10 : 0);
        row_span(g, r, &a, &b);
        if (r->free) {
            gfx_fill(&g->c, a, y, b - a, h, C_FREE);
            gfx_hatch(&g->c, a, y, b - a, h, 10, C_HATCH);
        } else {
            gfx_fill(&g->c, a, y, b - a, h, type_colour(v, p));
            gfx_frame(&g->c, a, y, b - a, h, 1, C_EDGE);
        }
        /* the label: whole, else shorter, else none - never cut */
        char lab[80], size[32];
        row_label(v, r, lab, sizeof(lab), true);
        if (gfx_text_width(l->f, lab) + 8 > b - a) {
            if (r->free)
                snprintf(lab, sizeof(lab), "free");
            else
                row_label(v, r, lab, sizeof(lab), false); /* the number only */
        }
        int m = MAX(4, l->L / 6); /* the margin grows with the font */
        if (gfx_text_width(l->f, lab) + m + 2 <= b - a)
            gfx_text(&g->c, a + m, y + m, l->f, r->free ? C_MUTED : C_TEXT, lab);
        if (p && h >= 2 * l->L) {
            pm_fmt_size(size, sizeof(size), p->size * v->d->dev.bsize);
            if (gfx_text_width(l->f, size) + m + 4 <= b - a)
                gfx_text(&g->c, a + m, y + m + l->L, l->f, 0x3D444D, size);
        }
    }
    if (v->nrows) {
        int a, b;
        row_span(g, &v->rows[v->sel], &a, &b);
        gfx_frame(&g->c, a - 2, bar->y - 4, b - a + 4, bar->h + 8, 4, g->focus_disks ? 0x8AA9D6 : C_BLUE);
    }
}

/* The cells of a row, as the text screen writes them. */
static void row_cells(const View *v, const Row *r, char cells[5][112])
{
    uint64_t bsz = v->d->dev.bsize;
    bool gpt = v->t.kind == PT_GPT;
    if (r->free) {
        snprintf(cells[0], 112, "-");
        pm_fmt_size(cells[1], 112, r->f.start * bsz);
        pm_fmt_size(cells[2], 112, r->f.size * bsz);
        snprintf(cells[3], 112, "%s", r->f.logical ? "free space (for logical partitions)" : "free space");
        cells[4][0] = 0;
        return;
    }
    const PtPart *p = &v->t.parts[r->part];
    snprintf(cells[0], 112, "%d%s", p->num, p->changed ? "*" : "");
    pm_fmt_size(cells[1], 112, p->start * bsz);
    pm_fmt_size(cells[2], 112, p->size * bsz);
    pm_type_text(v, p, cells[3], 112);
    if (gpt)
        snprintf(cells[4], 112, "%s", p->name);
    else
        snprintf(cells[4], 112, "%s", p->active ? "active" : p->role == PT_LOGICAL ? "logical" : "");
}

static void draw_table(Gui *g)
{
    const Layout *l = &g->l;
    View *v = &g->v;
    const Rect *t = &l->table;
    bool gpt = v->t.kind == PT_GPT;
    gfx_fill(&g->c, t->x, t->y, t->w, l->row_h, C_HEAD);
    static const char *const head[] = { "#", "Start", "Size", "Type", NULL };
    for (int i = 0; i < 5; i++)
        gfx_text(&g->c, l->col[i], t->y + 2, l->fb, C_TEXT, head[i] ? head[i] : gpt ? "Name" : "Flags");
    if (v->sel < v->top)
        v->top = v->sel;
    if (v->sel >= v->top + l->nvis)
        v->top = v->sel - l->nvis + 1;
    int y = t->y + l->row_h;
    if (!v->nrows) {
        gfx_fill(&g->c, t->x, y, t->w, l->row_h, C_PANEL);
        gfx_text(&g->c, l->col[0], y + 2, l->f, C_MUTED,
                 !v->loaded ? "" : v->t.kind == PT_NONE ? "No partition table." : "No free space.");
        y += l->row_h;
    }
    for (int i = v->top; i < v->nrows && i < v->top + l->nvis; i++, y += l->row_h) {
        const Row *r = &v->rows[i];
        bool sel = i == v->sel, strong = sel && !g->focus_disks;
        gfx_fill(&g->c, t->x, y, t->w, l->row_h, strong ? C_BLUE : sel ? C_DIM : (i % 2 ? C_ALT : C_PANEL));
        bool changed = !r->free && v->t.parts[r->part].changed;
        GfxColor fg = strong ? C_PANEL : r->free ? C_MUTED : changed ? C_AMBER : C_TEXT;
        char cells[5][112];
        row_cells(v, r, cells);
        for (int c = 0; c < 5; c++) {
            /* a cell may run over the next ones while they are empty */
            int next = c + 1;
            while (next < 5 && !cells[next][0])
                next++;
            int end = next < 5 ? l->col[next] - 8 : t->x + t->w - 4;
            gfx_clip(&g->c, l->col[c], y, end - l->col[c], l->row_h);
            gfx_text(&g->c, l->col[c], y + 2, l->f, fg, cells[c]);
            gfx_unclip(&g->c);
        }
    }
    gfx_frame(&g->c, t->x, t->y, t->w, y - t->y, 1, C_LINE);
    for (int i = 0; i < v->t.nnotes; i++) {
        char note[140];
        snprintf(note, sizeof(note), "Note: %s", v->t.notes[i]);
        gfx_text(&g->c, t->x, l->notes_y + i * l->L, l->f, C_AMBER, note);
    }
}

static void draw_window(Gui *g)
{
    View *v = &g->v;
    layout(g, v->t.nnotes);
    const Layout *l = &g->l;
    GfxCanvas *c = &g->c;
    gfx_fill(c, 0, 0, l->W, l->H, C_BG);
    /* title */
    char title[64];
    snprintf(title, sizeof(title), "EFI Partition Manager %s", PARTMGR_VERSION);
    gfx_fill(c, 0, 0, l->W, l->title_h, C_TITLE);
    gfx_text(c, 14, 3, l->fb, C_PANEL, title);
    const char *right = v->d && v->d->boot ? "started from here: read only"
                        : v->d && v->d->readonly ? "write-protected"
                        : v->t.changed ? "* = changes not written" : "";
    text_right(c, l->W - 14, 3, l->f, C_TITLE_NOTE, right);
    draw_disks(g);
    /* the selected disk */
    if (v->d) {
        char size[32];
        pm_fmt_size(size, sizeof(size), v->d->size);
        const char *parts[] = { v->d->name, v->d->kind, size, v->loaded ? pm_table_name(v->t.kind) : "unreadable" };
        int x = l->head.x;
        for (int i = 0; i < 4; i++)
            x = gfx_text(c, x, l->head.y, l->fb, C_TEXT, parts[i]) + 2 * l->pad;
        draw_bar(g);
        draw_table(g);
    }
    /* message line and buttons */
    gfx_fill(c, 0, l->msg.y, l->W, 1, C_LINE);
    if (v->msg[0])
        gfx_text(c, 14, l->msg.y + 4, l->f, v->msg_err ? C_RED : C_GREEN, v->msg);
    gfx_fill(c, 0, l->keys.y, l->W, l->keys.h, C_BAR);
    if (l->group_w) {
        gfx_text(c, 14, l->btn[0].y + 3, l->f, C_MUTED, "Partition:");
        gfx_text(c, 14, l->btn[6].y + 3, l->f, C_MUTED, "Disk:");
    }
    for (int i = 0; i < NBUTTONS - 1; i++)
        draw_button(g, i);
}

/* ---- the pointer: drawn over the canvas, which stays as it is ---- */

static void pointer_show(Gui *g)
{
    if (!g->pointer)
        return;
    int w = MIN(g->patch.w, g->c.w - g->px), h = MIN(g->patch.h, g->c.h - g->py);
    for (int j = 0; j < h; j++)
        memcpy(g->patch.px + j * g->patch.w, g->c.px + (g->py + j) * g->c.w + g->px, (size_t)w * 4);
    gfx_arrow(&g->patch, 0, 0, g->arrow);
    pal_gfx_show(g->patch.px, g->patch.w, 0, 0, g->px, g->py, w, h);
}

static void pointer_hide(Gui *g)
{
    if (!g->pointer)
        return;
    int w = MIN(g->patch.w, g->c.w - g->px), h = MIN(g->patch.h, g->c.h - g->py);
    pal_gfx_show(g->c.px, g->c.w, g->px, g->py, g->px, g->py, w, h);
}

/* ---- what the window shows, for the tests ---- */

static void logf(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
static void logf(const char *fmt, ...)
{
    char buf[400] = "gui: ";
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf + 5, sizeof(buf) - 5, fmt, ap);
    va_end(ap);
    pal_serial_log(buf);
}

static void log_window(Gui *g)
{
    const Layout *l = &g->l;
    View *v = &g->v;
    logf("window %dx%d font %d", l->W, l->H, gfx_font_size(l->f));
    int y = l->list_top;
    for (int i = MAX(g->dtop, 0); i < g->ndisks && y < l->list.y + l->list.h; i++) {
        char size[32], l2[64];
        const char *note;
        pm_fmt_size(size, sizeof(size), g->disks[i].size);
        disk_lines(g, i, l2, sizeof(l2), &note);
        logf("disk %s %s %s %s%s%s%s at %d,%d", g->disks[i].name, g->disks[i].kind, size, l2, note ? ", " : "",
             note ? note : "", i == g->dsel ? " <" : "", l->list.w / 2, y + disk_height(g, i) / 2);
        y += disk_height(g, i) + 4;
    }
    if (v->d) {
        char size[32];
        pm_fmt_size(size, sizeof(size), v->d->size);
        logf("shows %s %s %s %s%s", v->d->name, v->d->kind, size, v->loaded ? pm_table_name(v->t.kind) : "unreadable",
             v->t.changed ? " *" : "");
        for (int i = 0; i < v->nrows; i++) {
            const Row *r = &v->rows[i];
            char cells[5][112];
            row_cells(v, r, cells);
            int a, b;
            row_span(g, r, &a, &b);
            bool shown = i >= v->top && i < v->top + l->nvis;
            int ry = l->table.y + l->row_h * (1 + i - v->top) + l->row_h / 2;
            char where[48];
            if (shown)
                snprintf(where, sizeof(where), " at %d,%d", l->table.x + l->table.w / 2, ry);
            else
                where[0] = 0;
            logf("row %s %s %s %s%s%s%s%s bar %d,%d", cells[0], cells[1], cells[2], cells[3], cells[4][0] ? " " : "",
                 cells[4], i == v->sel ? " <" : "", where, (a + b) / 2, l->bar.y + l->bar.h / 2);
        }
        for (int i = 0; i < v->t.nnotes; i++)
            logf("note %s", v->t.notes[i]);
    }
    for (int i = 0; i < NBUTTONS; i++)
        logf("button %s %s at %d,%d%s", buttons[i].key, buttons[i].label, l->btn[i].x + l->btn[i].w / 2,
             l->btn[i].y + l->btn[i].h / 2, g->hover == i ? " hover" : "");
    int rows = 0, right = 0;
    for (int i = 0; i < NBUTTONS - 1; i++) {
        right = MAX(right, l->btn[i].x + l->btn[i].w);
        rows = MAX(rows, (l->btn[i].y - l->btn[0].y) / (l->btn[i].h + 6) + 1);
    }
    logf("buttons %d rows, right edge %d", rows, right);
    logf("focus %s", g->focus_disks ? "disks" : "partitions");
    if (v->msg[0])
        logf("message %s", v->msg);
    if (g->pointer)
        logf("pointer %d,%d", g->px, g->py);
    logf("shown");
}

static void redraw(Gui *g)
{
    draw_window(g);
    pal_gfx_show(g->c.px, g->c.w, 0, 0, 0, 0, g->c.w, g->c.h);
    pointer_show(g);
    log_window(g);
}

/* ---- input ---- */

static Gui *G; /* the window: one at a time */

/* Moves the pointer as the device says; true when it moved. */
static bool pointer_move(Gui *g, const PalPointer *p)
{
    int nx = p->abs ? (int)((long)p->ax * (g->c.w - 1) / 65535) : g->px + p->dx;
    int ny = p->abs ? (int)((long)p->ay * (g->c.h - 1) / 65535) : g->py + p->dy;
    nx = MAX(0, MIN(nx, g->c.w - 1)), ny = MAX(0, MIN(ny, g->c.h - 1));
    if (nx == g->px && ny == g->py)
        return false;
    pointer_hide(g);
    g->px = nx, g->py = ny;
    return true;
}

static int button_at(const Gui *g, int x, int y)
{
    for (int i = 0; i < NBUTTONS; i++)
        if (inside(&g->l.btn[i], x, y))
            return i;
    return -1;
}

/* Waits for a key or a click: 1 with the key in *K, 2 for a click at the
 * pointer. With HOVER, the window's button under the pointer is highlighted
 * (the window is drawn again when that changes). */
static int wait_input(Gui *g, PalKey *k, bool hover)
{
    for (;;) {
        if (pal_con_read_key(k, 10))
            return 1;
        PalPointer p;
        if (!g->pointer || !pal_pointer_read(&p))
            continue;
        bool pressed = p.left && !g->down;
        g->down = p.left;
        if (pointer_move(g, &p)) {
            int hv = hover ? button_at(g, g->px, g->py) : g->hover;
            if (hv != g->hover) {
                g->hover = hv;
                redraw(g);
            } else {
                pointer_show(g);
                logf("pointer %d,%d", g->px, g->py);
            }
        }
        if (pressed)
            return 2;
    }
}

/* Changing the disk, reading the disks again or quitting loses the changes
 * not written: asked first, as leaving a disk in the text screens. */
static bool may_leave(Gui *g, const char *title)
{
    if (!g->v.t.changed)
        return true;
    redraw(g);
    return ui_yesno(true, title, "Discard the unwritten changes? (Y/N)");
}

static void change_disk(Gui *g, int i)
{
    if (i < 0 || i >= g->ndisks || i == g->dsel || !may_leave(g, "Change disk"))
        return;
    open_disk(g, i);
}

/* The summary of the selected disk again, after an action. */
static void summarise_selected(Gui *g)
{
    PtTable t;
    Summary *s = &g->sum[g->dsel];
    *s = (Summary){ -1, 0 };
    if (!pt_read(&g->disks[g->dsel].dev, &t)) {
        s->kind = t.kind;
        for (int j = 0; j < t.nparts; j++)
            s->nparts += t.parts[j].role != PT_EXTENDED;
        pt_free(&t);
    }
}

/* A key; false to quit. */
static bool key(Gui *g, PalKey k)
{
    View *v = &g->v;
    g->v.msg[0] = 0;
    if (ui_is_esc(&k))
        return !may_leave(g, "Quit");
    if (k.ch == '\t') {
        g->focus_disks = !g->focus_disks;
        return true;
    }
    if (k.scan == KEY_F1 + 4) {
        if (may_leave(g, "Read the disks again")) {
            scan(g);
            pm_say(&g->v, false, "Disks read again.");
        }
        return true;
    }
    if (k.scan == KEY_PGUP || k.scan == KEY_PGDN) {
        change_disk(g, g->dsel + (k.scan == KEY_PGUP ? -1 : 1));
        return true;
    }
    if (k.scan == KEY_UP || k.scan == KEY_DOWN || k.scan == KEY_HOME || k.scan == KEY_END) {
        int n = g->focus_disks ? g->ndisks : v->nrows, cur = g->focus_disks ? g->dsel : v->sel;
        int to = k.scan == KEY_UP ? cur - 1 : k.scan == KEY_DOWN ? cur + 1 : k.scan == KEY_HOME ? 0 : n - 1;
        if (to < 0 || to >= n)
            return true;
        if (g->focus_disks)
            change_disk(g, to);
        else
            v->sel = to;
        return true;
    }
    if (v->d && v->loaded && pm_disk_action(v, k))
        summarise_selected(g);
    return true;
}

/* A click at the pointer; false to quit. */
static bool click(Gui *g)
{
    const Layout *l = &g->l;
    View *v = &g->v;
    int x = g->px, y = g->py;
    for (int i = 0; i < NBUTTONS; i++)
        if (inside(&l->btn[i], x, y))
            return key(g, (PalKey){ buttons[i].ch, buttons[i].scan, 0 });
    if (inside(&l->list, x, y) && y >= l->list_top) {
        int yy = l->list_top;
        for (int i = MAX(g->dtop, 0); i < g->ndisks; i++) {
            int h = disk_height(g, i);
            if (y >= yy && y < yy + h) {
                g->focus_disks = true;
                v->msg[0] = 0;
                change_disk(g, i);
                return true;
            }
            yy += h + 4;
        }
        return true;
    }
    if (inside(&l->bar, x, y) && v->nrows) {
        /* the row under the pointer: a logical partition before the extended band */
        for (int i = 0; i < v->nrows; i++) {
            const Row *r = &v->rows[i];
            if (!r->free && v->t.parts[r->part].role == PT_EXTENDED)
                continue;
            int a, b;
            row_span(g, r, &a, &b);
            if (x >= a && x < b) {
                v->sel = i;
                g->focus_disks = false;
                v->msg[0] = 0;
                return true;
            }
        }
        return true;
    }
    int first = l->table.y + l->row_h;
    if (inside(&l->table, x, y) && y >= first) {
        int i = v->top + (y - first) / l->row_h;
        if (i < v->nrows) {
            v->sel = i;
            g->focus_disks = false;
            v->msg[0] = 0;
        }
    }
    return true;
}

/* ---- dialogs: boxes over the window, which is dimmed meanwhile ---- */

#define DLG_LINES 16

typedef struct {
    bool warn;
    Rect box, text, extra; /* extra: a field or a list, between the text and the buttons */
    int nbtn;
    Rect btn[3];
    const char *key[3], *label[3];
    uint32_t *saved; /* the window under the box */
} Dlg;

/* Splits TEXT into lines of at most W pixels, at spaces when possible, and
 * at every '\n'. */
static int wrap(const GfxFont *f, const char *text, int w, char lines[DLG_LINES][200])
{
    int n = 0;
    const char *p = text;
    while (*p && n < DLG_LINES) {
        size_t para = strcspn(p, "\n");
        const char *end = p + para;
        while (p < end && n < DLG_LINES) {
            /* the longest piece that fits, cut at a space if it can be */
            size_t take = (size_t)(end - p), best = 0, space = 0;
            char piece[200];
            for (size_t k = 1; k <= take && k < sizeof(piece); k++) {
                if ((p[k - 1] & 0xC0) == 0x80 && k < take)
                    continue;
                snprintf(piece, sizeof(piece), "%.*s", (int)k, p);
                if (gfx_text_width(f, piece) > w)
                    break;
                best = k;
                if (k < take && p[k] == ' ')
                    space = k;
            }
            if (!best)
                best = 1;
            if (best < take && space)
                best = space;
            snprintf(lines[n++], 200, "%.*s", (int)best, p);
            p += best;
            while (p < end && *p == ' ')
                p++;
        }
        p = *end ? end + 1 : end;
    }
    return n;
}

static void dlg_button(Gui *g, const Rect *r, const char *key, const char *label, bool danger)
{
    const GfxFont *f = g->l.f;
    gfx_round_rect(&g->c, r->x, r->y, r->w, r->h, 5, danger ? C_RED : C_PANEL, 1, danger ? C_RED : C_LINE);
    int kw = gfx_text_width(f, key) + 8;
    gfx_round_rect(&g->c, r->x + 6, r->y + 4, kw, r->h - 8, 3, danger ? C_PANEL : C_KEY, 0, 0);
    gfx_text(&g->c, r->x + 10, r->y + 3, f, danger ? C_RED : C_PANEL, key);
    gfx_text(&g->c, r->x + 6 + kw + 8, r->y + 3, f, danger ? C_PANEL : C_TEXT, label);
}

/* Draws the box: title, text, room for EXTRA_H pixels, the buttons
 * (KEYS/LABELS, the first one on the left; with WARN the first is red). */
static void dlg_open(Gui *g, Dlg *d, bool warn, const char *title, const char *text, int extra_h, int nbtn,
                     const char *const *keys, const char *const *labels)
{
    const Layout *l = &g->l;
    GfxCanvas *c = &g->c;
    d->warn = warn;
    d->saved = xmalloc((size_t)c->w * (size_t)c->h * 4);
    memcpy(d->saved, c->px, (size_t)c->w * (size_t)c->h * 4);
    /* the window dimmed */
    for (int i = 0; i < c->w * c->h; i++) {
        uint32_t p = c->px[i];
        c->px[i] = (p >> 16 & 255) * 3 / 5 << 16 | (p >> 8 & 255) * 3 / 5 << 8 | (p & 255) * 3 / 5;
    }
    int w = MIN(c->w - 40, 26 * l->L);
    char lines[DLG_LINES][200];
    int n = wrap(warn ? l->fb : l->f, text, w - 2 * l->L, lines);
    int th = l->L + 8, bh = l->L + 6;
    int h = th + l->pad + n * l->L + (extra_h ? extra_h + l->pad : 0) + l->pad + bh + l->pad;
    d->box = (Rect){ (c->w - w) / 2, MAX(8, (c->h - h) / 2), w, h };
    Rect *b = &d->box;
    GfxColor tc = warn ? C_RED : C_TITLE;
    gfx_round_rect(c, b->x, b->y, b->w, b->h, 8, C_PANEL, 1, C_EDGE);
    gfx_round_rect(c, b->x, b->y, b->w, th + 8, 8, tc, 0, 0);
    gfx_fill(c, b->x, b->y + th, b->w, 8, C_PANEL);
    gfx_fill(c, b->x, b->y + th, 1, 8, C_EDGE);
    gfx_fill(c, b->x + b->w - 1, b->y + th, 1, 8, C_EDGE);
    gfx_text(c, b->x + l->L, b->y + 4, l->fb, C_PANEL, title);
    d->text = (Rect){ b->x + l->L, b->y + th + l->pad, b->w - 2 * l->L, n * l->L };
    for (int i = 0; i < n; i++)
        gfx_text(c, d->text.x, d->text.y + i * l->L, warn ? l->fb : l->f, warn ? 0xA40E26 : C_TEXT, lines[i]);
    d->extra = (Rect){ d->text.x, d->text.y + d->text.h + l->pad, d->text.w, extra_h };
    d->nbtn = nbtn;
    int x = b->x + b->w - l->L, y = b->y + b->h - l->pad - bh;
    for (int i = nbtn - 1; i >= 0; i--) {
        int bw = gfx_text_width(l->f, keys[i]) + 8 + gfx_text_width(l->f, labels[i]) + 26;
        x -= bw;
        d->btn[i] = (Rect){ x, y, bw, bh };
        d->key[i] = keys[i], d->label[i] = labels[i];
        dlg_button(g, &d->btn[i], keys[i], labels[i], warn && i == 0);
        x -= 10;
    }
    /* for the tests: the whole text, not the wrapped lines */
    char flat[600];
    snprintf(flat, sizeof(flat), "%s", text);
    for (char *q = flat; *q; q++)
        if (*q == '\n')
            *q = ' ';
    logf("dialog %s%s: %s", title, warn ? " (warning)" : "", flat);
    for (int i = 0; i < nbtn; i++)
        logf("dbutton %s %s at %d,%d", keys[i], labels[i], d->btn[i].x + d->btn[i].w / 2, d->btn[i].y + d->btn[i].h / 2);
}

static void dlg_show(Gui *g, const Dlg *d)
{
    pal_gfx_show(g->c.px, g->c.w, 0, 0, 0, 0, g->c.w, g->c.h);
    pointer_show(g);
    logf("dialog shown");
}

static void dlg_close(Gui *g, Dlg *d)
{
    memcpy(g->c.px, d->saved, (size_t)g->c.w * (size_t)g->c.h * 4);
    free(d->saved);
    pal_gfx_show(g->c.px, g->c.w, 0, 0, 0, 0, g->c.w, g->c.h);
    pointer_show(g);
    logf("dialog closed");
}

/* The dialog button under the pointer, or -1. */
static int dlg_hit(const Gui *g, const Dlg *d)
{
    for (int i = 0; i < d->nbtn; i++)
        if (inside(&d->btn[i], g->px, g->py))
            return i;
    return -1;
}

static void gui_message(bool warn, const char *title, const char *text)
{
    Gui *g = G;
    /* messages of the table code start in lower case: a sentence here */
    char buf[400];
    snprintf(buf, sizeof(buf), "%s", text);
    if (buf[0] >= 'a' && buf[0] <= 'z')
        buf[0] = (char)(buf[0] - 'a' + 'A');
    static const char *const keys[] = { "Enter" }, *const labels[] = { "OK" };
    Dlg d;
    dlg_open(g, &d, warn, title, buf, 0, 1, keys, labels);
    dlg_show(g, &d);
    for (;;) {
        PalKey k;
        int e = wait_input(g, &k, false);
        if (e == 1 || dlg_hit(g, &d) == 0)
            break; /* any key, as "Press a key." in the text screens */
    }
    dlg_close(g, &d);
}

static bool gui_yesno(bool warn, const char *title, const char *text)
{
    Gui *g = G;
    static const char *const keys[] = { "Y", "N" }, *const labels[] = { "Yes", "No" };
    Dlg d;
    dlg_open(g, &d, warn, title, text, 0, 2, keys, labels);
    dlg_show(g, &d);
    bool yes;
    for (;;) {
        PalKey k;
        int e = wait_input(g, &k, false);
        int hit = e == 2 ? dlg_hit(g, &d) : -1;
        /* Enter does nothing here: a key pressed twice cannot write (P8) */
        if ((e == 1 && (k.ch == 'y' || k.ch == 'Y')) || hit == 0) {
            yes = true;
            break;
        }
        if ((e == 1 && (k.ch == 'n' || k.ch == 'N' || ui_is_esc(&k))) || hit == 1) {
            yes = false;
            break;
        }
    }
    dlg_close(g, &d);
    return yes;
}

static void draw_field(Gui *g, const Dlg *d, const char *buf, size_t cur, bool fresh)
{
    const Layout *l = &g->l;
    const Rect *r = &d->extra;
    gfx_round_rect(&g->c, r->x, r->y, r->w, r->h, 4, C_PANEL, 2, C_BLUE);
    gfx_clip(&g->c, r->x + 4, r->y + 2, r->w - 8, r->h - 4);
    /* scrolled so that the cursor is visible */
    char before[200];
    snprintf(before, sizeof(before), "%.*s", (int)cur, buf);
    int shift = MAX(0, gfx_text_width(l->f, before) - (r->w - 24));
    int x = r->x + 8 - shift, y = r->y + (r->h - l->L) / 2;
    if (fresh && buf[0]) {
        /* the proposed text, replaced by the first character typed */
        gfx_fill(&g->c, x - 2, y + 2, gfx_text_width(l->f, buf) + 4, l->L - 4, C_BLUE);
        gfx_text(&g->c, x, y, l->f, C_PANEL, buf);
    } else {
        gfx_text(&g->c, x, y, l->f, C_TEXT, buf);
        gfx_fill(&g->c, x + gfx_text_width(l->f, before), y + 3, 2, l->L - 6, C_TEXT);
    }
    gfx_unclip(&g->c);
    pal_gfx_show(g->c.px, g->c.w, r->x, r->y, r->x, r->y, r->w, r->h);
    logf("field %s", buf);
}

static bool gui_input(bool warn, const char *title, const char *text, char *buf, size_t n)
{
    Gui *g = G;
    static const char *const keys[] = { "Enter", "Esc" }, *const labels[] = { "OK", "Cancel" };
    Dlg d;
    dlg_open(g, &d, warn, title, text, g->l.L + 12, 2, keys, labels);
    bool fresh = true; /* the first typed character replaces the proposed text */
    size_t cur = strlen(buf);
    draw_field(g, &d, buf, cur, fresh);
    dlg_show(g, &d);
    bool ok;
    for (;;) {
        PalKey k;
        int e = wait_input(g, &k, false);
        int hit = e == 2 ? dlg_hit(g, &d) : -1;
        if ((e == 1 && ui_is_enter(&k)) || hit == 0) {
            ok = true;
            break;
        }
        if ((e == 1 && ui_is_esc(&k)) || hit == 1) {
            ok = false;
            break;
        }
        if (e != 1)
            continue;
        size_t len = strlen(buf);
        if (k.scan == KEY_LEFT && cur > 0) {
            do
                cur--;
            while (cur > 0 && (buf[cur] & 0xC0) == 0x80);
        } else if (k.scan == KEY_RIGHT && cur < len) {
            do
                cur++;
            while (cur < len && (buf[cur] & 0xC0) == 0x80);
        } else if (k.scan == KEY_HOME)
            cur = 0;
        else if (k.scan == KEY_END)
            cur = len;
        else if ((k.ch == 8 || k.ch == 127) && cur > 0) {
            size_t s = cur;
            do
                s--;
            while (s > 0 && (buf[s] & 0xC0) == 0x80);
            memmove(buf + s, buf + cur, len - cur + 1);
            cur = s;
        } else if (k.scan == KEY_DELETE && cur < len) {
            size_t e2 = cur;
            do
                e2++;
            while (e2 < len && (buf[e2] & 0xC0) == 0x80);
            memmove(buf + cur, buf + e2, len - e2 + 1);
        } else if (k.ch >= 0x20 && k.ch != 127) {
            if (fresh) {
                buf[0] = 0;
                cur = len = 0;
            }
            char u[4];
            int ul = utf8_encode(k.ch, u);
            if (len + (size_t)ul < n) {
                memmove(buf + cur + ul, buf + cur, len - cur + 1);
                memcpy(buf + cur, u, (size_t)ul);
                cur += (size_t)ul;
            }
        } else
            continue;
        fresh = false;
        draw_field(g, &d, buf, cur, fresh);
        pointer_show(g);
    }
    dlg_close(g, &d);
    return ok;
}

static void draw_list(Gui *g, const Dlg *d, const char *const *items, int n, int sel, int top, int h)
{
    const Layout *l = &g->l;
    const Rect *r = &d->extra;
    gfx_fill(&g->c, r->x, r->y, r->w, r->h, C_PANEL);
    for (int i = 0; i < h && top + i < n; i++) {
        bool hl = top + i == sel;
        int y = r->y + i * l->row_h;
        if (hl)
            gfx_fill(&g->c, r->x, y, r->w, l->row_h, C_BLUE);
        gfx_text(&g->c, r->x + 8, y + 2, l->f, hl ? C_PANEL : C_TEXT, items[top + i]);
        logf("item %s at %d,%d%s", items[top + i], r->x + r->w / 2, y + l->row_h / 2, hl ? " <" : "");
    }
    if (n > h) {
        /* where the visible part lies in the list */
        int th = MAX(12, r->h * h / n), ty = r->y + (r->h - th) * top / MAX(1, n - h);
        gfx_fill(&g->c, r->x + r->w - 5, r->y, 5, r->h, C_BAR);
        gfx_fill(&g->c, r->x + r->w - 5, ty, 5, th, C_EDGE);
    }
    gfx_frame(&g->c, r->x, r->y, r->w, r->h, 1, C_LINE);
    pal_gfx_show(g->c.px, g->c.w, r->x, r->y, r->x, r->y, r->w, r->h);
}

static int gui_menu(const char *title, const char *const *items, int n, int sel)
{
    Gui *g = G;
    const Layout *l = &g->l;
    int h = MAX(1, MIN(n, MIN(14, (g->c.h - 12 * l->L) / l->row_h)));
    static const char *const keys[] = { "Enter", "Esc" }, *const labels[] = { "Choose", "Cancel" };
    Dlg d;
    dlg_open(g, &d, false, title, "", h * l->row_h, 2, keys, labels);
    if (sel < 0 || sel >= n)
        sel = 0;
    int top = 0;
    bool first = true;
    for (;;) {
        if (sel < top)
            top = sel;
        if (sel >= top + h)
            top = sel - h + 1;
        draw_list(g, &d, items, n, sel, top, h);
        if (first)
            dlg_show(g, &d); /* the whole box the first time, then the list only */
        else {
            pointer_show(g);
            logf("dialog shown");
        }
        first = false;
        PalKey k;
        int e = wait_input(g, &k, false);
        if (e == 2) {
            int hit = dlg_hit(g, &d);
            if (hit == 0)
                break;
            if (hit == 1) {
                sel = -1;
                break;
            }
            if (inside(&d.extra, g->px, g->py)) {
                int i = top + (g->py - d.extra.y) / l->row_h;
                if (i < n) {
                    sel = i; /* a click on an item chooses it */
                    break;
                }
            }
            continue;
        }
        if (ui_is_enter(&k))
            break;
        if (ui_is_esc(&k)) {
            sel = -1;
            break;
        }
        if (k.scan == KEY_UP && sel > 0)
            sel--;
        else if (k.scan == KEY_DOWN && sel + 1 < n)
            sel++;
        else if (k.scan == KEY_PGUP)
            sel = MAX(0, sel - h);
        else if (k.scan == KEY_PGDN)
            sel = MIN(n - 1, sel + h);
        else if (k.scan == KEY_HOME)
            sel = 0;
        else if (k.scan == KEY_END)
            sel = n - 1;
    }
    dlg_close(g, &d);
    return sel;
}

/* ---- the wipe: its progress in a red box, with a Stop button ---- */

static Rect wipe_stop_btn;

static void gui_wipe(const char *title, int pass, int percent, const char *amount)
{
    Gui *g = G;
    const Layout *l = &g->l;
    GfxCanvas *c = &g->c;
    int w = MIN(c->w - 40, 26 * l->L), th = l->L + 8, bh = l->L + 6;
    int h = th + l->pad + 3 * l->L + 16 + l->pad + bh + l->pad;
    Rect b = { (c->w - w) / 2, (c->h - h) / 2, w, h };
    gfx_round_rect(c, b.x, b.y, b.w, b.h, 8, C_PANEL, 1, C_EDGE);
    gfx_round_rect(c, b.x, b.y, b.w, th + 8, 8, C_RED, 0, 0);
    gfx_fill(c, b.x, b.y + th, b.w, 8, C_PANEL);
    gfx_fill(c, b.x, b.y + th, 1, 8, C_EDGE);
    gfx_fill(c, b.x + b.w - 1, b.y + th, 1, 8, C_EDGE);
    gfx_text(c, b.x + l->L, b.y + 4, l->fb, C_PANEL, title);
    int x = b.x + l->L, y = b.y + th + l->pad, bw = b.w - 2 * l->L;
    char line[120];
    snprintf(line, sizeof(line), "Pass %d of 2: %s", pass, pass == 1 ? "random data" : "zeros");
    gfx_text(c, x, y, l->fb, C_TEXT, line);
    y += l->L + 4;
    int barh = l->L;
    gfx_round_rect(c, x, y, bw, barh, 4, C_FREE, 1, C_LINE);
    int full = bw * MAX(0, MIN(percent, 100)) / 100;
    if (full > 0)
        gfx_round_rect(c, x, y, MAX(full, 8), barh, 4, C_RED, 0, 0);
    snprintf(line, sizeof(line), "%d%%", percent);
    gfx_text(c, x + (bw - gfx_text_width(l->fb, line)) / 2, y, l->fb, percent >= 50 ? C_PANEL : C_TEXT, line);
    y += barh + 8;
    gfx_text(c, x, y, l->f, C_TEXT, amount);
    wipe_stop_btn = (Rect){ 0, 0, gfx_text_width(l->f, "Esc") + 8 + gfx_text_width(l->f, "Stop") + 26, bh };
    wipe_stop_btn.x = b.x + b.w - l->L - wipe_stop_btn.w;
    wipe_stop_btn.y = b.y + b.h - l->pad - bh;
    dlg_button(g, &wipe_stop_btn, "Esc", "Stop", false);
    pal_gfx_show(c->px, c->w, b.x, b.y, b.x, b.y, b.w, b.h);
    pointer_show(g);
    logf("wipe %s pass %d %d%% %s stop at %d,%d", title, pass, percent, amount,
         wipe_stop_btn.x + wipe_stop_btn.w / 2, wipe_stop_btn.y + wipe_stop_btn.h / 2);
}

/* Esc, or a click on Stop, since the last look. */
static bool gui_wipe_stop(void)
{
    Gui *g = G;
    PalKey k;
    if (pal_con_read_key(&k, 0))
        return ui_is_esc(&k);
    PalPointer p;
    if (!g->pointer || !pal_pointer_read(&p))
        return false;
    bool pressed = p.left && !g->down;
    g->down = p.left;
    if (pointer_move(g, &p)) {
        pointer_show(g);
        logf("pointer %d,%d", g->px, g->py);
    }
    return pressed && inside(&wipe_stop_btn, g->px, g->py);
}

static void gui_redraw(View *v)
{
    redraw(G);
}

static const UiDialogs dialogs = { gui_message, gui_yesno, gui_input, gui_menu };
static const PmScreen screen = { gui_redraw, gui_wipe, gui_wipe_stop };

bool pm_gui(void)
{
    int w, h;
    if (!gfx_fonts_init() || !pal_gfx_open(&w, &h))
        return false;
    Gui g = { 0 };
    g.hover = -1;
    g.arrow = h > 1700 ? 2 : 1; /* with the fonts of 40 and 52 pixels */
    if (!gfx_canvas_init(&g.c, w, h) ||
        !gfx_canvas_init(&g.patch, GFX_ARROW_W * g.arrow, GFX_ARROW_H * g.arrow)) {
        gfx_canvas_free(&g.c);
        gfx_canvas_free(&g.patch);
        pal_gfx_close();
        return false;
    }
    G = &g;
    ui_dialogs = &dialogs;
    pm_screen = &screen;
    g.pointer = pal_pointer_open() > 0;
    logf("pointer devices: %s", pal_pointer_info());
    g.px = w / 2, g.py = h / 2;
    scan(&g);
    for (;;) {
        redraw(&g);
        PalKey k;
        int e = wait_input(&g, &k, true);
        if (e == 1 ? !key(&g, k) : !click(&g))
            break;
    }
    ui_dialogs = NULL;
    pm_screen = NULL;
    G = NULL;
    pal_pointer_close();
    pm_view_free(&g.v);
    free(g.sum);
    gfx_canvas_free(&g.patch);
    gfx_canvas_free(&g.c);
    pal_gfx_close();
    logf("closed");
    return true;
}
