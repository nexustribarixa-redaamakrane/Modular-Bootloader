/*
 * efi_entry.c - UEFI application entry point.
 *
 * Called by UEFI firmware as EfiMain(ImageHandle, SystemTable).
 * Locates the GOP, Block I/O, and Simple Text Input protocols,
 * then calls kmain() which runs the OWFS boot menu.
 */

#include "efi.h"
#include "mbl.h"

/* ============================================================================
 * Global UEFI table pointers
 * ============================================================================ */
EFI_SYSTEM_TABLE       *gST;
EFI_BOOT_SERVICES      *gBS;
EFI_RUNTIME_SERVICES   *gRT;
EFI_HANDLE             gImageHandle;

/* Protocol globals */
EFI_GRAPHICS_OUTPUT_PROTOCOL     *gGOP;
EFI_BLOCK_IO_PROTOCOL            *gBlockIO;
EFI_SIMPLE_TEXT_INPUT_PROTOCOL   *gConIn;
EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL  *gConOut;
EFI_EXIT_BOOT_SERVICES           gExitBootServices;

/* ============================================================================
 * Protocol GUIDs
 * ============================================================================ */
static EFI_GUID gEfiGraphicsOutputProtocolGuid =
    { 0x9042a9de, 0x23dc, 0x4a38, { 0x96, 0xfb, 0x72, 0xde, 0xd3, 0x43, 0xef, 0x3a }};

static EFI_GUID gEfiBlockIoProtocolGuid =
    { 0x964e5b21, 0x6459, 0x11d2, { 0x8e, 0x39, 0x00, 0xa0, 0xc9, 0x69, 0x72, 0x3b }};

static EFI_GUID gEfiSimpleTextInputProtocolGuid =
    { 0x387477c1, 0x69c7, 0x11d2, { 0x8e, 0x39, 0x00, 0xa0, 0xc9, 0x69, 0x72, 0x3b }};

static EFI_GUID gEfiSimpleTextOutProtocolGuid =
    { 0x387477c2, 0x69c7, 0x11d2, { 0x8e, 0x39, 0x00, 0xa0, 0xc9, 0x69, 0x72, 0x3b }};

static EFI_GUID gEfiLoadedImageProtocolGuid =
    { 0x5b1b31a1, 0x9562, 0x11d2, { 0x8e, 0x3f, 0x00, 0xa0, 0xc9, 0x69, 0x72, 0x3b }};

/* ============================================================================
 * Helper: select the best GOP mode (highest resolution)
 * ============================================================================ */
static void init_gop(void)
{
    EFI_STATUS status;
    UINTN num_handles = 0;
    EFI_HANDLE *handles = NULL;
    UINTN i;

    status = gBS->LocateHandleBuffer(0 /* AllHandles */,
                                     &gEfiGraphicsOutputProtocolGuid,
                                     NULL, &num_handles, &handles);
    if (EFI_ERROR(status) || num_handles == 0) {
        return;
    }

    for (i = 0; i < num_handles; i++) {
        EFI_GRAPHICS_OUTPUT_PROTOCOL *gop = NULL;
        status = gBS->HandleProtocol(handles[i], &gEfiGraphicsOutputProtocolGuid, (void **)&gop);
        if (EFI_ERROR(status) || gop == NULL) {
            continue;
        }

        /* Pick the first working GOP */
        if (gGOP == NULL) {
            gGOP = gop;
        }
    }

    if (handles) {
        gBS->FreePool(handles);
    }

    if (gGOP == NULL) {
        return;
    }

    /* Select the mode with the highest resolution */
    {
        UINT32 best_mode = 0;
        UINT32 best_pixels = 0;

        for (i = 0; i < (UINTN)gGOP->Mode->MaxMode; i++) {
            EFI_GRAPHICS_OUTPUT_MODE_INFORMATION *info = NULL;
            UINTN info_size = 0;

            status = gGOP->QueryMode(gGOP, (UINT32)i, &info_size, &info);
            if (EFI_ERROR(status)) {
                continue;
            }
            if (info == NULL) {
                continue;
            }

            {
                UINT32 pixels = info->HorizontalResolution * info->VerticalResolution;
                if (pixels > best_pixels) {
                    best_pixels = pixels;
                    best_mode = (UINT32)i;
                }
            }
        }

        if (best_mode != gGOP->Mode->Mode) {
            gGOP->SetMode(gGOP, best_mode);
        }
    }
}

