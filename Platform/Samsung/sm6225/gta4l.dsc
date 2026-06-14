[Defines]
  VENDOR_NAME                    = Samsung
  PLATFORM_NAME                  = gta4l
  PLATFORM_GUID                  = 7b1e9a02-5c44-4e8b-9a3d-1f2c6d4e8a90
  PLATFORM_VERSION               = 0.1
  DSC_SPECIFICATION              = 0x00010019
  OUTPUT_DIRECTORY               = Build/$(PLATFORM_NAME)
  SUPPORTED_ARCHITECTURES        = AARCH64
  BUILD_TARGETS                  = DEBUG|RELEASE
  SKUID_IDENTIFIER               = DEFAULT
  FLASH_DEFINITION               = Platform/Qualcomm/sm6225/sm6225.fdf
  DEVICE_DXE_FV_COMPONENTS       = Platform/Samsung/sm6225/gta4l.fdf.inc

  # SM-T500 is A-only (partitions are xblbak/xbl, not xbl_a/xbl_b)
  AB_SLOTS_SUPPORT               = FALSE

!include Platform/Qualcomm/sm6225/sm6225.dsc

# Device-specific 3 GB memory map (overrides the generic 4 GB sm6225 one;
# last assignment of a library class wins in EDK2)
[LibraryClasses.common]
  PlatformMemoryMapLib|Platform/Samsung/sm6225/Library/gta4l/PlatformMemoryMapLib/PlatformMemoryMapLib.inf

[Components.common]
  # dwc3 USB device-mode bring-up (HS) for Windows KDNET-EEM observability
  Platform/Samsung/sm6225/Drivers/UsbDeviceModeDxe/UsbDeviceModeDxe.inf

[BuildOptions.common]
  GCC:*_*_AARCH64_CC_FLAGS = -DENABLE_SIMPLE_INIT

[PcdsFixedAtBuild.common]
  # Galaxy Tab A7 (SM-T500) panel: 1200x2000 native portrait
  gQcomTokenSpaceGuid.PcdMipiFrameBufferWidth|1200
  gQcomTokenSpaceGuid.PcdMipiFrameBufferHeight|2000

  # Simple Init GUI DPI (~224 ppi panel, density 240)
  gSimpleInitTokenSpaceGuid.PcdGuiDefaultDPI|240

  gRenegadePkgTokenSpaceGuid.PcdDeviceVendor|"Samsung"
  gRenegadePkgTokenSpaceGuid.PcdDeviceProduct|"Galaxy Tab A7"
  gRenegadePkgTokenSpaceGuid.PcdDeviceCodeName|"gta4l"

# Produce the highest video mode in Shell and UiApp
[PcdsDynamicDefault.common]
  gEfiMdeModulePkgTokenSpaceGuid.PcdVideoHorizontalResolution|0
  gEfiMdeModulePkgTokenSpaceGuid.PcdVideoVerticalResolution|0
  gEfiMdeModulePkgTokenSpaceGuid.PcdSetupVideoHorizontalResolution|0
  gEfiMdeModulePkgTokenSpaceGuid.PcdSetupVideoVerticalResolution|0
  gEfiMdeModulePkgTokenSpaceGuid.PcdSetupConOutRow|0
  gEfiMdeModulePkgTokenSpaceGuid.PcdSetupConOutColumn|0
  gEfiMdeModulePkgTokenSpaceGuid.PcdConOutRow|0
  gEfiMdeModulePkgTokenSpaceGuid.PcdConOutColumn|0
