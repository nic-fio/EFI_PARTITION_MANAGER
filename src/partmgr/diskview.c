/* partmgr: the screen of one disk. It shows the partitions and the free
 * areas in disk order, and every change - new partition, delete, type,
 * name, active flag, new table, delete table - is made on the table in
 * memory only: the screen shows the table as it will be, with * on what
 * changed, until Write writes it (decision P7). Backup reads the disk as it
 * is; restore and wipe write it at once, after their own warning. The
 * disk partmgr was started from, or a write-protected one, can only be
 * looked at and backed up. */
#include "partmgr.h"

typedef struct {
    bool free;
    int part;   /* index in t.parts */
    PtFree f;
    uint64_t start;
} Row;

typedef struct {
    PmDisk *d;
    PtTable t;
    bool loaded;
    Row *rows;
    int nrows, sel, top;
    char msg[240];
    bool msg_err;
} View;

/* the file of the last backup or restore, proposed again for the whole session */
static char last_file[200];

static uint64_t bs(const View *v)
{
    return v->d->dev.bsize;
}

static void say(View *v, bool err, const char *fmt, ...) __attribute__((format(printf, 3, 4)));
static void say(View *v, bool err, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(v->msg, sizeof(v->msg), fmt, ap);
    va_end(ap);
    v->msg_err = err;
}

static int row_cmp(const void *a, const void *b)
{
    const Row *x = a, *y = b;
    return x->start < y->start ? -1 : x->start > y->start;
}

/* The rows of the screen: partitions and free areas in disk order. */
static void build_rows(View *v)
{
    free(v->rows);
    PtFree *fr;
    int nf = pt_free_space(&v->t, &fr);
    v->rows = xcalloc(v->t.nparts + nf + 1, sizeof(Row));
    v->nrows = 0;
    for (int i = 0; i < v->t.nparts; i++)
        v->rows[v->nrows++] = (Row){ false, i, { 0 }, v->t.parts[i].start };
    for (int i = 0; i < nf; i++)
        v->rows[v->nrows++] = (Row){ true, -1, fr[i], fr[i].start };
    free(fr);
    qsort(v->rows, v->nrows, sizeof(Row), row_cmp);
    if (v->sel >= v->nrows)
        v->sel = v->nrows ? v->nrows - 1 : 0;
}

static void select_start(View *v, uint64_t start, bool is_free)
{
    for (int i = 0; i < v->nrows; i++)
        if (v->rows[i].start == start && v->rows[i].free == is_free)
            v->sel = i;
}

static bool load(View *v)
{
    if (v->loaded)
        pt_free(&v->t);
    v->loaded = !pt_read(&v->d->dev, &v->t);
    if (!v->loaded) {
        memset(&v->t, 0, sizeof(v->t));
        return false;
    }
    build_rows(v);
    return true;
}

static PtPart *selected_part(View *v)
{
    if (!v->nrows || v->rows[v->sel].free)
        return NULL;
    return &v->t.parts[v->rows[v->sel].part];
}

/* ---- drawing ---- */

static void type_text(const View *v, const PtPart *p, char *out, size_t n)
{
    const char *tn = pt_type_name(&v->t, p);
    if (tn)
        snprintf(out, n, "%s", tn);
    else if (v->t.kind == PT_GPT) {
        char g[37];
        pt_guid_str(p->type_guid, g);
        snprintf(out, n, "%.18s...", g);
    } else
        snprintf(out, n, "type %02X", p->mbr_type);
}

