/* gfxdemo.efi: the test picture of gfxscene.c on the graphics screen, for
 * qemu-test.py, which compares QEMU's screen with gfxtool's picture. Then
 * the mouse pointer, when there is a pointing device, follows it, drawn over the picture
 * without changing it. What happens is written to the serial port: the
 * picture shown, the devices found, each click with the pointer's position.
 * A key ends it. */
#include "../../src/partmgr/gfx.h"
#include "../../src/pal/pal.h"

const char app_name[] = "gfxdemo";

void gfx_scene(GfxCanvas *c);

static GfxCanvas screen, patch;

/* The pointer at X, Y: the picture under it with the arrow on top. */
static void show_pointer(int x, int y)
{
    int w = MIN(patch.w, screen.w - x), h = MIN(patch.h, screen.h - y);
    for (int j = 0; j < h; j++)
        memcpy(patch.px + j * patch.w, screen.px + (y + j) * screen.w + x, (size_t)w * 4);
    gfx_arrow(&patch, 0, 0, 1);
    pal_gfx_show(patch.px, patch.w, 0, 0, x, y, w, h);
}

/* The picture again where the pointer was. */
static void hide_pointer(int x, int y)
{
    pal_gfx_show(screen.px, screen.w, x, y, x, y, MIN(patch.w, screen.w - x), MIN(patch.h, screen.h - y));
}

int app_main(int argc, char **argv)
{
    int w, h;
    char line[96];
    if (!gfx_fonts_init()) {
        pal_serial_log("gfxdemo: the font is damaged");
        return 1;
    }
    if (!pal_gfx_open(&w, &h)) {
        pal_serial_log("gfxdemo: no graphics screen");
        return 1;
    }
    if (!gfx_canvas_init(&screen, w, h) || !gfx_canvas_init(&patch, GFX_ARROW_W, GFX_ARROW_H)) {
        pal_gfx_close();
        pal_serial_log("gfxdemo: out of memory");
        return 1;
    }
    gfx_scene(&screen);
    pal_gfx_show(screen.px, screen.w, 0, 0, 0, 0, w, h);
    snprintf(line, sizeof(line), "gfxdemo: shown %dx%d", w, h);
    pal_serial_log(line);

    /* the pointer appears only when there is something to move it */
    bool pointer = pal_pointer_open() > 0;
    snprintf(line, sizeof(line), "gfxdemo: pointer: %s", pal_pointer_info());
    pal_serial_log(line);
    int x = w / 2, y = h / 2;
    bool down = false;
    if (pointer)
        show_pointer(x, y);
    for (;;) {
        PalKey k;
        if (pal_con_read_key(&k, 10))
            break;
        PalPointer p;
        if (!pal_pointer_read(&p))
            continue;
        int nx = p.abs ? (int)((long)p.ax * (w - 1) / 65535) : x + p.dx;
        int ny = p.abs ? (int)((long)p.ay * (h - 1) / 65535) : y + p.dy;
        nx = MAX(0, MIN(nx, w - 1)), ny = MAX(0, MIN(ny, h - 1));
        if (nx != x || ny != y) {
            hide_pointer(x, y);
            x = nx, y = ny;
            show_pointer(x, y);
        }
        if (p.left != down) {
            down = p.left;
            snprintf(line, sizeof(line), "gfxdemo: %s at %d,%d", down ? "click" : "release", x, y);
            pal_serial_log(line);
        }
    }
    pal_pointer_close();
    gfx_canvas_free(&patch);
    gfx_canvas_free(&screen);
    pal_gfx_close();
    pal_serial_log("gfxdemo: closed");
    return 0;
}
