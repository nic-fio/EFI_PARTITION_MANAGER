/* The drawing code on Linux: draws the test picture of gfxscene.c into a PPM
 * file, and checks the primitives and the font on their own.
 *
 *   gfxtool scene W H FILE.ppm   the test picture, W x H pixels
 *   gfxtool check                the checks; the exit status says if they passed */
#include "../../src/partmgr/gfx.h"

void gfx_scene(GfxCanvas *c);

void rt_fatal(const char *msg)
{
    fprintf(stderr, "gfxtool: %s\n", msg);
    exit(2);
}

static int count, failed;

static void check(const char *name, bool ok)
{
    count++;
    if (!ok) {
        failed++;
        printf("FAIL %s\n", name);
    }
}

static int write_ppm(const GfxCanvas *c, const char *path)
{
    FILE *f = fopen(path, "wb");
    if (!f)
        return 1;
    fprintf(f, "P6\n%d %d\n255\n", c->w, c->h);
    for (int i = 0; i < c->w * c->h; i++) {
        uint32_t p = c->px[i];
        fputc(p >> 16 & 255, f), fputc(p >> 8 & 255, f), fputc(p & 255, f);
    }
    return fclose(f) ? 1 : 0;
}

static uint32_t at(const GfxCanvas *c, int x, int y) { return c->px[y * c->w + x]; }

/* pixels of the canvas that are not COL, inside or outside a rectangle */
static int others(const GfxCanvas *c, int x, int y, int w, int h, uint32_t col, bool inside)
{
    int n = 0;
    for (int j = 0; j < c->h; j++)
        for (int i = 0; i < c->w; i++) {
            bool in = i >= x && i < x + w && j >= y && j < y + h;
            n += in == inside && at(c, i, j) != col;
        }
    return n;
}