static void draw(View *v)
{
    PmDisk *d = v->d;
    char size[32], left[200], right[80];
    pm_fmt_size(size, sizeof(size), d->size);
    snprintf(left, sizeof(left), "EFI Partition Manager %s   %s  %s  %s  %s", PARTMGR_VERSION, d->name, d->kind, size,
             pm_table_name(v->t.kind));
    snprintf(right, sizeof(right), "%s", d->boot ? "started from here: read only"
                                         : d->readonly ? "write-protected"
                                         : v->t.changed ? "* = changes not written" : "");
    ui_title(left, right);
    ui_clear_body();
    bool gpt = v->t.kind == PT_GPT;
    ui_textf(0, 2, ui_cols, WHITE, BLACK, "  %-4s %-11s %-11s %-22s %-8s %s", "#", "Start", "Size", "Type",
             gpt ? "" : "Flags", gpt ? "Name" : "");
    int first = 3, notes = v->t.nnotes, status = ui_rows - 3;
    int visible = status - first - (notes ? notes + 1 : 0);
    if (visible < 1)
        visible = 1;
    if (v->sel < v->top)
        v->top = v->sel;
    if (v->sel >= v->top + visible)
        v->top = v->sel - visible + 1;
    if (!v->nrows)
        ui_text(2, first, ui_cols - 2, LIGHTGRAY, BLACK,
                v->t.kind == PT_NONE ? "No partition table." : "No free space.");
    for (int i = v->top; i < v->nrows && i < v->top + visible; i++) {
        const Row *r = &v->rows[i];
        char st[32], sz[32], line[300];
        bool hl = i == v->sel;
        int fg = hl ? BLACK : LIGHTGRAY, bg = hl ? CYAN : BLACK;
        if (r->free) {
            pm_fmt_size(st, sizeof(st), r->f.start * bs(v));
            pm_fmt_size(sz, sizeof(sz), r->f.size * bs(v));
            snprintf(line, sizeof(line), "  %-4s %-11s %-11s %s", "-", st, sz,
                     r->f.logical ? "free space (for logical partitions)" : "free space");
            if (!hl)
                fg = DARKGRAY;
        } else {
            const PtPart *p = &v->t.parts[r->part];
            char type[48], flags[16] = "", num[8];
            type_text(v, p, type, sizeof(type));
            if (!gpt)
                snprintf(flags, sizeof(flags), "%s",
                         p->active ? "active" : p->role == PT_LOGICAL ? "logical" : "");
            snprintf(num, sizeof(num), "%d%s", p->num, p->changed ? "*" : "");
            pm_fmt_size(st, sizeof(st), p->start * bs(v));
            pm_fmt_size(sz, sizeof(sz), p->size * bs(v));
            snprintf(line, sizeof(line), "  %-4s %-11s %-11s %-22s %-8s %s", num, st, sz, type, flags,
                     gpt ? p->name : "");
            if (p->changed && !hl)
                fg = YELLOW;
        }
        ui_text(0, first + i - v->top, ui_cols, fg, bg, line);
    }
    for (int i = 0; i < notes; i++)
        ui_textf(0, status - notes + i, ui_cols, YELLOW, BLACK, "  Note: %s", v->t.notes[i]);
    if (v->msg[0])
        ui_textf(0, status, ui_cols, v->msg_err ? LIGHTRED : LIGHTGREEN, BLACK, "  %s", v->msg);
    /* the two key bars: what applies to the selected row, what applies to the
     * whole disk; keys that do not apply now are dimmed in place */
    const PtPart *p = selected_part(v);
    bool fr = v->nrows && v->rows[v->sel].free, rw = !d->boot && !d->readonly, part = p != NULL;
    bool plain = part && p->role != PT_EXTENDED;
    UiKey pk[] = {
        { "N", "New", rw && fr }, { "D", "Delete", rw && part }, { "T", "Type", rw && plain },
        { "R", "Rename", rw && part && gpt }, { "A", "Active", rw && plain && v->t.kind == PT_MBR },
        { "W", "Wipe", rw && plain && !v->t.changed },
    };
    UiKey dk[] = {
        { "Enter", "Write", rw && v->t.changed }, { "Z", "New table", rw }, { "X", "Delete table", rw },
        { "B", "Backup", true }, { "S", "Restore", rw }, { "Esc", "Back", true },
    };
    int gw = ui_cols >= 84 ? 11 : 0; /* align the two groups when there is room */
    ui_keybar(1, "Partition:", gw ? gw : 11, pk, (int)ARRAY_SIZE(pk));
    ui_keybar(0, "Disk:", gw ? gw : 6, dk, (int)ARRAY_SIZE(dk));
}

