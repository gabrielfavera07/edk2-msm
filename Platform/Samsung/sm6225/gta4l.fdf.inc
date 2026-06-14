// per-device BSP DXEs for Samsung Galaxy Tab A7 (SM-T500 / gta4l / SM6115 Bengal)

// Buttons (power / volume via PMIC GPIO) - prebuilt sm6225 DXE
INF Platform/EFI_Binaries/Drivers/sm6225/ButtonsDxe/ButtonsDxe.inf

// dwc3 USB device-mode bring-up (HS) so Windows KDNET-EEM finds a live controller.
// (The Khaje Usbfn prebuilts hang on Bengal; this is a minimal from-source bring-up.)
INF Platform/Samsung/sm6225/Drivers/UsbDeviceModeDxe/UsbDeviceModeDxe.inf

// Post-ExitBootServices probe: paints the screen green at the kernel's first
// runtime call (SetVirtualAddressMap) to see how far past EBS the kernel gets.
INF Platform/Samsung/sm6225/Drivers/EbsProbeDxe/EbsProbeDxe.inf

// ACPI Tables - the Windows-on-ARM hardware description for the SoC.
// MADT/FADT/GTDT are the Kamorta (SM6115) tables from Project-Silicium, which
// are tuned for the Windows kernel handoff (the shared sm6225 ones were
// incomplete and the kernel died pre-KdInitSystem). DSDT kept from fog.
FILE FREEFORM = 7E374E25-8E01-4FEE-87F2-390C23C606CD {
  SECTION RAW = Platform/Samsung/sm6225/AcpiTables/gta4l/DSDT.aml
  SECTION RAW = Platform/Samsung/sm6225/AcpiTables/gta4l/MADT-kamorta.aml
  SECTION RAW = Platform/Samsung/sm6225/AcpiTables/gta4l/FADT-kamorta.aml
  SECTION RAW = Platform/Samsung/sm6225/AcpiTables/gta4l/GTDT-kamorta.aml
  SECTION RAW = Platform/Samsung/sm6225/AcpiTables/gta4l/SSDT-sdhc.aml
  SECTION RAW = Platform/Samsung/sm6225/AcpiTables/gta4l/DBG2.aml
  SECTION UI = "AcpiTables"
}

// Device Tree blob (pulled from /sys/firmware/fdt on the live device)
FILE FREEFORM = 25462CDA-221F-47DF-AC1D-259CFAA4E326 {
  SECTION RAW = Platform/Samsung/sm6225/FdtBlob_compat/gta4l.dtb
}
