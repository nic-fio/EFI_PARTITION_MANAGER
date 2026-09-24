/* gfxdemo.efi: the test picture of gfxscene.c on the graphics screen, for
 * qemu-test.py, which compares QEMU's screen with gfxtool's picture. Says on
 * the serial port when the picture is shown; a key ends it. */
#include "../../src/partmgr/gfx.h"
#include "../../src/pal/pal.h"

const char app_name[] = "gfxdemo";

void gfx_scene(GfxCanvas *c);

int app_main(int argc, char **argv)
{
    int w, h;
    char line[80];
    if (!gfx_fonts_init()) {
        pal_serial_log("gfxdemo: the font is damaged");
        return 1;
    }
    if (!pal_gfx_open(&w, &h)) {
        pal_serial_log("gfxdemo: no graphics screen");
        return 1;
    }
    GfxCanvas c;
    if (!gfx_canvas_init(&c, w, h)) {
        pal_gfx_close();
        pal_serial_log("gfxdemo: out of memory");
        return 1;
    }
    gfx_scene(&c);
    pal_gfx_show(c.px, c.w, 0, 0, w, h);
    snprintf(line, sizeof(line), "gfxdemo: shown %dx%d", w, h);
    pal_serial_log(line);
    PalKey k;
    pal_con_read_key(&k, -1);
    gfx_canvas_free(&c);
    pal_gfx_close();
    pal_serial_log("gfxdemo: closed");
    return 0;
}