static void checks(void)
{
    check("fonts load", gfx_fonts_init());
    check("20 px regular", gfx_font_size(gfx_font(20, false)) == 20);
    check("23 px gives 20", gfx_font_size(gfx_font(23, true)) == 20);
    check("10 px gives the smallest", gfx_font_size(gfx_font(10, false)) == 16);
    check("100 px gives the largest", gfx_font_size(gfx_font(100, false)) == 32);
    const GfxFont *f = gfx_font(20, false);
    check("kerning: AV narrower than A + V",
          gfx_text_width(f, "AV") < gfx_text_width(f, "A") + gfx_text_width(f, "V"));
    check("a missing character is ?", gfx_text_width(f, "中") == gfx_text_width(f, "?"));
    check("empty text", gfx_text_width(f, "") == 0);
    check("bold is wider", gfx_text_width(gfx_font(20, true), "Partition") > gfx_text_width(f, "Partition"));

    GfxCanvas c;
    gfx_canvas_init(&c, 200, 100);
    gfx_fill(&c, -50, -50, 400, 400, 0xFFFFFF);
    check("fill cut to the canvas", others(&c, 0, 0, 200, 100, 0xFFFFFF, true) == 0);
    int end = gfx_text(&c, 10, 10, f, 0x000000, "Hello");
    check("text returns its end", end == 10 + gfx_text_width(f, "Hello"));
    int dark = 0, grey = 0;
    for (int i = 0; i < 200 * 100; i++)
        dark += c.px[i] == 0, grey += c.px[i] != 0 && c.px[i] != 0xFFFFFF;
    check("text: opaque pixels", dark > 20);
    check("text: smoothed edges", grey > 20);
    check("text stays in its line",
          others(&c, 10, 10, end - 10 + 2, gfx_line_height(f), 0xFFFFFF, false) == 0);

    gfx_fill(&c, 0, 0, 200, 100, 0xFFFFFF);
    gfx_clip(&c, 20, 20, 30, 15);
    gfx_text(&c, 0, 15, gfx_font(32, true), 0x000000, "WWWWWW");
    gfx_fill(&c, 0, 0, 200, 100, 0x000000); /* the whole canvas, cut to the clip */
    gfx_unclip(&c);
    check("clip keeps the outside", others(&c, 20, 20, 30, 15, 0xFFFFFF, false) == 0);

    gfx_fill(&c, 0, 0, 200, 100, 0xFFFFFF);
    gfx_text(&c, 5, 5, f, 0xFFFFFF, "Invisible");
    check("text of the background colour changes nothing", others(&c, 0, 0, 0, 0, 0xFFFFFF, false) == 0);

    gfx_fill(&c, 0, 0, 200, 100, 0xFFFFFF);
    gfx_round_rect(&c, 20, 10, 100, 60, 12, 0x0000FF, 2, 0xFF0000);
    bool sym = true;
    for (int j = 10; j < 70; j++)
        for (int i = 20; i < 120; i++)
            sym &= at(&c, i, j) == at(&c, 139 - i, j) && at(&c, i, j) == at(&c, i, 79 - j);
    check("round rectangle symmetric", sym);
    check("round rectangle: corner outside untouched", at(&c, 20, 10) == 0xFFFFFF);
    check("round rectangle: border", at(&c, 70, 10) == 0xFF0000 && at(&c, 70, 11) == 0xFF0000);
    check("round rectangle: fill", at(&c, 70, 40) == 0x0000FF);
    check("round rectangle: nothing outside", others(&c, 20, 10, 100, 60, 0xFFFFFF, false) == 0);
    int part = 0;
    for (int j = 10; j < 22; j++)
        for (int i = 20; i < 32; i++)
            part += at(&c, i, j) != 0xFFFFFF && at(&c, i, j) != 0xFF0000 && at(&c, i, j) != 0x0000FF;
    check("round rectangle: smoothed corner", part > 3);

    gfx_fill(&c, 0, 0, 200, 100, 0xFFFFFF);
    gfx_hatch(&c, 0, 0, 200, 100, 10, 0x000000);
    check("hatch: lines where x + y is a multiple of the step",
          at(&c, 10, 0) == 0 && at(&c, 5, 5) == 0 && at(&c, 6, 5) == 0xFFFFFF);
    gfx_fill(&c, 0, 0, 200, 100, 0xFFFFFF);
    gfx_frame(&c, 10, 10, 50, 30, 3, 0x000000);
    check("frame", at(&c, 10, 10) == 0 && at(&c, 12, 25) == 0 && at(&c, 13, 25) == 0xFFFFFF &&
                       at(&c, 59, 39) == 0 && at(&c, 30, 20) == 0xFFFFFF);
    gfx_canvas_free(&c);

    /* the test picture at the sizes the QEMU test uses, and at the smallest */
    static const int sizes[][2] = { { 1280, 800 }, { 1024, 768 }, { 640, 480 } };
    for (int i = 0; i < 3; i++) {
        gfx_canvas_init(&c, sizes[i][0], sizes[i][1]);
        gfx_scene(&c);
        check("scene draws", others(&c, 0, 0, 0, 0, 0xF3F4F6, false) > 0);
        gfx_canvas_free(&c);
    }
}

int main(int argc, char **argv)
{
    if (argc == 5 && !strcmp(argv[1], "scene")) {
        GfxCanvas c;
        if (!gfx_fonts_init() || !gfx_canvas_init(&c, atoi(argv[2]), atoi(argv[3])))
            return 2;
        gfx_scene(&c);
        int r = write_ppm(&c, argv[4]);
        gfx_canvas_free(&c);
        return r;
    }
    if (argc == 2 && !strcmp(argv[1], "check")) {
        checks();
        printf("gfx tests: %d checks, %d failed\n", count, failed);
        return failed ? 1 : 0;
    }
    fprintf(stderr, "usage: gfxtool scene W H FILE.ppm | gfxtool check\n");
    return 2;
}
