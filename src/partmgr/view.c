/* partmgr: one disk as the screens show it (view.h). */
#include "view.h"

void pm_say(View *v, bool err, const char *fmt, ...)
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

void pm_view_rows(View *v)
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

void pm_view_select(View *v, uint64_t start, bool is_free)
{
    for (int i = 0; i < v->nrows; i++)
        if (v->rows[i].start == start && v->rows[i].free == is_free)
            v->sel = i;
}

bool pm_view_load(View *v)
{
    if (v->loaded)
        pt_free(&v->t);
    v->loaded = !pt_read(&v->d->dev, &v->t);
    if (!v->loaded) {
        memset(&v->t, 0, sizeof(v->t));
        return false;
    }
    pm_view_rows(v);
    return true;
}

void pm_view_free(View *v)
{
    free(v->rows);
    v->rows = NULL;
    v->nrows = 0;
    if (v->loaded)
        pt_free(&v->t);
    v->loaded = false;
}

PtPart *pm_view_part(View *v)
{
    if (!v->nrows || v->rows[v->sel].free)
        return NULL;
    return &v->t.parts[v->rows[v->sel].part];
}

void pm_type_text(const View *v, const PtPart *p, char *out, size_t n)
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
