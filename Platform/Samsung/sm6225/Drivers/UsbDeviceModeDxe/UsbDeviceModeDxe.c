/** @file
 *  UsbDeviceModeDxe.c  -  SM6115 (Bengal) dwc3 USB device-mode bring-up for Windows KDNET-EEM
 *
 *  The stock Khaje USB prebuilts (UsbfnDwc3Dxe/UsbConfigDxe) hang on Bengal, so they are
 *  disabled.  Windows' kdnet KDNET-EEM debugger can drive the dwc3 itself, BUT it only does
 *  MMIO register pokes -- it cannot turn on clocks, power domains, or bring up the PHY.
 *
 *  This driver does exactly the minimum so kdnet finds a LIVE, High-Speed, device-mode dwc3:
 *      1. GCC USB30_PRIM RCGs + clock branches  (clocks ON)
 *      2. USB30_PRIM GDSC                        (power domain ON)
 *      3. QSCRATCH wrapper VBUS override         (force device session valid)
 *      4. QUSB2 High-Speed PHY init + PLL lock
 *      5. dwc3 core: PHY soft-reset -> core soft-reset -> GCTL.PrtCapDir = DEVICE
 *
 *  HS-only: the SS (QMP) PHY is intentionally left suspended -- KDNET-EEM links at High-Speed.
 *
 *  Runs at ReadyToBoot (everything powered, right before bootmgfw) so the state persists
 *  through ExitBootServices into winload/kdnet (nothing else touches the USB MMIO).
 *
 *  Register recipe: Linux gcc-sm6115.c / phy-qcom-qusb2.c / dwc3 core.c + sm6115.dtsi.
 **/

#include <Uefi.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiLib.h>
#include <Library/DebugLib.h>
#include <Library/IoLib.h>

//
// ---------- GCC (base 0x01400000) ----------
//
#define GCC_BASE                         0x01400000

// RCGs: CMD_RCGR (+0 bit0=UPDATE, self-clearing), CFG_RCGR (+4)
#define GCC_USB30_PRIM_MASTER_CMD_RCGR   (GCC_BASE + 0x1A01C)
#define GCC_USB30_PRIM_MASTER_CFG_RCGR   (GCC_BASE + 0x1A020)
#define GCC_USB30_PRIM_MOCK_UTMI_CMD     (GCC_BASE + 0x1A034)
#define GCC_USB30_PRIM_MOCK_UTMI_CFG     (GCC_BASE + 0x1A038)
#define RCG_UPDATE                       BIT0
#define MASTER_CFG_200MHZ                0x105   // src=1 GPLL0_EARLY, hid=0x5 (/3) -> 200 MHz
#define MOCK_UTMI_CFG_19MHZ              0x000   // src=0 TCXO, no div -> 19.2 MHz

// Clock branches (CBCR): set bit0 to ungate; bit31 = CLK_OFF (poll until clear)
#define CBCR_ENABLE                      BIT0
#define CBCR_CLK_OFF                     BIT31
STATIC CONST UINT32 mUsbCbcr[] = {
  GCC_BASE + 0x1A080,   // sys_noc_usb3_prim_axi
  GCC_BASE + 0x1A084,   // cfg_noc_usb3_prim_axi
  GCC_BASE + 0x1D008,   // ahb2phy_usb
  GCC_BASE + 0x1A010,   // usb30_prim_master
  GCC_BASE + 0x1A018,   // usb30_prim_mock_utmi
  GCC_BASE + 0x1A014,   // usb30_prim_sleep
  GCC_BASE + 0x1A054,   // usb3_prim_phy_com_aux
  GCC_BASE + 0x9F000,   // usb3_prim_clkref
};
#define GCC_USB3_PRIM_PHY_PIPE_CBCR      (GCC_BASE + 0x1A058)   // BRANCH_HALT_SKIP: enable, no poll

// GDSC
#define GCC_USB30_PRIM_GDSCR             (GCC_BASE + 0x1A004)
#define GDSC_SW_COLLAPSE                 BIT0
#define GDSC_PWR_ON                      BIT31

//
// ---------- dwc3-qcom wrapper / QSCRATCH (base 0x04EF8800) ----------
//
#define QSCRATCH_BASE                    0x04EF8800
#define QSCRATCH_HS_PHY_CTRL             (QSCRATCH_BASE + 0x10)  // 0x04EF8810
#define HS_UTMI_OTG_VBUS_VALID           BIT20
#define HS_SW_SESSVLD_SEL                BIT28

//
// ---------- QUSB2 High-Speed PHY (base 0x01613000) ----------
//
#define QUSB2_BASE                       0x01613000
#define QUSB2_PLL_TEST                   (QUSB2_BASE + 0x04)
#define QUSB2_PWR_CTRL                   (QUSB2_BASE + 0x18)
#define QUSB2_PLL_STATUS                 (QUSB2_BASE + 0x38)
#define QUSB2_POWERDOWN                  (QUSB2_BASE + 0xB4)
#define QUSB2_PLL_LOCKED                 BIT5        // 0x20 in PLL_STATUS
#define QUSB2_CLK_REF_SEL                BIT7        // 0x80 in PLL_TEST
#define QUSB2_DISABLE_CTRL               0x23        // CLAMP_N_EN | FREEZIO_N | POWER_DOWN
#define QUSB2_POWER_DOWN                 BIT0

