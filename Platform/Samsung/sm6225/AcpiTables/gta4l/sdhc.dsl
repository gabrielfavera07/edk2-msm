// SDHCI eMMC controller — Samsung Galaxy Tab A7 (gta4l / SM6115 "Bengal")
// Boot storage of the SM-T500: qcom,sdhci-msm-v5 (+CQE) @ 0x04744000, eMMC (fstab=emmc).
//
// PATH 1 (inbox): target Microsoft's inbox SD Standard Host Controller stack
//   (sdhc/sdport/sdbus/sdstor) via _HID PNP0D40 — the Lumia 950 model, which booted
//   Windows from eMMC on a Qualcomm SDHCI controller WITHOUT a custom storage .sys.
//
// Values from the live device tree (/sys/firmware/devicetree/base/soc/sdhci@4744000):
//   reg "hc_mem" = 0x04744000 size 0x1000   (standard SDHCI register block)
//   interrupts   = <GIC_SPI 348 LEVEL_HIGH>, <GIC_SPI 352 LEVEL_HIGH>  (hc_irq, pwr_irq)
//   ACPI GSIV = SPI + 32  ->  hc_irq = 380 (0x17C), pwr_irq = 384 (0x180)
//
// If the inbox driver can't drive the msm vendor quirks (clock/DLL via the vendor core
// block), PATH 2 = a Qualcomm SDHCI miniport ported from Linux sdhci-msm.c.

Device (SDC0)
{
    Name (_HID, "PNP0D40")   // SDA-compliant SD Host Controller (Microsoft inbox driver)
    Name (_UID, One)
    Name (_CCA, Zero)        // non-coherent DMA (matches UFS0)

    Method (_STA, 0, NotSerialized)  // present + enabled + functioning
    {
        Return (0x0F)
    }

    Method (_CRS, 0, NotSerialized)
    {
        Name (RBUF, ResourceTemplate ()
        {
            Memory32Fixed (ReadWrite,
                0x04744000,         // hc_mem — standard SDHCI register block
                0x00001000,
                )
            Interrupt (ResourceConsumer, Level, ActiveHigh, Exclusive, ,, )
            {
                0x0000017C,         // hc_irq: GIC SPI 348 + 32
            }
        })
        Return (RBUF) /* \_SB_.SDC0._CRS.RBUF */
    }

    // Embedded eMMC device (non-removable), slot 0
    Device (EMMC)
    {
        Method (_ADR, 0, NotSerialized)  // _ADR: Address (embedded slot 0)
        {
            Return (Zero)
        }

        Method (_RMV, 0, NotSerialized)  // _RMV: non-removable
        {
            Return (Zero)
        }
    }
}
