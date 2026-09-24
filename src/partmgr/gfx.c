/* partmgr: drawing on a canvas in memory, and text with the glyphs of
 * font_inter.bin (its format is described in tools/gen-font.py). Integer
 * arithmetic only. */
#include "gfx.h"

/* the glyph file, linked in by font_inter.S */
extern const uint8_t font_inter_data[], font_inter_end[];

static inline uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }
static inline uint32_t rd32(const uint8_t *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }

/* ---- canvas ---- */

bool gfx_canvas_init(GfxCanvas *c, int w, int h)
{
    c->px = malloc((size_t)w * (size_t)h * 4);
    c->w = w, c->h = h;
    gfx_unclip(c);
    return c->px != NULL;
}

void gfx_canvas_free(GfxCanvas *c)
{
    free(c->px);
    c->px = NULL;
}

void gfx_unclip(GfxCanvas *c)
{
    c->clip = (GfxRect){ 0, 0, c->w, c->h };
}

void gfx_clip(GfxCanvas *c, int x, int y, int w, int h)
{
    int x1 = MIN(x + w, c->w), y1 = MIN(y + h, c->h);
    x = MAX(x, 0), y = MAX(y, 0);
    c->clip = (GfxRect){ x, y, MAX(x1 - x, 0), MAX(y1 - y, 0) };
}

/* Cuts the rectangle to the clip; false when nothing is left. */
static bool cut(const GfxCanvas *c, int *x, int *y, int *w, int *h)
{
    int x0 = MAX(*x, c->clip.x), y0 = MAX(*y, c->clip.y);
    int x1 = MIN(*x + *w, c->clip.x + c->clip.w), y1 = MIN(*y + *h, c->clip.y + c->clip.h);
    if (x1 <= x0 || y1 <= y0)
        return false;
    *x = x0, *y = y0, *w = x1 - x0, *h = y1 - y0;
    return true;
}

/* BG towards FG by A/MAX. */
static inline GfxColor mix(GfxColor bg, GfxColor fg, unsigned a, unsigned max)
{
    if (a >= max)
        return fg;
    if (!a)
        return bg;
    unsigned r = (bg >> 16 & 255) * (max - a) + (fg >> 16 & 255) * a;
    unsigned g = (bg >> 8 & 255) * (max - a) + (fg >> 8 & 255) * a;
    unsigned b = (bg & 255) * (max - a) + (fg & 255) * a;
    return (r + max / 2) / max << 16 | (g + max / 2) / max << 8 | (b + max / 2) / max;
}

void gfx_fill(GfxCanvas *c, int x, int y, int w, int h, GfxColor col)
{
    if (!cut(c, &x, &y, &w, &h))
        return;
    for (int j = 0; j < h; j++) {
        uint32_t *p = c->px + (size_t)(y + j) * (size_t)c->w + (size_t)x;
        for (int i = 0; i < w; i++)
            p[i] = col;
    }
}

void gfx_frame(GfxCanvas *c, int x, int y, int w, int h, int thick, GfxColor col)
{
    thick = MIN(thick, MIN(w, h) / 2 + 1);
    gfx_fill(c, x, y, w, thick, col);
    gfx_fill(c, x, y + h - thick, w, thick, col);
    gfx_fill(c, x, y + thick, thick, h - 2 * thick, col);
    gfx_fill(c, x + w - thick, y + thick, thick, h - 2 * thick, col);
}

/* How much of pixel (PX, PY) the rounded rectangle covers, 0..16, from 4 x 4
 * samples; coordinates in eighths of a pixel. */
static unsigned coverage(int px, int py, int x, int y, int w, int h, int r)
{
    if (w <= 0 || h <= 0 || px < x || py < y || px >= x + w || py >= y + h)
        return 0;
    int cx, cy;
    if (px < x + r)
        cx = x + r;
    else if (px >= x + w - r)
        cx = x + w - r;
    else
        return 16;
    if (py < y + r)
        cy = y + r;
    else if (py >= y + h - r)
        cy = y + h - r;
    else
        return 16;
    unsigned n = 0;
    long rr = (long)r * 8 * r * 8;
    for (int j = 0; j < 4; j++)
        for (int i = 0; i < 4; i++) {
            long dx = (long)px * 8 + 1 + 2 * i - (long)cx * 8, dy = (long)py * 8 + 1 + 2 * j - (long)cy * 8;
            n += dx * dx + dy * dy <= rr;
        }
    return n;
}

void gfx_round_rect(GfxCanvas *c, int x, int y, int w, int h, int radius, GfxColor fill, int border,
                    GfxColor border_col)
{
    radius = MAX(0, MIN(radius, MIN(w, h) / 2));
    int cx = x, cy = y, cw = w, ch = h;
    if (!cut(c, &cx, &cy, &cw, &ch))
        return;
    int ir = MAX(radius - border, 0);
    for (int py = cy; py < cy + ch; py++) {
        uint32_t *row = c->px + (size_t)py * (size_t)c->w;
        for (int px = cx; px < cx + cw; px++) {
            unsigned outer = coverage(px, py, x, y, w, h, radius);
            if (!outer)
                continue;
            GfxColor v = row[px];
            if (border) {
                v = mix(v, border_col, outer, 16);
                v = mix(v, fill, coverage(px, py, x + border, y + border, w - 2 * border, h - 2 * border, ir), 16);
            } else {
                v = mix(v, fill, outer, 16);
            }
            row[px] = v;
        }
    }
}

