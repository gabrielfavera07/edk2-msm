/** @file
 *  EbsProbeDxe.c  -  breadcrumb tracer for the Windows winload->kernel handoff
 *
 *  We can't run code inside the kernel, but the kernel calls UEFI RUNTIME services as
 *  it boots. So we hook every runtime service plus the boot milestones and paint a
 *  white bar at a DIFFERENT vertical position for each. The LOWEST bar on screen tells
 *  how far the boot got. Runtime-service bars are gated to fire only AFTER EBS, so they
 *  signal the KERNEL (not winload).
 *
 *    Row band   Milestone                         Meaning if its bar shows
 *    --------   --------------------------------  --------------------------------------
 *    0          ReadyToBoot (UEFI)                paint works + FB correct
 *    250        ExitBootServices (winload done)   winload reached EBS
 *    500        GetVariable (kernel, post-EBS)    *** kernel is ALIVE post-EBS ***
 *    750        GetTime (kernel)                  kernel further along
 *    1000       SetVirtualAddressMap (kernel)     kernel in the runtime/VM phase
 *    1250       ResetSystem (kernel)              kernel crashed -> firmware reset
 *
 *  Paints both the GOP framebuffer (queried fresh at ReadyToBoot) and 0x5C000000.
 **/

#include <Uefi.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiRuntimeServicesTableLib.h>
#include <Library/UefiLib.h>
#include <Library/DebugLib.h>
#include <Protocol/GraphicsOutput.h>

#define FB_FALLBACK   0x5C000000
#define BAR_ROWS      150

STATIC UINTN    mFbBase   = 0;
STATIC UINT32   mFbStride = 1200;
STATIC BOOLEAN  mAfterEbs = FALSE;

STATIC EFI_GET_VARIABLE            mOrigGetVariable = NULL;
STATIC EFI_GET_TIME                mOrigGetTime     = NULL;
STATIC EFI_SET_VIRTUAL_ADDRESS_MAP mOrigSVAM        = NULL;
STATIC EFI_RESET_SYSTEM            mOrigReset       = NULL;

STATIC EFI_EVENT  mRtbEvent = NULL;
STATIC EFI_EVENT  mEbsEvent = NULL;
STATIC EFI_EVENT  mVacEvent = NULL;

STATIC
VOID
PaintAt (
  IN UINTN   Addr,
  IN UINT32  Stride,
  IN UINTN   StartRow
  )
{
  volatile UINT32  *Fb;
  UINTN            n, i;

  if (Addr == 0) {
    return;
  }
  Fb = (volatile UINT32 *)(Addr + (UINTN)StartRow * Stride * 4);
  n  = (UINTN)BAR_ROWS * Stride;
  for (i = 0; i < n; i++) {
    Fb[i] = 0xFFFFFFFF;
  }
}

STATIC
VOID
Bar (
  IN UINTN  StartRow
  )
{
  if (mFbBase != 0) {
    PaintAt (mFbBase, mFbStride, StartRow);
  }
  if (mFbBase != FB_FALLBACK) {
    PaintAt (FB_FALLBACK, 1200, StartRow);
  }
}

//
// ---- hooked runtime services (paint once, only after EBS = kernel) ----
//
STATIC
EFI_STATUS
EFIAPI
HookGetVariable (
  IN CHAR16    *VariableName,
  IN EFI_GUID  *VendorGuid,
  OUT UINT32   *Attributes  OPTIONAL,
  IN OUT UINTN *DataSize,
  OUT VOID     *Data         OPTIONAL
  )
{
  STATIC BOOLEAN done = FALSE;
  if (mAfterEbs && !done) { done = TRUE; Bar (500); }
  return mOrigGetVariable (VariableName, VendorGuid, Attributes, DataSize, Data);
}

STATIC
EFI_STATUS
EFIAPI
HookGetTime (
  OUT EFI_TIME               *Time,
  OUT EFI_TIME_CAPABILITIES  *Capabilities OPTIONAL
  )
{
  STATIC BOOLEAN done = FALSE;
  if (mAfterEbs && !done) { done = TRUE; Bar (750); }
  return mOrigGetTime (Time, Capabilities);
}

