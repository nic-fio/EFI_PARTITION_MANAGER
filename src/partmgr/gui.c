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

/* The font by the screen's height: the text grows on large screens (P23). */
static int font_size(int h)
{
    return h <= 600 ? 16 : h <= 900 ? 20 : h <= 1300 ? 26 : 32;
}

static int button_w(const Layout *l, int i)
{
    return gfx_text_width(l->f, buttons[i].key) + 8 + gfx_text_width(l->f, buttons[i].label) + 26;
}

/* The two rows of buttons: with the group names when they fit, else
 * without; a row that is still too wide gets narrower gaps. */
static void layout_buttons(Layout *l)
{
    int bh = l->L + 6, gap = 8;
    int y0 = l->H - 2 * (bh + 6) - 2;
    l->keys = (Rect){ 0, y0 - 4, l->W, l->H - y0 + 4 };
    int gw = MAX(gfx_text_width(l->f, "Partition:"), gfx_text_width(l->f, "Disk:")) + 20;
    for (int pass = 0; pass < 3; pass++) {
        l->group_w = pass == 0 ? gw : 0;
        gap = pass < 2 ? 8 : 3;
        bool fits = true;
        for (int row = 0; row < 2; row++) {
            int x = 14 + l->group_w;
            for (int i = 0; i < NBUTTONS; i++)
                if (buttons[i].row == row)
                    x += button_w(l, i) + gap;
            fits &= x - gap <= l->W - 8;
        }
        if (fits)
            break;
    }
    for (int row = 0; row < 2; row++) {
        int x = 14 + l->group_w, y = y0 + row * (bh + 6);
        for (int i = 0; i < NBUTTONS; i++)
            if (buttons[i].row == row) {
                l->btn[i] = (Rect){ x, y, button_w(l, i), bh };
                x += l->btn[i].w + gap;
            }
    }
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
    int pw = MAX(10 * l->L, MIN(14 * l->L, l->W * 300 / 1280));
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
    static const int frac[5] = { 0, 63, 215, 367, 658 }; /* of the width, as in the mockup */
    for (int i = 0; i < 5; i++)
        l->col[i] = l->table.x + 10 + l->table.w * frac[i] / 1000;
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
    gfx_round_rect(&g->c, r->x + 6, r->y + 4, kw, r->h - 8, 3, C_KEY, 0, 0);
    gfx_text(&g->c, r->x + 10, r->y + 3, l->f, C_PANEL, buttons[i].key);
    gfx_text(&g->c, r->x + 6 + kw + 8, r->y + 3, l->f, C_TEXT, buttons[i].label);
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

static int disk_height(const Gui *g, int i)
{
    return g->l.disk_h + (g->disks[i].boot || g->disks[i].readonly ? g->l.L : 0);
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
        gfx_text(&g->c, 14 + gfx_text_width(l->fb, "blk00") + l->pad, y + 4, l->fb, fg, g->disks[i].kind);
        text_right(&g->c, r->w - 14, y + 4, l->fb, fg, size);
        gfx_text(&g->c, 14, y + 4 + l->L, l->f, sub, l2);
        if (note)
            gfx_text(&g->c, 14, y + 4 + 2 * l->L, l->f, strong ? C_TITLE_NOTE : C_AMBER, note);
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
        if (gfx_text_width(l->f, lab) + 6 <= b - a)
            gfx_text(&g->c, a + 4, y + 4, l->f, r->free ? C_MUTED : C_TEXT, lab);
        if (p && h >= 2 * l->L) {
            pm_fmt_size(size, sizeof(size), p->size * v->d->dev.bsize);
            if (gfx_text_width(l->f, size) + 8 <= b - a)
                gfx_text(&g->c, a + 4, y + 4 + l->L, l->f, 0x3D444D, size);
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
    gfx_arrow(&g->patch, 0, 0, 1);
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

static void change_disk(Gui *g, int i)
{
    if (i < 0 || i >= g->ndisks || i == g->dsel)
        return;
    open_disk(g, i);
}

/* A key; false to quit. */
static bool key(Gui *g, PalKey k)
{
    View *v = &g->v;
    g->v.msg[0] = 0;
    if (ui_is_esc(&k))
        return false;
    if (k.ch == '\t') {
        g->focus_disks = !g->focus_disks;
        return true;
    }
    if (k.scan == KEY_F1 + 4) {
        scan(g);
        pm_say(v, false, "Disks read again.");
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
    int ch = k.ch < 128 ? toupper((int)k.ch) : 0;
    if ((ch && strchr("NDTRAWZXBS", ch)) || ui_is_enter(&k))
        pm_say(v, true, "Not in the graphical interface yet: the next stage brings the actions.");
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

static int button_at(const Gui *g, int x, int y)
{
    for (int i = 0; i < NBUTTONS; i++)
        if (inside(&g->l.btn[i], x, y))
            return i;
    return -1;
}

bool pm_gui(void)
{
    int w, h;
    if (!gfx_fonts_init() || !pal_gfx_open(&w, &h))
        return false;
    Gui g = { 0 };
    g.hover = -1;
    if (!gfx_canvas_init(&g.c, w, h) || !gfx_canvas_init(&g.patch, GFX_ARROW_W, GFX_ARROW_H)) {
        gfx_canvas_free(&g.c);
        pal_gfx_close();
        return false;
    }
    g.pointer = pal_pointer_open() > 0;
    logf("pointer devices: %s", pal_pointer_info());
    g.px = w / 2, g.py = h / 2;
    scan(&g);
    redraw(&g);
    for (;;) {
        PalKey k;
        if (pal_con_read_key(&k, 10)) {
            if (!key(&g, k))
                break;
            redraw(&g);
            continue;
        }
        PalPointer p;
        if (!g.pointer || !pal_pointer_read(&p))
            continue;
        int nx = p.abs ? (int)((long)p.ax * (w - 1) / 65535) : g.px + p.dx;
        int ny = p.abs ? (int)((long)p.ay * (h - 1) / 65535) : g.py + p.dy;
        nx = MAX(0, MIN(nx, w - 1)), ny = MAX(0, MIN(ny, h - 1));
        bool pressed = p.left && !g.down;
        g.down = p.left;
        if (nx != g.px || ny != g.py) {
            pointer_hide(&g);
            g.px = nx, g.py = ny;
            int hv = button_at(&g, nx, ny);
            if (hv != g.hover) {
                g.hover = hv;
                redraw(&g);
            } else {
                pointer_show(&g);
                logf("pointer %d,%d", g.px, g.py);
            }
        }
        if (pressed) {
            if (!click(&g))
                break;
            redraw(&g);
        }
    }
    pal_pointer_close();
    pm_view_free(&g.v);
    free(g.sum);
    gfx_canvas_free(&g.patch);
    gfx_canvas_free(&g.c);
    pal_gfx_close();
    logf("closed");
    return true;
}