/* ---- changes in memory ---- */

static bool may_change(View *v)
{
    if (v->d->boot) {
        ui_message(false, "Read only", "partmgr was started from this disk: it cannot be changed.");
        return false;
    }
    if (v->d->readonly) {
        ui_message(false, "Read only", "The disk is write-protected.");
        return false;
    }
    return true;
}

static void draw(View *v);

/* Chooses a type from the list, or types one ("Other..."). */
static bool choose_type(View *v, uint8_t *mbr, uint8_t guid[16], int current)
{
    draw(v);
    const char *items[64];
    uint8_t types[64], guids[64][16];
    int n = 0;
    while (n < 62 && pt_type_at(v->t.kind, n, &items[n], &types[n], guids[n]))
        n++;
    items[n++] = v->t.kind == PT_GPT ? "Other (type GUID)..." : "Other (type byte)...";
    int c = ui_menu("Type", items, n, current);
    if (c < 0)
        return false;
    if (c < n - 1) {
        *mbr = types[c];
        memcpy(guid, guids[c], 16);
        return true;
    }
    char buf[64] = "";
    draw(v);
    if (v->t.kind == PT_GPT) {
        if (!ui_input(false, "Type", "Type GUID:", buf,
                      sizeof(buf)))
            return false;
        if (!pt_guid_parse(buf, guid)) {
            ui_message(true, "Type", "Not a GUID.");
            return false;
        }
        return true;
    }
    if (!ui_input(false, "Type", "Type byte (hex):", buf, sizeof(buf)))
        return false;
    char *end;
    unsigned long b = strtoul(buf, &end, 16);
    if (!buf[0] || *end || b == 0 || b > 0xFF) {
        ui_message(true, "Type", "A byte from 01 to FF.");
        return false;
    }
    *mbr = (uint8_t)b;
    return true;
}

static int type_index(const View *v, const PtPart *p)
{
    const char *name;
    uint8_t mbr, guid[16];
    for (int i = 0; pt_type_at(v->t.kind, i, &name, &mbr, guid); i++)
        if (v->t.kind == PT_GPT ? !memcmp(guid, p->type_guid, 16) : mbr == p->mbr_type)
            return i;
    return 0;
}