// sm6115_init_tbl  (offset, value) -- write in order
STATIC CONST struct { UINT32 Off; UINT32 Val; } mQusb2InitTbl[] = {
  { 0x80, 0xF8 },   // PORT_TUNE1
  { 0x84, 0x53 },   // PORT_TUNE2
  { 0x88, 0x81 },   // PORT_TUNE3
  { 0x8C, 0x17 },   // PORT_TUNE4
  { 0x08, 0x30 },   // PLL_TUNE
  { 0x0C, 0x79 },   // PLL_USER_CTL1
  { 0x10, 0x21 },   // PLL_USER_CTL2
  { 0x9C, 0x14 },   // PORT_TEST2
  { 0x1C, 0x9F },   // PLL_AUTOPGM_CTL1
  { 0x18, 0x00 },   // PLL_PWR_CTRL
};

//
// ---------- dwc3 core (base 0x04E00000; globals already include +0xC100) ----------
//
#define DWC3_BASE                        0x04E00000
#define DWC3_GCTL                        (DWC3_BASE + 0xC110)
#define DWC3_GUSB2PHYCFG0                (DWC3_BASE + 0xC200)
#define DWC3_GUSB3PIPECTL0               (DWC3_BASE + 0xC2C0)
#define DWC3_DCTL                        (DWC3_BASE + 0xC704)

#define GCTL_CORESOFTRESET               BIT11
#define GCTL_DISSCRAMBLE                 BIT3
#define GCTL_DSBLCLKGTNG                 BIT0
#define GCTL_SCALEDOWN_MASK              (0x3 << 4)
#define GCTL_PRTCAPDIR_MASK              (0x3 << 12)
#define GCTL_PRTCAPDIR_DEVICE            (0x2 << 12)   // 0x2000

#define GUSB2_PHYSOFTRST                 BIT31
#define GUSB2_SUSPHY                     BIT6
#define GUSB2_PHYIF                      BIT3
#define GUSB2_USBTRDTIM_MASK             (0xF << 10)
#define GUSB2_USBTRDTIM_8BIT             (9 << 10)     // 0x2400 (8-bit UTMI+)

#define GUSB3_PHYSOFTRST                 BIT31
#define GUSB3_SUSPHY                     BIT17

#define DCTL_CSFTRST                     BIT30
#define DCTL_RUN_STOP                    BIT31

#define POLL_TIMEOUT_US                  100000   // 100 ms ceiling on any poll

//
// MMIO helpers (IoLib)
//
STATIC VOID Set32 (UINTN A, UINT32 M) { MmioOr32 (A, M); }
STATIC VOID Clr32 (UINTN A, UINT32 M) { MmioAnd32 (A, ~M); }

// Poll until (read & Mask) == Want, with a timeout. Returns TRUE on success.
STATIC BOOLEAN
PollBit (UINTN Addr, UINT32 Mask, UINT32 Want)
{
  UINTN i;
  for (i = 0; i < POLL_TIMEOUT_US; i += 10) {
    if ((MmioRead32 (Addr) & Mask) == Want) {
      return TRUE;
    }
    gBS->Stall (10);
  }
  return FALSE;
}

STATIC VOID
GccBringUp (VOID)
{
  UINTN i;

  // --- RCGs: write CFG, then pulse UPDATE ---
  MmioWrite32 (GCC_USB30_PRIM_MASTER_CFG_RCGR, MASTER_CFG_200MHZ);
  Set32 (GCC_USB30_PRIM_MASTER_CMD_RCGR, RCG_UPDATE);
  PollBit (GCC_USB30_PRIM_MASTER_CMD_RCGR, RCG_UPDATE, 0);   // self-clears

  MmioWrite32 (GCC_USB30_PRIM_MOCK_UTMI_CFG, MOCK_UTMI_CFG_19MHZ);
  Set32 (GCC_USB30_PRIM_MOCK_UTMI_CMD, RCG_UPDATE);
  PollBit (GCC_USB30_PRIM_MOCK_UTMI_CMD, RCG_UPDATE, 0);

  // --- clock branches: ungate + wait running ---
  for (i = 0; i < (sizeof (mUsbCbcr) / sizeof (mUsbCbcr[0])); i++) {
    Set32 (mUsbCbcr[i], CBCR_ENABLE);
    PollBit (mUsbCbcr[i], CBCR_CLK_OFF, 0);
  }
  // pipe clock: enable, BRANCH_HALT_SKIP (do not poll)
  Set32 (GCC_USB3_PRIM_PHY_PIPE_CBCR, CBCR_ENABLE);

  // --- GDSC: request power-up, wait PWR_ON ---
  Clr32 (GCC_USB30_PRIM_GDSCR, GDSC_SW_COLLAPSE);
  if (!PollBit (GCC_USB30_PRIM_GDSCR, GDSC_PWR_ON, GDSC_PWR_ON)) {
    DEBUG ((DEBUG_WARN, "[usbdev] GDSC PWR_ON timeout\n"));
  }
}

