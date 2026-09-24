/* Platform layer for UEFI: the pointer (decision P21). Two kinds of devices
 * are read together:
 *
 * - the firmware's pointers: every Simple Pointer or Absolute Pointer of a
 *   real device (one with a device path; the console's own is only the sum
 *   of the others, and exists even with no mouse at all);
 * - partmgr's own USB mice: a USB interface of the HID boot mouse kind that
 *   no firmware driver has taken. partmgr opens it as a driver would (so the
 *   firmware cannot take it meanwhile), switches it to the boot protocol -
 *   three bytes: buttons, x, y - and lets the host controller poll it,
 *   collecting the reports in a callback. OVMF, the firmware of the tests,
 *   has no mouse driver at all, so the tests exercise this code. */
#include "efi_glue.h"

static EFI_GUID simple_guid = EFI_SIMPLE_POINTER_PROTOCOL_GUID;
static EFI_GUID absolute_guid = EFI_ABSOLUTE_POINTER_PROTOCOL_GUID;
static EFI_GUID usbio_guid = EFI_USB_IO_PROTOCOL_GUID;
static EFI_GUID usb2hc_guid = EFI_USB2_HC_PROTOCOL_GUID;

#define MAX_DEV 8

static EFI_SIMPLE_POINTER_PROTOCOL *simple[MAX_DEV];
static EFI_ABSOLUTE_POINTER_PROTOCOL *absolute[MAX_DEV];
static int nsimple, nabsolute;

typedef struct {
    EFI_HANDLE handle;
    EFI_USB_IO_PROTOCOL *io;
    UINT8 endpoint;
} OwnMouse;
static OwnMouse own[MAX_DEV];
static int nown;

/* what the callbacks collected since the last read (at TPL_NOTIFY) */
static volatile int acc_dx, acc_dy;
static volatile unsigned acc_buttons;
static volatile bool acc_changed;

static char info[80];

static EFI_STATUS EFIAPI report(void *data, UINTN len, void *ctx, UINT32 status)
{
    const UINT8 *r = data;
    if (status || !r || len < 3)
        return EFI_SUCCESS; /* an error or a short report: the next poll goes on */
    acc_dx += (INT8)r[1];
    acc_dy += (INT8)r[2];
    acc_buttons = r[0] & 3;
    acc_changed = true;
    return EFI_SUCCESS;
}

static bool has_devpath(EFI_HANDLE h)
{
    void *dp;
    return gBS->HandleProtocol(h, &gEfiDevicePathGuid, &dp) == EFI_SUCCESS;
}

static void find_firmware_pointers(void)
{
    EFI_HANDLE *h;
    UINTN n = 0;
    if (gBS->LocateHandleBuffer(ByProtocol, &simple_guid, NULL, &n, &h) == EFI_SUCCESS) {
        for (UINTN i = 0; i < n && nsimple < MAX_DEV; i++)
            if (has_devpath(h[i]) &&
                gBS->HandleProtocol(h[i], &simple_guid, (void **)&simple[nsimple]) == EFI_SUCCESS)
                simple[nsimple]->Reset(simple[nsimple], 0), nsimple++;
        gBS->FreePool(h);
    }
    n = 0;
    if (gBS->LocateHandleBuffer(ByProtocol, &absolute_guid, NULL, &n, &h) == EFI_SUCCESS) {
        for (UINTN i = 0; i < n && nabsolute < MAX_DEV; i++)
            if (has_devpath(h[i]) &&
                gBS->HandleProtocol(h[i], &absolute_guid, (void **)&absolute[nabsolute]) == EFI_SUCCESS)
                absolute[nabsolute]->Reset(absolute[nabsolute], 0), nabsolute++;
        gBS->FreePool(h);
    }
}

/* A USB interface: take it when it is a boot mouse no driver has. */
static void try_usb(EFI_HANDLE h)
{
    EFI_USB_IO_PROTOCOL *io;
    EFI_USB_INTERFACE_DESCRIPTOR d;
    if (gBS->HandleProtocol(h, &usbio_guid, (void **)&io) != EFI_SUCCESS ||
        io->UsbGetInterfaceDescriptor(io, &d) != EFI_SUCCESS)
        return;
    if (d.InterfaceClass != 3 || d.InterfaceSubClass != 1 || d.InterfaceProtocol != 2)
        return; /* not a HID boot mouse */
    UINT8 ep = 0;
    UINTN interval = 8, maxpkt = 8;
    for (UINT8 i = 0; i < d.NumEndpoints; i++) {
        EFI_USB_ENDPOINT_DESCRIPTOR e;
        if (io->UsbGetEndpointDescriptor(io, i, &e) == EFI_SUCCESS && (e.Attributes & 3) == 3 &&
            (e.EndpointAddress & 0x80)) {
            ep = e.EndpointAddress;
            interval = e.Interval ? e.Interval : 8;
            maxpkt = e.MaxPacketSize ? e.MaxPacketSize : 8;
            break;
        }
    }
    if (!ep)
        return;
    /* as a driver: refused when a firmware driver already has the mouse */
    if (gBS->OpenProtocol(h, &usbio_guid, (void **)&io, gImage, h, EFI_OPEN_PROTOCOL_BY_DRIVER) != EFI_SUCCESS)
        return;
    UINT32 st;
    EFI_USB_DEVICE_REQUEST boot = { 0x21, 0x0B, 0, d.InterfaceNumber, 0 }; /* SET_PROTOCOL: boot */
    EFI_USB_DEVICE_REQUEST idle = { 0x21, 0x0A, 0, d.InterfaceNumber, 0 }; /* SET_IDLE: report changes only */
    io->UsbControlTransfer(io, &boot, EfiUsbNoData, 100, NULL, 0, &st);
    io->UsbControlTransfer(io, &idle, EfiUsbNoData, 100, NULL, 0, &st); /* optional: may stall */
    if (io->UsbAsyncInterruptTransfer(io, ep, 1, interval, maxpkt, report, NULL) != EFI_SUCCESS) {
        gBS->CloseProtocol(h, &usbio_guid, gImage, h);
        return;
    }
    own[nown++] = (OwnMouse){ h, io, ep };
}