static void new_partition(View *v, const PtFree *f)
{
    if (!may_change(v))
        return;
    uint64_t al = pt_align(&v->t), b = bs(v), f_end = f->start + f->size - 1;
    PtPart req = { 0 };
    req.role = PT_PRIMARY;
    if (v->t.kind == PT_MBR) {
        bool ext = false;
        for (int i = 0; i < v->t.nparts; i++)
            ext |= v->t.parts[i].role == PT_EXTENDED;
        if (f->logical)
            req.role = PT_LOGICAL;
        else if (!ext) {
            static const char *const kinds[] = { "Primary", "Logical (creates the extended partition)" };
            int c = ui_menu("New partition", kinds, 2, 0);
            if (c < 0)
                return;
            req.role = c ? PT_LOGICAL : PT_PRIMARY;
        }
    }
    /* a logical partition that creates the extended one leaves room for its record */
    uint64_t first = f->start + (req.role == PT_LOGICAL && !f->logical ? al : 0);
    if (first > f_end) {
        ui_message(true, "New partition", "The free area is too small.");
        return;
    }
    char fs[32], fe[32], buf[64], text[300];
    pm_fmt_exact(fs, sizeof(fs), first * b);
    pm_fmt_size(fe, sizeof(fe), (f_end + 1) * b);
    snprintf(text, sizeof(text), "Start (free: %s to %s):", fs, fe);
    snprintf(buf, sizeof(buf), "%s", fs);
    draw(v);
    if (!ui_input(false, "New partition", text, buf, sizeof(buf)))
        return;
    uint64_t bytes;
    bool rest;
    const char *err = pm_parse_size(buf, (uint32_t)b, &bytes, &rest);
    if (!err && rest)
        err = "the start must be a position, such as 1 MiB";
    if (err) {
        ui_message(true, "New partition", err);
        return;
    }
    uint64_t start = (bytes + b - 1) / b;
    start = (start + al - 1) / al * al; /* partitions start on 1 MiB boundaries */
    if (start < first || start > f_end) {
        snprintf(text, sizeof(text), "The start must be in the free area, from %s.", fs);
        ui_message(true, "New partition", text);
        return;
    }
    char avail[32];
    pm_fmt_size(avail, sizeof(avail), (f_end - start + 1) * b);
    snprintf(text, sizeof(text), "Size (512M, 20G..., rest = %s):", avail);
    snprintf(buf, sizeof(buf), "rest");
    draw(v);
    if (!ui_input(false, "New partition", text, buf, sizeof(buf)))
        return;
    err = pm_parse_size(buf, (uint32_t)b, &bytes, &rest);
    if (err) {
        ui_message(true, "New partition", err);
        return;
    }
    uint64_t size = rest ? f_end - start + 1 : bytes / b;
    if (!size) {
        ui_message(true, "New partition", "The size is zero.");
        return;
    }
    if (size > f_end - start + 1) {
        snprintf(text, sizeof(text), "Too big: at most %s.", avail);
        ui_message(true, "New partition", text);
        return;
    }
    req.start = start;
    req.size = size;
    if (!choose_type(v, &req.mbr_type, req.type_guid, 0))
        return;
    if (v->t.kind == PT_GPT) {
        buf[0] = 0;
        draw(v);
        if (!ui_input(false, "New partition", "Name (optional, 36 characters):",
                      buf, sizeof(buf)))
            return;
        snprintf(req.name, sizeof(req.name), "%s", buf);
    }
    err = pt_add(&v->t, &v->d->dev, &req);
    if (err) {
        ui_message(true, "New partition", err);
        return;
    }
    build_rows(v);
    select_start(v, start, false);
    PtPart *p = selected_part(v);
    say(v, false, "Partition %d added.", p ? p->num : 0);
}

static void delete_partition(View *v, PtPart *p)
{
    if (!may_change(v))
        return;
    int num = p->num;
    if (p->role == PT_EXTENDED) {
        int logs = 0;
        for (int i = 0; i < v->t.nparts; i++)
            logs += v->t.parts[i].role == PT_LOGICAL;
        char text[200];
        snprintf(text, sizeof(text), "Delete the extended partition and its %d logical partition%s? (Y/N)", logs,
                 logs == 1 ? "" : "s");
        if (logs && !ui_yesno(true, "Delete partition", text))
            return;
    }
    const char *err = pt_delete(&v->t, num);
    if (err) {
        ui_message(true, "Delete partition", err);
        return;
    }
    build_rows(v);
    say(v, false, "Partition %d deleted.", num);
}

static void change_type(View *v, PtPart *p)
{
    if (!may_change(v))
        return;
    if (p->role == PT_EXTENDED) {
        ui_message(false, "Type", "The extended partition keeps its type.");
        return;
    }
    uint8_t mbr = 0, guid[16] = { 0 };
    if (!choose_type(v, &mbr, guid, type_index(v, p)))
        return;
    int num = p->num;
    const char *err = pt_set_type(&v->t, num, mbr, guid);
    if (err)
        ui_message(true, "Type", err);
    else
        say(v, false, "Partition %d: type changed.", num);
}

