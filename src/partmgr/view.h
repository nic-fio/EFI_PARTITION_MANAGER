/* partmgr: one disk as the screens show it - its table in memory, the rows
 * (partitions and free areas in disk order), the selection and the message
 * line. The text screen (diskview.c) and the graphical window (gui.c)
 * share it. */
#ifndef PARTMGR_VIEW_H
#define PARTMGR_VIEW_H

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

/* Reads the table of the disk again (false when it cannot be read). */
bool pm_view_load(View *v);
void pm_view_free(View *v);
/* The rows again, after the table changed. */
void pm_view_rows(View *v);
/* Selects the row that starts at START (a partition, or a free area). */
void pm_view_select(View *v, uint64_t start, bool is_free);
/* The selected partition; NULL when a free area or nothing is selected. */
PtPart *pm_view_part(View *v);
/* The type of a partition as the screens write it. */
void pm_type_text(const View *v, const PtPart *p, char *out, size_t n);
/* The message line. */
void pm_say(View *v, bool err, const char *fmt, ...) __attribute__((format(printf, 3, 4)));

#endif