void gfx_hatch(GfxCanvas *c, int x, int y, int w, int h, int step, GfxColor col)
{
    if (step < 2 || !cut(c, &x, &y, &w, &h))
        return;
    for (int py = y; py < y + h; py++)
        for (int px = x; px < x + w; px++)
            if ((px + py) % step == 0)
                c->px[(size_t)py * (size_t)c->w + (size_t)px] = col;
}

/* ---- fonts ---- */

struct GfxFont {
    int size;
    bool bold;
    int ascent, descent, line;
    const uint8_t *glyphs; /* 12 bytes each */
    const uint8_t *pairs;  /* 4 bytes each */
    int npairs;
};

static GfxFont faces[16];
static int nfaces, nglyphs, replacement;
static const uint8_t *points;

static int glyph_index(uint32_t cp)
{
    int lo = 0, hi = nglyphs - 1;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        uint32_t v = rd32(points + 4 * mid);
        if (v == cp)
            return mid;
        if (v < cp)
            lo = mid + 1;
        else
            hi = mid - 1;
    }
    return -1;
}

bool gfx_fonts_init(void)
{
    const uint8_t *d = font_inter_data;
    size_t len = (size_t)(font_inter_end - font_inter_data);
    if (nfaces)
        return true;
    if (len < 12 || memcmp(d, "PMF1", 4))
        return false;
    int nf = rd16(d + 4);
    nglyphs = rd16(d + 6);
    size_t po = rd32(d + 8);
    if (nf < 1 || nf > (int)ARRAY_SIZE(faces) || !nglyphs || 12 + 20 * (size_t)nf > len ||
        po + 4 * (size_t)nglyphs > len)
        return false;
    points = d + po;
    for (int i = 0; i < nf; i++) {
        const uint8_t *r = d + 12 + 20 * i;
        size_t go = rd32(r + 8), pairs = rd32(r + 12), np = rd32(r + 16);
        if (go + 12 * (size_t)nglyphs > len || pairs + 4 * np > len)
            return false;
        for (int g = 0; g < nglyphs; g++) {
            const uint8_t *e = d + go + 12 * g;
            if (rd32(e + 8) + (size_t)(e[4] + 1) / 2 * e[5] > len)
                return false;
        }
        faces[i] = (GfxFont){ r[0], r[1] != 0, r[2], r[3], r[4], d + go, d + pairs, (int)np };
    }
    replacement = glyph_index('?');
    if (replacement < 0)
        return false;
    nfaces = nf;
    return true;
}

const GfxFont *gfx_font(int px, bool bold)
{
    const GfxFont *best = NULL;
    for (int i = 0; i < nfaces; i++) {
        const GfxFont *f = &faces[i];
        if (f->bold != bold)
            continue;
        if (!best || (f->size <= px && (best->size > px || f->size > best->size)) ||
            (best->size > px && f->size < best->size))
            best = f;
    }
    return best;
}

int gfx_font_size(const GfxFont *f) { return f->size; }
int gfx_line_height(const GfxFont *f) { return f->line; }

/* Kerning of the pair A, B in 1/64 pixel; the pairs are sorted. */
static int kern(const GfxFont *f, uint32_t a, uint32_t b)
{
    if (a < 0x21 || a > 0x7E || b < 0x21 || b > 0x7E)
        return 0;
    unsigned key = a << 8 | b;
    int lo = 0, hi = f->npairs - 1;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        const uint8_t *p = f->pairs + 4 * mid;
        unsigned k = (unsigned)p[0] << 8 | p[1];
        if (k == key)
            return (int16_t)rd16(p + 2);
        if (k < key)
            lo = mid + 1;
        else
            hi = mid - 1;
    }
    return 0;
}

/* Lays out TEXT from X; draws when C is not NULL. Returns the end in 1/64 px. */
static long layout(GfxCanvas *c, int x, int y, const GfxFont *f, GfxColor col, const char *text)
{
    long pos = (long)x * 64;
    int base = y + (f->line - f->ascent - f->descent) / 2 + f->ascent;
    uint32_t prev = 0;
    for (size_t i = 0, n = strlen(text); i < n;) {
        uint32_t cp;
        i += (size_t)utf8_decode(text + i, n - i, &cp);
        int gi = glyph_index(cp);
        if (gi < 0)
            gi = replacement, cp = '?';
        pos += kern(f, prev, cp);
        prev = cp;
        const uint8_t *g = f->glyphs + 12 * gi;
        int gw = g[4], gh = g[5];
        if (c && gw && gh) {
            int gx = (int)((pos + 32) >> 6) + (int8_t)g[2], gy = base + (int8_t)g[3];
            int x0 = gx, y0 = gy, w = gw, h = gh;
            if (cut(c, &x0, &y0, &w, &h)) {
                const uint8_t *pix = font_inter_data + rd32(g + 8);
                int stride = (gw + 1) / 2;
                for (int j = y0; j < y0 + h; j++) {
                    const uint8_t *src = pix + (size_t)(j - gy) * (size_t)stride;
                    uint32_t *row = c->px + (size_t)j * (size_t)c->w;
                    for (int i2 = x0; i2 < x0 + w; i2++) {
                        int k = i2 - gx;
                        unsigned a = (src[k / 2] >> (k % 2 * 4)) & 15;
                        if (a)
                            row[i2] = mix(row[i2], col, a, 15);
                    }
                }
            }
        }
        pos += rd16(g);
    }
    return pos;
}

int gfx_text(GfxCanvas *c, int x, int y, const GfxFont *f, GfxColor col, const char *text)
{
    return (int)((layout(c, x, y, f, col, text) + 32) >> 6);
}

int gfx_text_width(const GfxFont *f, const char *text)
{
    return (int)((layout(NULL, 0, 0, f, 0, text) + 32) >> 6);
}