/* ============================================================================
 * Helper: find the first Block I/O device (non-removable, media present)
 * ============================================================================ */
static void init_block_io(void)
{
    EFI_STATUS status;
    UINTN num_handles = 0;
    EFI_HANDLE *handles = NULL;
    UINTN i;

    status = gBS->LocateHandleBuffer(0 /* AllHandles */,
                                     &gEfiBlockIoProtocolGuid,
                                     NULL, &num_handles, &handles);
    if (EFI_ERROR(status) || num_handles == 0) {
        return;
    }

    for (i = 0; i < num_handles; i++) {
        EFI_BLOCK_IO_PROTOCOL *bio = NULL;
        status = gBS->HandleProtocol(handles[i], &gEfiBlockIoProtocolGuid, (void **)&bio);
        if (EFI_ERROR(status) || bio == NULL) {
            continue;
        }
        if (bio->Media == NULL) {
            continue;
        }
        if (bio->Media->RemovableMedia) {
            continue;
        }
        if (!bio->Media->MediaPresent) {
            continue;
        }
        /* Found a fixed, present block device */
        gBlockIO = bio;
        break;
    }

    if (handles) {
        gBS->FreePool(handles);
    }
}

/* ============================================================================
 * EfiMain - UEFI application entry point
 * ============================================================================ */
EFI_STATUS EFIAPI EfiMain(EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE *SystemTable)
{
    /* Store UEFI table pointers */
    gImageHandle = ImageHandle;
    gST   = SystemTable;
    gBS   = SystemTable->BootServices;
    gRT   = SystemTable->RuntimeServices;
    gConOut = SystemTable->ConOut;
    gConIn  = SystemTable->ConIn;

    /* Clear the screen */
    if (gConOut) {
        gConOut->SetAttribute(gConOut, EFI_TEXT_ATTR(EFI_WHITE, EFI_BLACK));
        gConOut->ClearScreen(gConOut);
    }

    /* Locate GOP (for framebuffer rendering) */
    gGOP = NULL;
    init_gop();

    /* Locate Block I/O (for OWFS disk reads) */
    gBlockIO = NULL;
    init_block_io();

    /* Locate Loaded Image Protocol (for ExitBootServices) */
    gExitBootServices = NULL;
    {
        EFI_LOADED_IMAGE_PROTOCOL *loaded_image = NULL;
        EFI_STATUS status;
        status = gBS->HandleProtocol(ImageHandle, &gEfiLoadedImageProtocolGuid,
                                     (void **)&loaded_image);
        if (!EFI_ERROR(status) && loaded_image != NULL) {
            /* The Loaded Image Protocol has an Exit field which is
               ExitBootServices. Store it for use by kmain(). */
            typedef EFI_STATUS (EFIAPI *EXIT_FN)(EFI_HANDLE, UINTN);
            /* Exit is at offset after Unload in the struct:
               Revision(4) + ParentHandle(8) + SystemTable(8) + DeviceHandle(8) +
               FilePath(8) + LoadOptionsSize(4) + LoadOptions(8) + ImageBase(8) +
               ImageSize(8) + ImageCodeType(4) + ImageDataType(4) + Unload(8) = 80
               Then Exit at offset 80. */
            void **img = (void **)loaded_image;
            gExitBootServices = (EFI_EXIT_BOOT_SERVICES)img[10]; /* Exit field */
        }
    }

    /* Run the bootloader */
    kmain();

    /* Should never return */
    for (;;) {
        __asm__ volatile ("hlt");
    }

    return EFI_SUCCESS;
}
