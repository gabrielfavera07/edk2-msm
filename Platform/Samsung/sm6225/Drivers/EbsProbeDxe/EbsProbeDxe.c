/** @file
 *  EbsProbeDxe.c  -  breadcrumb tracer for the Windows winload->kernel handoff
 *
 *  Rotation/stride-proof: instead of bars, each milestone fills a LARGER FRACTION of the
 *  framebuffer from its base. "How much of the screen is white" tells how far the boot
 *  got. Runtime-service fills are gated to AFTER EBS so they signal the KERNEL.
 *
 *    Fraction white   Milestone (furthest reached)
 *    --------------   -----------------------------------------------
 *    ~1/8             ReadyToBoot   (UEFI; paint works + FB correct)
 *    ~1/4             ExitBootServices (winload finished)
 *    ~1/2  (HALF)     GetVariable   *** KERNEL ALIVE post-EBS ***
 *    ~5/8             GetTime       (kernel further)
 *    ~3/4             SetVirtualAddressMap (kernel runtime phase)
 *    full             ResetSystem   (kernel crashed -> firmware reset)
 *
 *  The FB base is re-read fresh at EBS from the GOP (winload swaps it after ReadyToBoot).
 **/

#include <Uefi.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiRuntimeServicesTableLib.h>
#include <Library/UefiLib.h>
#include <Library/DebugLib.h>
#include <Protocol/GraphicsOutput.h>

STATIC EFI_GRAPHICS_OUTPUT_PROTOCOL  *mGop      = NULL;
STATIC UINTN                         mFbBase    = 0;
STATIC UINTN                         mFbPixels  = 1200 * 2000;   // total pixels
STATIC BOOLEAN                       mAfterEbs  = FALSE;

STATIC EFI_GET_VARIABLE             mOrigGetVariable = NULL;
STATIC EFI_GET_TIME                 mOrigGetTime     = NULL;
STATIC EFI_SET_VIRTUAL_ADDRESS_MAP  mOrigSVAM        = NULL;
STATIC EFI_RESET_SYSTEM             mOrigReset       = NULL;

STATIC EFI_EVENT  mRtbEvent = NULL;
STATIC EFI_EVENT  mEbsEvent = NULL;
STATIC EFI_EVENT  mVacEvent = NULL;

// fill Num/Den of the framebuffer (from base) with white
STATIC
VOID
FillFrac (
  IN UINTN  Num,
  IN UINTN  Den
  )
{
  volatile UINT32  *Fb;
  UINTN            count, i;

  if (mFbBase == 0) {
    return;
  }
  Fb    = (volatile UINT32 *)mFbBase;
  count = mFbPixels * Num / Den;
  for (i = 0; i < count; i++) {
    Fb[i] = 0xFFFFFFFF;
  }
}

// pull the CURRENT framebuffer from the GOP (winload may have swapped it)
STATIC
VOID
RefreshFb (
  VOID
  )
{
  if ((mGop != NULL) && (mGop->Mode != NULL)) {
    mFbBase = (UINTN)mGop->Mode->FrameBufferBase;
    if (mGop->Mode->FrameBufferSize != 0) {
      mFbPixels = mGop->Mode->FrameBufferSize / 4;
    }
  }
}

STATIC
EFI_STATUS
EFIAPI
HookGetVariable (
  IN CHAR16 *VariableName, IN EFI_GUID *VendorGuid,
  OUT UINT32 *Attributes OPTIONAL, IN OUT UINTN *DataSize, OUT VOID *Data OPTIONAL
  )
{
  STATIC BOOLEAN done = FALSE;
  if (mAfterEbs && !done) { done = TRUE; FillFrac (4, 8); }   // HALF
  return mOrigGetVariable (VariableName, VendorGuid, Attributes, DataSize, Data);
}

STATIC
EFI_STATUS
EFIAPI
HookGetTime (
  OUT EFI_TIME *Time, OUT EFI_TIME_CAPABILITIES *Capabilities OPTIONAL
  )
{
  STATIC BOOLEAN done = FALSE;
  if (mAfterEbs && !done) { done = TRUE; FillFrac (5, 8); }
  return mOrigGetTime (Time, Capabilities);
}

STATIC
EFI_STATUS
EFIAPI
HookSVAM (
  IN UINTN MemoryMapSize, IN UINTN DescriptorSize,
  IN UINT32 DescriptorVersion, IN EFI_MEMORY_DESCRIPTOR *VirtualMap
  )
{
  FillFrac (6, 8);
  return mOrigSVAM (MemoryMapSize, DescriptorSize, DescriptorVersion, VirtualMap);
}

STATIC
VOID
EFIAPI
HookReset (
  IN EFI_RESET_TYPE ResetType, IN EFI_STATUS ResetStatus,
  IN UINTN DataSize, IN VOID *ResetData OPTIONAL
  )
{
  if (mAfterEbs) { FillFrac (8, 8); }
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
  RefreshFb ();          // GOP still valid in the EBS callback -> get winload's FB
  FillFrac (2, 8);
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
  EFI_STATUS  Status;
  UINT32      Crc;

  gBS->CloseEvent (Event);

  Status = gBS->LocateProtocol (&gEfiGraphicsOutputProtocolGuid, NULL, (VOID **)&mGop);
  if (EFI_ERROR (Status)) {
    mGop = NULL;
  }
  RefreshFb ();
  FillFrac (1, 8);       // ReadyToBoot marker

  mOrigGetVariable          = gRT->GetVariable;
  mOrigGetTime              = gRT->GetTime;
  mOrigSVAM                 = gRT->SetVirtualAddressMap;
  mOrigReset                = gRT->ResetSystem;
  gRT->GetVariable          = HookGetVariable;
  gRT->GetTime              = HookGetTime;
  gRT->SetVirtualAddressMap = HookSVAM;
  gRT->ResetSystem          = HookReset;
  gRT->Hdr.CRC32            = 0;
  gBS->CalculateCrc32 (gRT, gRT->Hdr.HeaderSize, &Crc);
  gRT->Hdr.CRC32            = Crc;

  gBS->CreateEvent (EVT_SIGNAL_EXIT_BOOT_SERVICES, TPL_NOTIFY,
                    OnExitBootServices, NULL, &mEbsEvent);
  gBS->CreateEventEx (EVT_NOTIFY_SIGNAL, TPL_NOTIFY, OnVirtualAddressChange,
                      NULL, &gEfiEventVirtualAddressChangeGuid, &mVacEvent);

  DEBUG ((DEBUG_ERROR, "[ebsprobe] RTB FB=0x%lx pixels=%d\n", (UINT64)mFbBase, mFbPixels));
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