static void rename_partition(View *v, PtPart *p)
{
    if (v->t.kind != PT_GPT || !may_change(v))
        return;
    char buf[112];
    snprintf(buf, sizeof(buf), "%s", p->name);
    if (!ui_input(false, "Rename", "Name (36 characters):", buf, sizeof(buf)))
        return;
    int num = p->num;
    const char *err = pt_set_name(&v->t, num, buf);
    if (err)
        ui_message(true, "Rename", err);
    else
        say(v, false, "Partition %d renamed.", num);
}

static void toggle_active(View *v, PtPart *p)
{
    if (v->t.kind != PT_MBR || !may_change(v))
        return;
    int num = p->num;
    bool on = !p->active;
    const char *err = pt_set_active(&v->t, num, on);
    if (err)
        ui_message(true, "Active", err);
    else
        say(v, false, on ? "Partition %d active." : "Partition %d no longer active.", num);
}

static void new_table(View *v)
{
    if (!may_change(v))
        return;
    static const char *const kinds[] = { "GPT", "MBR" };
    int c = ui_menu("New partition table", kinds, 2, 0);
    if (c < 0)
        return;
    pt_new(&v->t, &v->d->dev, c ? PT_MBR : PT_GPT);
    v->sel = 0;
    build_rows(v);
    say(v, false, "New empty %s table.", c ? "MBR" : "GPT");
}

static void delete_table(View *v)
{
    if (!may_change(v))
        return;
    pt_new(&v->t, &v->d->dev, PT_NONE);
    v->sel = 0;
    build_rows(v);
    say(v, false, "Partition table deleted.");
}

/* ---- writing ---- */

/* The red confirmation of every operation that writes to the disk: one line
 * ending in (Y/N). Enter does nothing there, so a key pressed twice cannot
 * write. */
static bool confirm_destroy(View *v, const char *title, const char *question)
{
    draw(v);
    if (ui_yesno(true, title, question))
        return true;
    say(v, false, "Nothing was written.");
    return false;
}

static void write_table(View *v)
{
    if (!v->t.changed) {
        say(v, false, "No changes to write.");
        return;
    }
    if (!may_change(v))
        return;
    char size[32], q[200];
    pm_fmt_size(size, sizeof(size), v->d->size);
    if (v->t.kind == PT_NONE)
        snprintf(q, sizeof(q), "Delete the partition table of %s (%s)? (Y/N)", v->d->name, size);
    else
        snprintf(q, sizeof(q), "Write the %s table to %s (%s)? (Y/N)", pm_table_name(v->t.kind), v->d->name, size);
    if (!confirm_destroy(v, "Write", q))
        return;
    int rc = pt_write(&v->d->dev, &v->t);
    if (rc) {
        ui_message(true, "Write", pal_strerror(rc));
        say(v, true, "Not written: %s.", pal_strerror(rc));
        return;
    }
    load(v);
    say(v, false, "Written.");
}

/* ---- wipe ---- */

typedef struct {
    View *v;
    int num;
    uint64_t bytes;     /* of the partition */
    uint64_t t0, drawn; /* ms */
    int pass;
} WipeUi;

static void fmt_time(char *out, size_t n, uint64_t s)
{
    if (s >= 3600)
        snprintf(out, n, "about %llu h %llu min left", (unsigned long long)(s / 3600),
                 (unsigned long long)(s % 3600 / 60));
    else if (s >= 60)
        snprintf(out, n, "about %llu min %llu s left", (unsigned long long)(s / 60), (unsigned long long)(s % 60));
    else
        snprintf(out, n, "about %llu s left", (unsigned long long)s);
}