STATIC VOID
Qusb2HsPhyInit (VOID)
{
  UINT32 PllTest;
  UINTN  i;

  // clamp/disable PHY before programming
  Set32 (QUSB2_POWERDOWN, QUSB2_DISABLE_CTRL);

  PllTest = MmioRead32 (QUSB2_PLL_TEST);

  for (i = 0; i < (sizeof (mQusb2InitTbl) / sizeof (mQusb2InitTbl[0])); i++) {
    MmioWrite32 (QUSB2_BASE + mQusb2InitTbl[i].Off, mQusb2InitTbl[i].Val);
  }

  // enable PHY (clear POWER_DOWN)
  Clr32 (QUSB2_POWERDOWN, QUSB2_POWER_DOWN);
  gBS->Stall (150);

  // single-ended ref clock select
  MmioWrite32 (QUSB2_PLL_TEST, PllTest | QUSB2_CLK_REF_SEL);
  gBS->Stall (100);

  if (!PollBit (QUSB2_PLL_STATUS, QUSB2_PLL_LOCKED, QUSB2_PLL_LOCKED)) {
    DEBUG ((DEBUG_WARN, "[usbdev] QUSB2 PLL lock timeout (status=0x%08x)\n",
            MmioRead32 (QUSB2_PLL_STATUS)));
  }
}

STATIC VOID
Dwc3DeviceMode (VOID)
{
  UINT32 v;

  // A. HS PHY soft-reset pulse
  Set32 (DWC3_GUSB2PHYCFG0, GUSB2_PHYSOFTRST);
  gBS->Stall (10000);                       // 10 ms settle
  Clr32 (DWC3_GUSB2PHYCFG0, GUSB2_PHYSOFTRST);

  // B. core soft-reset (DCTL.CSFTRST), poll until clear
  v  = MmioRead32 (DWC3_DCTL);
  v |= DCTL_CSFTRST;
  v &= ~DCTL_RUN_STOP;
  MmioWrite32 (DWC3_DCTL, v);
  if (!PollBit (DWC3_DCTL, DCTL_CSFTRST, 0)) {
    DEBUG ((DEBUG_WARN, "[usbdev] dwc3 core soft-reset timeout\n"));
  }

  // C. global control: scaledown off, clock-gating on, scramble on
  v  = MmioRead32 (DWC3_GCTL);
  v &= ~GCTL_SCALEDOWN_MASK;
  v &= ~GCTL_DSBLCLKGTNG;
  v &= ~GCTL_DISSCRAMBLE;
  // D. PrtCapDir = DEVICE
  v &= ~GCTL_PRTCAPDIR_MASK;
  v |=  GCTL_PRTCAPDIR_DEVICE;
  MmioWrite32 (DWC3_GCTL, v);

  // E. HS PHY cfg: 8-bit UTMI+ (PHYIF=0, USBTRDTIM=9), clear SUSPHY
  v  = MmioRead32 (DWC3_GUSB2PHYCFG0);
  v &= ~(GUSB2_PHYIF | GUSB2_USBTRDTIM_MASK);
  v |=  GUSB2_USBTRDTIM_8BIT;
  v &= ~GUSB2_SUSPHY;
  MmioWrite32 (DWC3_GUSB2PHYCFG0, v);

  // F. HS-only: keep SS PHY suspended
  Set32 (DWC3_GUSB3PIPECTL0, GUSB3_SUSPHY);
}

STATIC
VOID
EFIAPI
OnReadyToBoot (
  IN EFI_EVENT  Event,
  IN VOID       *Context
  )
{
  gBS->CloseEvent (Event);

  DEBUG ((DEBUG_INFO, "[usbdev] SM6115 dwc3 device-mode bring-up (HS, for KDNET)...\n"));

  GccBringUp ();
  // VBUS override: force device session valid (no Type-C/OTG detection here)
  Set32 (QSCRATCH_HS_PHY_CTRL, HS_UTMI_OTG_VBUS_VALID | HS_SW_SESSVLD_SEL);
  Qusb2HsPhyInit ();
  Dwc3DeviceMode ();

  DEBUG ((DEBUG_INFO, "[usbdev] done. GCTL=0x%08x GUSB2PHYCFG=0x%08x DCTL=0x%08x\n",
          MmioRead32 (DWC3_GCTL), MmioRead32 (DWC3_GUSB2PHYCFG0), MmioRead32 (DWC3_DCTL)));
}

EFI_STATUS
EFIAPI
UsbDeviceModeDxeEntry (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  EFI_EVENT   Event;
  EFI_STATUS  Status;

  Status = gBS->CreateEventEx (
                  EVT_NOTIFY_SIGNAL,
                  TPL_CALLBACK,
                  OnReadyToBoot,
                  NULL,
                  &gEfiEventReadyToBootGuid,
                  &Event
                  );
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "[usbdev] CreateEventEx ReadyToBoot failed: %r\n", Status));
  }
  return Status;
}
