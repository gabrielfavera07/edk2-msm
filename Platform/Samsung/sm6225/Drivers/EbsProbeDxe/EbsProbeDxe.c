/** @file
 *  EbsProbeDxe.c  -  post-ExitBootServices observability for the Windows kernel handoff
 *
 *  Brackets the winload->kernel handoff with on-screen bars:
 *    - TOP bar at ExitBootServices (winload always calls it) -> paint works + EBS reached.
 *    - SECOND bar when SetVirtualAddressMap is called -> the runtime phase was reached.
 *
 *  Paints into BOTH the GOP-reported framebuffer (queried at ReadyToBoot, when the GOP
 *  actually exists) AND the known display region 0x5C000000, so a wrong guess can't hide
 *  the result. Runtime driver so the hook + globals survive EBS.
 **/

#include <Uefi.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiRuntimeServicesTableLib.h>
#include <Library/UefiLib.h>
#include <Library/DebugLib.h>
#include <Protocol/GraphicsOutput.h>

#define FB_FALLBACK   0x5C000000        // Display Reserved (cont_splash)

STATIC UINTN                        mFbBase   = 0;
STATIC UINT32                       mFbStride = 1200;
STATIC EFI_SET_VIRTUAL_ADDRESS_MAP  mOrigSVAM = NULL;
STATIC EFI_EVENT                    mRtbEvent = NULL;
STATIC EFI_EVENT                    mEbsEvent = NULL;
STATIC EFI_EVENT                    mVacEvent = NULL;

STATIC
VOID
PaintAt (
  IN UINTN   Addr,
  IN UINT32  Stride,
  IN UINTN   StartRow,
  IN UINTN   Rows
  )
{
  volatile UINT32  *Fb;
  UINTN            n, i;

  if (Addr == 0) {
    return;
  }
  Fb = (volatile UINT32 *)(Addr + (UINTN)StartRow * Stride * 4);
  n  = Rows * Stride;
  for (i = 0; i < n; i++) {
    Fb[i] = 0xFFFFFFFF;   // white
  }
}

STATIC
VOID
PaintBar (
  IN UINTN  StartRow,
  IN UINTN  Rows
  )
{
  if (mFbBase != 0) {
    PaintAt (mFbBase, mFbStride, StartRow, Rows);
  }
  if (mFbBase != FB_FALLBACK) {
    PaintAt (FB_FALLBACK, 1200, StartRow, Rows);   // reinforcement
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
  PaintBar (0, 120);            // TOP bar
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
  PaintBar (300, 120);         // SECOND bar (lower)
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

// Runs at ReadyToBoot -- the GOP exists by now, and we are still before EBS.
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

  // hook the first kernel runtime call
  mOrigSVAM                 = gRT->SetVirtualAddressMap;
  gRT->SetVirtualAddressMap = HookSetVirtualAddressMap;
  gRT->Hdr.CRC32            = 0;
  gBS->CalculateCrc32 (gRT, gRT->Hdr.HeaderSize, &Crc);
  gRT->Hdr.CRC32           = Crc;

  gBS->CreateEvent (EVT_SIGNAL_EXIT_BOOT_SERVICES, TPL_NOTIFY,
                    OnExitBootServices, NULL, &mEbsEvent);
  gBS->CreateEventEx (EVT_NOTIFY_SIGNAL, TPL_NOTIFY, OnVirtualAddressChange,
                      NULL, &gEfiEventVirtualAddressChangeGuid, &mVacEvent);

  DEBUG ((DEBUG_ERROR, "[ebsprobe] RTB: FB=0x%lx stride=%d (fallback 0x%x)\n",
          (UINT64)mFbBase, mFbStride, FB_FALLBACK));
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