/* The progress box: pass, bar, percentage, amount, speed, time left. */
static void draw_wipe(WipeUi *w, int pass, uint64_t done, uint64_t total)
{
    int bw = MIN(ui_cols - 4, 64), col = (ui_cols - bw) / 2, row = MAX(1, (ui_rows - 8) / 2);
    uint64_t bs = w->v->d->dev.bsize, now = pal_ticks_ms(), ms = now - w->t0;
    char size[32], title[120], line[200], amount[32], speed[32], left[48];
    pm_fmt_size(size, sizeof(size), w->bytes);
    snprintf(title, sizeof(title), " Wiping %s partition %d  (%s)", w->v->d->name, w->num, size);
    ui_text(col, row, bw, YELLOW, RED, title);
    ui_text(col, row + 1, bw, WHITE, RED, "");
    ui_textf(col, row + 2, bw, WHITE, RED, "  Pass %d of 2: %s", pass, pass == 1 ? "random data" : "zeros");
    int barw = bw - 12, pct = total ? (int)(done * 100 / total) : 100, full = total ? (int)(done * barw / total) : barw;
    Sbuf b;
    sb_init(&b);
    sb_adds(&b, "  [");
    for (int i = 0; i < barw; i++)
        sb_adds(&b, i < full ? "\u2588" : "\u2591");
    sb_printf(&b, "] %3d%%", pct);
    ui_text(col, row + 3, bw, WHITE, RED, b.s);
    sb_free(&b);
    /* both passes count for the speed and the time left */
    uint64_t written = ((uint64_t)(pass - 1) * total + done) * bs, remaining = (2 * total) * bs - written;
    pm_fmt_size(amount, sizeof(amount), done * bs);
    if (ms >= 500 && written) {
        uint64_t rate = written * 1000 / ms; /* bytes per second */
        pm_fmt_size(speed, sizeof(speed), rate);
        fmt_time(left, sizeof(left), rate ? remaining / rate : 0);
        snprintf(line, sizeof(line), "  %s of %s   %s/s   %s", amount, size, speed, left);
    } else
        snprintf(line, sizeof(line), "  %s of %s   estimating the time...", amount, size);
    ui_text(col, row + 4, bw, WHITE, RED, line);
    ui_text(col, row + 5, bw, WHITE, RED, "");
    ui_text(col, row + 6, bw, WHITE, RED, "  Esc: stop");
    w->drawn = now;
}

static bool wipe_progress(void *ctx, int pass, uint64_t done, uint64_t total)
{
    WipeUi *w = ctx;
    PalKey k;
    if (pal_con_read_key(&k, 0) && ui_is_esc(&k)) {
        if (ui_yesno(true, "Wipe", "Stop the wipe? (Y/N)"))
            return false;
        w->drawn = 0; /* the box is drawn again below */
    }
    /* every pass start and end, else five times a second */
    if (done == 0 || done == total || pass != w->pass || pal_ticks_ms() - w->drawn >= 200)
        draw_wipe(w, pass, done, total);
    w->pass = pass;
    return true;
}

static void wipe_partition(View *v, PtPart *p)
{
    if (!may_change(v))
        return;
    const char *err = pt_can_wipe(&v->t, p->num);
    if (err) {
        ui_message(false, "Wipe", err);
        return;
    }
    char size[32], type[48], q[300];
    pm_fmt_size(size, sizeof(size), p->size * bs(v));
    type_text(v, p, type, sizeof(type));
    snprintf(q, sizeof(q), "Wipe partition %d of %s (%s, %s%s%s%s)? (Y/N)", p->num, v->d->name, type, size,
             p->name[0] ? ", \"" : "", p->name, p->name[0] ? "\"" : "");
    if (!confirm_destroy(v, "Wipe", q))
        return;
    WipeUi w = { v, p->num, p->size * bs(v), pal_ticks_ms(), 0, 0 };
    draw(v);
    int rc = pt_wipe(&v->d->dev, p->start, p->size, wipe_progress, &w);
    uint64_t secs = (pal_ticks_ms() - w.t0 + 500) / 1000;
    if (rc == PAL_EABORT)
        say(v, true, "Wipe of partition %d stopped in pass %d: partly overwritten.", w.num, w.pass);
    else if (rc)
        say(v, true, "Wipe of partition %d failed: %s.", w.num, pal_strerror(rc));
    else
        say(v, false, "Partition %d wiped (%s, 2 passes, %llu s).", w.num, size, (unsigned long long)secs);
}

