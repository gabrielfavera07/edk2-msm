/** @file
 *  EbsProbeDxe.c  -  post-ExitBootServices observability for the Windows kernel handoff
 *
 *  The Windows boot debugger (winload) connects over KDNET but won't hold, and the
 *  kernel dies BEFORE KdInitSystem (its own debugger), so we're blind right where it
 *  dies: the first moments of the kernel, post-ExitBootServices.
 *
 *  This runtime driver hooks gRT->SetVirtualAddressMap -- the kernel's FIRST UEFI
 *  runtime call, made very early (after the initial memory bring-up, before KdInitSystem).
 *  When the kernel calls it, we paint the framebuffer GREEN, then chain to the real one.
 *
 *      screen turns GREEN  -> kernel reached the runtime phase (past earliest init);
 *                             the death is in the narrow SVAM..KdInitSystem window.
 *      stays on the logo   -> kernel died before SVAM (memory/page-table setup).
 *
 *  The driver's code lives in EfiRuntimeServicesCode so the hook survives EBS.
 **/

#include <Uefi.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiRuntimeServicesTableLib.h>
#include <Library/UefiLib.h>
#include <Library/DebugLib.h>
#include <Library/BaseLib.h>

#define FB_BASE        0x5C000000          // GOP framebuffer (Display Reserved)
#define FB_BAR_PIXELS  (1200 * 200)        // a green bar across the top
#define FB_GREEN       0xFF00FF00

STATIC EFI_SET_VIRTUAL_ADDRESS_MAP  mOrigSetVirtualAddressMap = NULL;
STATIC EFI_EVENT                    mVirtualAddrChangeEvent   = NULL;

STATIC
VOID
PaintGreen (
  VOID
  )
{
  volatile UINT32  *fb = (volatile UINT32 *)(UINTN)FB_BASE;
  UINTN            i;

  for (i = 0; i < FB_BAR_PIXELS; i++) {
    fb[i] = FB_GREEN;
  }
}

//
// Our hook: runs at the kernel's first runtime call (still at physical addresses,
// because the address conversion happens INSIDE the real SetVirtualAddressMap).
//
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
  PaintGreen ();   // <-- the breadcrumb: kernel reached the runtime phase
  return mOrigSetVirtualAddressMap (MemoryMapSize, DescriptorSize, DescriptorVersion, VirtualMap);
}

STATIC
VOID
EFIAPI
OnVirtualAddressChange (
  IN EFI_EVENT  Event,
  IN VOID       *Context
  )
{
  // Convert our saved pointer so a later call would still resolve (defensive).
  gRT->ConvertPointer (0, (VOID **)&mOrigSetVirtualAddressMap);
}

EFI_STATUS
EFIAPI
EbsProbeDxeEntry (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  EFI_STATUS  Status;
  UINT32      Crc;

  // install the hook into the runtime services table
  mOrigSetVirtualAddressMap = gRT->SetVirtualAddressMap;
  gRT->SetVirtualAddressMap = HookSetVirtualAddressMap;

  // fix the RT table CRC so winload/kernel don't reject it
  gRT->Hdr.CRC32 = 0;
  gBS->CalculateCrc32 (gRT, gRT->Hdr.HeaderSize, &Crc);
  gRT->Hdr.CRC32 = Crc;

  Status = gBS->CreateEventEx (
                  EVT_NOTIFY_SIGNAL,
                  TPL_NOTIFY,
                  OnVirtualAddressChange,
                  NULL,
                  &gEfiEventVirtualAddressChangeGuid,
                  &mVirtualAddrChangeEvent
                  );
  DEBUG ((DEBUG_INFO, "[ebsprobe] hooked SetVirtualAddressMap (orig=%p) status=%r\n",
          mOrigSetVirtualAddressMap, Status));
  return EFI_SUCCESS;
}