STATIC
EFI_STATUS
EFIAPI
HookSVAM (
  IN UINTN                  MemoryMapSize,
  IN UINTN                  DescriptorSize,
  IN UINT32                 DescriptorVersion,
  IN EFI_MEMORY_DESCRIPTOR  *VirtualMap
  )
{
  Bar (1000);
  return mOrigSVAM (MemoryMapSize, DescriptorSize, DescriptorVersion, VirtualMap);
}

STATIC
VOID
EFIAPI
HookReset (
  IN EFI_RESET_TYPE  ResetType,
  IN EFI_STATUS      ResetStatus,
  IN UINTN           DataSize,
  IN VOID            *ResetData OPTIONAL
  )
{
  if (mAfterEbs) { Bar (1250); }
  mOrigReset (ResetType, ResetStatus, DataSize, ResetData);
}

STATIC
VOID
EFIAPI
OnExitBootServices (
  IN EFI_EVENT  Event,
  IN VOID       *Context
  )
{
  Bar (250);
  mAfterEbs = TRUE;
}

STATIC
VOID
EFIAPI
OnVirtualAddressChange (
  IN EFI_EVENT  Event,
  IN VOID       *Context
  )
{
  gRT->ConvertPointer (0, (VOID **)&mOrigGetVariable);
  gRT->ConvertPointer (0, (VOID **)&mOrigGetTime);
  gRT->ConvertPointer (0, (VOID **)&mOrigSVAM);
  gRT->ConvertPointer (0, (VOID **)&mOrigReset);
}

STATIC
VOID
EFIAPI
OnReadyToBoot (
  IN EFI_EVENT  Event,
  IN VOID       *Context
  )
{
  EFI_GRAPHICS_OUTPUT_PROTOCOL  *Gop;
  EFI_STATUS                    Status;
  UINT32                        Crc;

  gBS->CloseEvent (Event);

  Status = gBS->LocateProtocol (&gEfiGraphicsOutputProtocolGuid, NULL, (VOID **)&Gop);
  if (!EFI_ERROR (Status) && (Gop->Mode != NULL)) {
    mFbBase = (UINTN)Gop->Mode->FrameBufferBase;
    if ((Gop->Mode->Info != NULL) && (Gop->Mode->Info->PixelsPerScanLine != 0)) {
      mFbStride = Gop->Mode->Info->PixelsPerScanLine;
    }
  }

  Bar (0);   // ReadyToBoot bar -- tests FB + paint immediately

  // hook the runtime services
  mOrigGetVariable        = gRT->GetVariable;
  mOrigGetTime            = gRT->GetTime;
  mOrigSVAM               = gRT->SetVirtualAddressMap;
  mOrigReset              = gRT->ResetSystem;
  gRT->GetVariable        = HookGetVariable;
  gRT->GetTime            = HookGetTime;
  gRT->SetVirtualAddressMap = HookSVAM;
  gRT->ResetSystem        = HookReset;
  gRT->Hdr.CRC32          = 0;
  gBS->CalculateCrc32 (gRT, gRT->Hdr.HeaderSize, &Crc);
  gRT->Hdr.CRC32          = Crc;

  gBS->CreateEvent (EVT_SIGNAL_EXIT_BOOT_SERVICES, TPL_NOTIFY,
                    OnExitBootServices, NULL, &mEbsEvent);
  gBS->CreateEventEx (EVT_NOTIFY_SIGNAL, TPL_NOTIFY, OnVirtualAddressChange,
                      NULL, &gEfiEventVirtualAddressChangeGuid, &mVacEvent);

  DEBUG ((DEBUG_ERROR, "[ebsprobe] RTB FB=0x%lx stride=%d\n", (UINT64)mFbBase, mFbStride));
}

EFI_STATUS
EFIAPI
EbsProbeDxeEntry (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  return gBS->CreateEventEx (
                EVT_NOTIFY_SIGNAL,
                TPL_CALLBACK,
                OnReadyToBoot,
                NULL,
                &gEfiEventReadyToBootGuid,
                &mRtbEvent
                );
}