/* ---- backup and restore ---- */

/* The volume for backup files: the one partmgr was started from if it can be
 * written, else the first one that can. */
static int backup_volume(void)
{
    pal_volumes_refresh();
    int b = pal_boot_volume();
    if (b >= 0 && !pal_volume(b)->readonly)
        return b;
    for (int i = 0; i < pal_volume_count(); i++)
        if (!pal_volume(i)->readonly)
            return i;
    return -1;
}

static void volumes_text(char *out, size_t n)
{
    Sbuf s;
    sb_init(&s);
    for (int i = 0; i < pal_volume_count(); i++)
        sb_printf(&s, "%s%s%s", i ? ", " : "", pal_volume(i)->name, pal_volume(i)->readonly ? " (ro)" : "");
    snprintf(out, n, "%s", s.len ? s.s : "none");
    sb_free(&s);
}

/* "FS1:/dir/file" -> "fs1:\dir\file"; NULL if there is no volume name. */
static char *canonical(const char *in)
{
    const char *c = strchr(in, ':');
    if (!c || c == in)
        return NULL;
    Sbuf s;
    sb_init(&s);
    for (const char *p = in; p < c; p++)
        sb_putc(&s, (char)tolower((uint8_t)*p));
    sb_putc(&s, ':');
    if (c[1] != '\\' && c[1] != '/')
        sb_putc(&s, '\\');
    for (const char *p = c + 1; *p; p++)
        sb_putc(&s, *p == '/' ? '\\' : *p);
    return sb_steal(&s);
}

static bool ask_file(View *v, const char *title, const char *intro, char *buf, size_t n)
{
    char vols[200], text[500];
    volumes_text(vols, sizeof(vols));
    snprintf(text, sizeof(text), "%s\nVolumes: %s", intro, vols);
    return ui_input(false, title, text, buf, n);
}

static void backup(View *v)
{
    int vi = backup_volume();
    char buf[200];
    if (last_file[0])
        snprintf(buf, sizeof(buf), "%s", last_file);
    else if (vi >= 0)
        snprintf(buf, sizeof(buf), "%s:\\partmgr-%s.bin", pal_volume(vi)->name, v->d->name);
    else
        buf[0] = 0;
    if (!ask_file(v, "Backup", "Backup of the table on the disk, to file:", buf, sizeof(buf)))
        return;
    char *path = canonical(buf);
    if (!path) {
        ui_message(true, "Backup", "The file needs its volume, e.g. fs1:\\table.bin.");
        return;
    }
    PalStat st;
    if (!pal_stat(path, &st) && !ui_yesno(true, "Backup", "The file exists. Replace it? (Y/N)")) {
        free(path);
        return;
    }
    PtTable disk;
    uint8_t *data = NULL;
    size_t len = 0;
    int rc = pt_read(&v->d->dev, &disk);
    if (!rc) {
        if (disk.kind == PT_NONE)
            rc = PAL_ENOENT;
        else
            rc = pt_backup(&v->d->dev, &disk, &data, &len);
        pt_free(&disk);
    }
    if (rc == PAL_ENOENT) {
        ui_message(true, "Backup", "No partition table to back up.");
        free(path);
        return;
    }
    PalFile *f = NULL;
    if (!rc)
        rc = pal_open(path, PAL_O_WRITE | PAL_O_CREATE | PAL_O_TRUNC, &f);
    if (!rc) {
        rc = pal_write(f, data, len);
        int rc2 = pal_close(f);
        if (!rc)
            rc = rc2;
    }
    free(data);
    if (rc) {
        char text[300];
        snprintf(text, sizeof(text), "%s: %s.", path, pal_strerror(rc));
        ui_message(true, "Backup", text);
    } else {
        snprintf(last_file, sizeof(last_file), "%s", path);
        say(v, false, "Saved %s (%zu bytes).", path, len);
    }
    free(path);
}