static void find_usb_mice(void)
{
    EFI_HANDLE *h;
    UINTN n = 0;
    if (gBS->LocateHandleBuffer(ByProtocol, &usbio_guid, NULL, &n, &h) != EFI_SUCCESS || !n) {
        /* a firmware that starts fast may not have looked at the USB devices:
         * let it connect its USB host controllers, then look again */
        EFI_HANDLE *hc;
        UINTN nhc = 0;
        if (gBS->LocateHandleBuffer(ByProtocol, &usb2hc_guid, NULL, &nhc, &hc) == EFI_SUCCESS) {
            for (UINTN i = 0; i < nhc; i++)
                gBS->ConnectController(hc[i], NULL, NULL, 1);
            gBS->FreePool(hc);
        }
        n = 0;
        if (gBS->LocateHandleBuffer(ByProtocol, &usbio_guid, NULL, &n, &h) != EFI_SUCCESS)
            return;
    }
    for (UINTN i = 0; i < n && nown < MAX_DEV; i++)
        try_usb(h[i]);
    gBS->FreePool(h);
}

int pal_pointer_open(void)
{
    nsimple = nabsolute = nown = 0;
    acc_dx = acc_dy = 0, acc_buttons = 0, acc_changed = false;
    find_firmware_pointers();
    find_usb_mice();
    snprintf(info, sizeof(info), "firmware %d, partmgr's USB driver %d", nsimple + nabsolute, nown);
    return nsimple + nabsolute + nown;
}

const char *pal_pointer_info(void)
{
    return info;
}

bool pal_pointer_read(PalPointer *p)
{
    bool got = false;
    *p = (PalPointer){ 0 };
    for (int i = 0; i < nsimple; i++) {
        EFI_SIMPLE_POINTER_STATE s;
        if (simple[i]->GetState(simple[i], &s) != EFI_SUCCESS)
            continue;
        /* counts to pixels: about one pixel per count at 8 counts per millimetre */
        UINT64 rx = simple[i]->Mode->ResolutionX, ry = simple[i]->Mode->ResolutionY;
        p->dx += rx > 8 ? (int)((INT64)s.RelativeMovementX * 8 / (INT64)rx) : s.RelativeMovementX;
        p->dy += ry > 8 ? (int)((INT64)s.RelativeMovementY * 8 / (INT64)ry) : s.RelativeMovementY;
        p->left |= s.LeftButton, p->right |= s.RightButton;
        got = true;
    }
    for (int i = 0; i < nabsolute; i++) {
        EFI_ABSOLUTE_POINTER_STATE s;
        EFI_ABSOLUTE_POINTER_MODE *m = absolute[i]->Mode;
        if (absolute[i]->GetState(absolute[i], &s) != EFI_SUCCESS || m->AbsoluteMaxX <= m->AbsoluteMinX ||
            m->AbsoluteMaxY <= m->AbsoluteMinY)
            continue;
        p->abs = true;
        p->ax = (int)((s.CurrentX - m->AbsoluteMinX) * 65535 / (m->AbsoluteMaxX - m->AbsoluteMinX));
        p->ay = (int)((s.CurrentY - m->AbsoluteMinY) * 65535 / (m->AbsoluteMaxY - m->AbsoluteMinY));
        p->left |= s.ActiveButtons & 1, p->right |= (s.ActiveButtons >> 1) & 1;
        got = true;
    }
    if (nown) {
        EFI_TPL old = gBS->RaiseTPL(TPL_NOTIFY);
        if (acc_changed) {
            p->dx += acc_dx, p->dy += acc_dy;
            p->left |= acc_buttons & 1, p->right |= acc_buttons >> 1 & 1;
            acc_dx = acc_dy = 0, acc_changed = false;
            got = true;
        } else {
            /* the buttons stay down between reports */
            p->left |= acc_buttons & 1, p->right |= acc_buttons >> 1 & 1;
        }
        gBS->RestoreTPL(old);
    }
    return got;
}

void pal_pointer_close(void)
{
    for (int i = 0; i < nown; i++) {
        own[i].io->UsbAsyncInterruptTransfer(own[i].io, own[i].endpoint, 0, 0, 0, NULL, NULL);
        gBS->CloseProtocol(own[i].handle, &usbio_guid, gImage, own[i].handle);
    }
    nsimple = nabsolute = nown = 0;
}
