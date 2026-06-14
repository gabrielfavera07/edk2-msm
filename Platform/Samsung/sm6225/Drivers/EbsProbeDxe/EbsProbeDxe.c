/** @file
 *  EbsProbeDxe.c  -  post-ExitBootServices observability for the Windows kernel handoff
 *
 *  winload's boot debugger connects over KDNET but won't hold, and the kernel dies
 *  before KdInitSystem (its own debugger), so we're blind at the handoff. There is no
 *  UEFI touchpoint inside the kernel's earliest code, but we CAN bracket the handoff:
 *
 *    - paint a WHITE bar at the TOP at ExitBootServices (winload always calls this) ->
 *      proves the framebuffer paint works AND winload reached EBS.
 *    - paint a WHITE bar LOWER when SetVirtualAddressMap is called (the kernel's first
 *      UEFI runtime call, after its early VM setup) -> the kernel got into the runtime
 *      phase.
 *
 *  Top bar only  -> winload reached EBS, paint works, kernel died before SVAM.
 *  Both bars     -> kernel reached the runtime phase.
 *  No bar at all -> framebuffer base/format wrong (paint itself failed).
 *
 *  Uses the real GOP framebuffer base (queried at init), not a hard-coded address.
 *  Runtime driver so the hook + globals survive EBS.
 **/

#include <Uefi.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiRuntimeServicesTableLib.h>
#include <Library/UefiLib.h>
#include <Library/DebugLib.h>
#include <Protocol/GraphicsOutput.h>

STATIC UINTN                        mFbBase   = 0;
STATIC UINT32                       mFbStride = 1200;   // pixels per scan line
STATIC EFI_SET_VIRTUAL_ADDRESS_MAP  mOrigSVAM = NULL;
STATIC EFI_EVENT                    mEbsEvent = NULL;
STATIC EFI_EVENT                    mVacEvent = NULL;

// paint `rows` scan lines of white starting at scan line `startRow`
STATIC
VOID
PaintBar (
  IN UINTN  StartRow,
  IN UINTN  Rows
  )
{
  volatile UINT32  *Fb;
  UINTN            n, i;

  if (mFbBase == 0) {
    return;
  }
  Fb = (volatile UINT32 *)mFbBase;
  n  = Rows * mFbStride;
  Fb = &Fb[StartRow * mFbStride];
  for (i = 0; i < n; i++) {
    Fb[i] = 0xFFFFFFFF;   // white (format-agnostic)
  }
}

STATIC
VOID
EFIAPI
OnExitBootServices (
  IN EFI_EVENT  Event,
  IN VOID       *Context
  )
{
  PaintBar (0, 120);            // TOP bar: winload reached EBS, paint works
}

STATIC
EFI_STATUS
EFIAPI
HookSetVirtualAddressMap (
  IN UINTN                  MemoryMapSize,
  IN UINTN                  DescriptorSize,
  IN UINT32                 DescriptorVersion,
  IN EFI_MEMORY_DESCRIPTOR  *VirtualMap
  )
{
  PaintBar (200, 120);         // SECOND bar: kernel reached the runtime phase
  return mOrigSVAM (MemoryMapSize, DescriptorSize, DescriptorVersion, VirtualMap);
}

STATIC
VOID
EFIAPI
OnVirtualAddressChange (
  IN EFI_EVENT  Event,
  IN VOID       *Context
  )
{
  gRT->ConvertPointer (0, (VOID **)&mOrigSVAM);
}

EFI_STATUS
EFIAPI
EbsProbeDxeEntry (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  EFI_GRAPHICS_OUTPUT_PROTOCOL  *Gop;
  EFI_STATUS                    Status;
  UINT32                        Crc;

  // real framebuffer base from the GOP (this is where the Renegade logo is drawn)
  Status = gBS->LocateProtocol (&gEfiGraphicsOutputProtocolGuid, NULL, (VOID **)&Gop);
  if (!EFI_ERROR (Status) && (Gop->Mode != NULL)) {
    mFbBase = (UINTN)Gop->Mode->FrameBufferBase;
    if (Gop->Mode->Info != NULL) {
      mFbStride = Gop->Mode->Info->PixelsPerScanLine;
    }
  }

  // hook the kernel's first runtime call
  mOrigSVAM                 = gRT->SetVirtualAddressMap;
  gRT->SetVirtualAddressMap = HookSetVirtualAddressMap;
  gRT->Hdr.CRC32            = 0;
  gBS->CalculateCrc32 (gRT, gRT->Hdr.HeaderSize, &Crc);
  gRT->Hdr.CRC32           = Crc;

  // EBS bracket
  gBS->CreateEvent (EVT_SIGNAL_EXIT_BOOT_SERVICES, TPL_NOTIFY,
                    OnExitBootServices, NULL, &mEbsEvent);
  // keep the saved pointer valid after the address switch
  gBS->CreateEventEx (EVT_NOTIFY_SIGNAL, TPL_NOTIFY, OnVirtualAddressChange,
                      NULL, &gEfiEventVirtualAddressChangeGuid, &mVacEvent);

  DEBUG ((DEBUG_INFO, "[ebsprobe] FB=0x%lx stride=%d\n", (UINT64)mFbBase, mFbStride));
  return EFI_SUCCESS;
}
