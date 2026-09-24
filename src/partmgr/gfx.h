/* partmgr: drawing for the graphical interface (decisions P18-P25). A canvas
 * is an image in memory; the platform layer copies it to the screen
 * (pal_gfx_show). Portable: the tests draw the same canvases on Linux.
 *
 * Colours are 0xRRGGBB, the layout of a UEFI Blt pixel read as a
 * little-endian 32-bit number. No floating point: the UEFI build has none. */
#ifndef PARTMGR_GFX_H
#define PARTMGR_GFX_H

#include "../lib/rt.h"

typedef uint32_t GfxColor;

typedef struct {
    int x, y, w, h;
} GfxRect;

typedef struct {
    uint32_t *px; /* w * h pixels, row after row */
    int w, h;
    GfxRect clip; /* nothing is drawn outside it */
} GfxCanvas;

bool gfx_canvas_init(GfxCanvas *c, int w, int h); /* false when out of memory */
void gfx_canvas_free(GfxCanvas *c);
void gfx_clip(GfxCanvas *c, int x, int y, int w, int h); /* inside the canvas */
void gfx_unclip(GfxCanvas *c);

void gfx_fill(GfxCanvas *c, int x, int y, int w, int h, GfxColor col);
/* A frame of THICK pixels inside the rectangle. */
void gfx_frame(GfxCanvas *c, int x, int y, int w, int h, int thick, GfxColor col);
/* A rectangle with rounded corners, smoothed; BORDER pixels of BORDER_COL
 * around FILL (border 0: no border). */
void gfx_round_rect(GfxCanvas *c, int x, int y, int w, int h, int radius, GfxColor fill, int border,
                    GfxColor border_col);
/* Diagonal lines ("/") every STEP pixels, one pixel wide: the free space. */
void gfx_hatch(GfxCanvas *c, int x, int y, int w, int h, int step, GfxColor col);

/* The mouse pointer: an arrow, white with a black outline, its tip at X, Y,
 * SCALE times the size of GFX_ARROW_W x GFX_ARROW_H. */
#define GFX_ARROW_W 12
#define GFX_ARROW_H 19
void gfx_arrow(GfxCanvas *c, int x, int y, int scale);

/* ---- Text: Inter, rendered by tools/gen-font.py into font_inter.bin ---- */

typedef struct GfxFont GfxFont;

/* Checks the embedded glyphs; false when they are damaged. */
bool gfx_fonts_init(void);
/* The face of the largest size not above PX (the smallest if none), regular
 * or semibold. */
const GfxFont *gfx_font(int px, bool bold);
int gfx_font_size(const GfxFont *f);
int gfx_line_height(const GfxFont *f); /* the height of one line of text */

/* Draws UTF-8 TEXT in the line whose top is Y; returns the x after it.
 * Characters the font lacks are drawn as "?". */
int gfx_text(GfxCanvas *c, int x, int y, const GfxFont *f, GfxColor col, const char *text);
int gfx_text_width(const GfxFont *f, const char *text);

#endif
