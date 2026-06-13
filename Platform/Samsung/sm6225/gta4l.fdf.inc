// per-device BSP DXEs for Samsung Galaxy Tab A7 (SM-T500 / gta4l / SM6115 Bengal)

// Buttons (power / volume via PMIC GPIO) - prebuilt sm6225 DXE
INF Platform/EFI_Binaries/Drivers/sm6225/ButtonsDxe/ButtonsDxe.inf

// ACPI Tables - the Windows-on-ARM hardware description for the SoC.
// DSDT adapted from the proven fog (sm6225) port; MADT/FADT/GTDT are the
// shared sm6225 SoC tables. This is what Windows reads to find the hardware.
FILE FREEFORM = 7E374E25-8E01-4FEE-87F2-390C23C606CD {
  SECTION RAW = Platform/Samsung/sm6225/AcpiTables/gta4l/DSDT.aml
  SECTION RAW = Silicon/Qualcomm/sm6225/AcpiTables/MADT.aml
  SECTION RAW = Silicon/Qualcomm/sm6225/AcpiTables/FADT.aml
  SECTION RAW = Silicon/Qualcomm/sm6225/AcpiTables/GTDT.aml
  SECTION RAW = Platform/Samsung/sm6225/AcpiTables/gta4l/SSDT-sdhc.aml
  SECTION UI = "AcpiTables"
}

// Device Tree blob (pulled from /sys/firmware/fdt on the live device)
FILE FREEFORM = 25462CDA-221F-47DF-AC1D-259CFAA4E326 {
  SECTION RAW = Platform/Samsung/sm6225/FdtBlob_compat/gta4l.dtb
}