static void restore(View *v)
{
    if (!may_change(v))
        return;
    char buf[200];
    snprintf(buf, sizeof(buf), "%s", last_file);
    if (!ask_file(v, "Restore", "Restore from file:", buf, sizeof(buf)))
        return;
    char *path = canonical(buf);
    if (!path) {
        ui_message(true, "Restore", "The file needs its volume, e.g. fs1:\\table.bin.");
        return;
    }
    PalStat st;
    PalFile *f = NULL;
    uint8_t *data = NULL;
    int rc = pal_stat(path, &st);
    if (!rc && st.size > 64 * 1024 * 1024)
        rc = PAL_EINVAL;
    if (!rc)
        rc = pal_open(path, PAL_O_READ, &f);
    if (!rc) {
        data = xmalloc(st.size ? st.size : 1);
        size_t got = 0;
        rc = pal_read(f, data, st.size, &got);
        if (!rc && got != st.size)
            rc = PAL_EIO;
        pal_close(f);
    }
    if (rc) {
        char text[300];
        snprintf(text, sizeof(text), "%s could not be read: %s.", path, pal_strerror(rc));
        ui_message(true, "Restore", text);
        free(data);
        free(path);
        return;
    }
    char q[400];
    snprintf(q, sizeof(q), "Restore %s to %s?%s (Y/N)", path, v->d->name, v->t.changed ? " Unwritten changes are lost." : "");
    if (confirm_destroy(v, "Restore", q)) {
        const char *err = pt_restore(&v->d->dev, data, st.size);
        if (err)
            ui_message(true, "Restore", err);
        load(v);
        if (!err) {
            snprintf(last_file, sizeof(last_file), "%s", path);
            say(v, false, "Restored from %s.", path);
        }
    }
    free(data);
    free(path);
}

/* ---- the screen ---- */

void pm_disk_screen(PmDisk *d)
{
    View v = { 0 };
    v.d = d;
    if (!load(&v)) {
        ui_message(true, d->name, "The disk could not be read.");
        return;
    }
    for (;;) {
        ui_init();
        draw(&v);
        PalKey k = ui_key();
        v.msg[0] = 0;
        PtPart *p = selected_part(&v);
        Row *r = v.nrows ? &v.rows[v.sel] : NULL;
        int ch = k.ch < 128 ? toupper((int)k.ch) : 0;
        if (ui_is_esc(&k)) {
            if (!v.t.changed || ui_yesno(true, "Leave the disk", "Discard the unwritten changes? (Y/N)"))
                break;
        } else if (k.scan == KEY_UP && v.sel > 0)
            v.sel--;
        else if (k.scan == KEY_DOWN && v.sel + 1 < v.nrows)
            v.sel++;
        else if (k.scan == KEY_HOME)
            v.sel = 0;
        else if (k.scan == KEY_END && v.nrows)
            v.sel = v.nrows - 1;
        else if (ui_is_enter(&k))
            write_table(&v);
        else if (ch == 'N' && r && r->free)
            new_partition(&v, &r->f);
        else if (ch == 'N')
            say(&v, true, v.t.kind == PT_NONE ? "No partition table: Z makes one." : "Select a free area.");
        else if (ch == 'D' && p)
            delete_partition(&v, p);
        else if (ch == 'T' && p)
            change_type(&v, p);
        else if (ch == 'R' && p)
            rename_partition(&v, p);
        else if (ch == 'A' && p)
            toggle_active(&v, p);
        else if (ch == 'W' && p)
            wipe_partition(&v, p);
        else if (ch == 'Z')
            new_table(&v);
        else if (ch == 'X')
            delete_table(&v);
        else if (ch == 'B')
            backup(&v);
        else if (ch == 'S')
            restore(&v);
    }
    free(v.rows);
    if (v.loaded)
        pt_free(&v.t);
}
