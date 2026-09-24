/* Platform layer for UEFI: the graphics screen (Graphics Output Protocol)
 * and the serial port of the console, where the graphical interface writes
 * what it shows for the tests. */
#include "efi_glue.h"

static EFI_GUID gop_guid = EFI_GRAPHICS_OUTPUT_PROTOCOL_GUID;
static EFI_GUID serial_guid = EFI_SERIAL_IO_PROTOCOL_GUID;

static EFI_GRAPHICS_OUTPUT_PROTOCOL *gop;

bool pal_gfx_open(int *w, int *h)
{
    /* the screen the console is on; any screen otherwise */
    gop = NULL;
    if (!gST->ConsoleOutHandle ||
        gBS->HandleProtocol(gST->ConsoleOutHandle, &gop_guid, (void **)&gop) != EFI_SUCCESS)
        if (gBS->LocateProtocol(&gop_guid, NULL, (void **)&gop) != EFI_SUCCESS)
            gop = NULL;
    if (!gop || !gop->Mode || !gop->Mode->Info)
        return false;
    *w = (int)gop->Mode->Info->HorizontalResolution;
    *h = (int)gop->Mode->Info->VerticalResolution;
    if (*w < 640 || *h < 480) {
        gop = NULL;
        return false;
    }
    /* the text console must not draw over the picture */
    gST->ConOut->EnableCursor(gST->ConOut, 0);
    return true;
}

void pal_gfx_show(const uint32_t *px, int stride, int x, int y, int w, int h)
{
    if (!gop || w <= 0 || h <= 0)
        return;
    /* a 0x00RRGGBB number in memory is a Blt pixel: blue, green, red, 0 */
    gop->Blt(gop, (EFI_GRAPHICS_OUTPUT_BLT_PIXEL *)(uintptr_t)px, EfiBltBufferToVideo, (UINTN)x, (UINTN)y,
             (UINTN)x, (UINTN)y, (UINTN)w, (UINTN)h, (UINTN)stride * 4);
}

void pal_gfx_close(void)
{
    if (!gop)
        return;
    gop = NULL;
    /* the text console redraws the whole screen */
    gST->ConOut->ClearScreen(gST->ConOut);
}

/* ---- the serial port of the console ---- */

static EFI_SERIAL_IO_PROTOCOL *serial;
static bool serial_looked;

/* The serial port the firmware's console is sent to, found from the device
 * paths of the ConOut variable; NULL when the console has none. */
static EFI_SERIAL_IO_PROTOCOL *console_serial(void)
{
    if (serial_looked)
        return serial;
    serial_looked = true;
    UINTN size = 0;
    CHAR16 name[] = L"ConOut";
    if (gRT->GetVariable(name, &gEfiGlobalVariableGuid, NULL, &size, NULL) != EFI_BUFFER_TOO_SMALL || !size)
        return NULL;
    uint8_t *buf = xmalloc(size);
    if (gRT->GetVariable(name, &gEfiGlobalVariableGuid, NULL, &size, buf) != EFI_SUCCESS) {
        free(buf);
        return NULL;
    }
    /* each instance of the multi-instance path: does it go through a UART? */
    uint8_t *inst = buf, *end = buf + size;
    for (uint8_t *p = buf; p + 4 <= end;) {
        EFI_DEVICE_PATH_PROTOCOL *n = (EFI_DEVICE_PATH_PROTOCOL *)p;
        size_t len = (size_t)(n->Length[0] | n->Length[1] << 8);
        if (len < 4 || p + len > end)
            break;
        if (n->Type == MESSAGING_DEVICE_PATH && n->SubType == 0x0E /* UART */) {
            EFI_DEVICE_PATH_PROTOCOL *dp = (EFI_DEVICE_PATH_PROTOCOL *)inst;
            EFI_HANDLE h;
            if (gBS->LocateDevicePath(&serial_guid, &dp, &h) == EFI_SUCCESS &&
                gBS->HandleProtocol(h, &serial_guid, (void **)&serial) == EFI_SUCCESS)
                break;
            serial = NULL;
        }
        if (n->Type == END_DEVICE_PATH_TYPE) {
            if (n->SubType == END_ENTIRE_DEVICE_PATH_SUBTYPE)
                break;
            inst = p + len;
        }
        p += len;
    }
    free(buf);
    return serial;
}

void pal_serial_log(const char *line)
{
    EFI_SERIAL_IO_PROTOCOL *s = console_serial();
    if (!s)
        return;
    UINTN n = strlen(line);
    s->Write(s, &n, (void *)(uintptr_t)line);
    char crlf[] = "\r\n";
    n = 2;
    s->Write(s, &n, crlf);
}
