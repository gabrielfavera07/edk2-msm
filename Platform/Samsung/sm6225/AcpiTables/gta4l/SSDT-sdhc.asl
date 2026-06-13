// SSDT: SDHCI eMMC storage controller — Samsung Galaxy Tab A7 (gta4l / SM6115 "Bengal")
//
// Additive ACPI table (merged with the DSDT by the OS) that exposes the SM-T500's
// boot storage to Windows: qcom,sdhci-msm-v5 (+CQE) @ 0x04744000, eMMC (fstab=emmc).
//
// PATH 2 (custom miniport): bind our own sdhcmsm.sys (ACPI\BENG0610) — the MS inbox
//   SDHC sample + the Qualcomm CORE_PWRCTL handshake. The inbox PNP0D40 path hung at
//   the spinning dots because nothing serviced the power IRQ / brought up the rails.
//   Repo: github.com/gabrielfavera07/bengal-sdhc-driver
//
// Values from the live device tree (/sys/firmware/devicetree/base/soc/sdhci@4744000):
//   reg "hc_mem" = 0x04744000 size 0x1000   (standard SDHCI register block)
//   interrupts   = <GIC_SPI 348 LEVEL_HIGH>, <GIC_SPI 352 LEVEL_HIGH>  (hc_irq, pwr_irq)
//   ACPI GSIV = SPI + 32  ->  hc_irq = 380 (0x17C), pwr_irq = 384 (0x180)
//
// Done as an SSDT (not in DSDT.dsl) because the decompiled usb.dsl Device(URS0) lacks a
// _HID/_ADR and won't recompile; an SSDT keeps the storage device independent of that.

DefinitionBlock ("", "SSDT", 2, "QCOMM ", "SDHCMSM ", 0x00000002)
{
    Scope (\_SB)
    {
        Device (SDC0)
        {
            Name (_HID, "BENG0610")  // -> our sdhcmsm.sys (sdhcmsm.inf)
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
                        0x0000017C,         // hc_irq: GIC SPI 348 + 32 (only IRQ wired)
                    }
                    // NOTE: pwr_irq (SPI 352) is intentionally NOT declared. Like the
                    // Lumia 950 storage device (1 IRQ), the CORE_PWRCTL handshake is
                    // serviced by polling in sdhcmsm.sys (SdhcMsmAckPwrIrq), so the
                    // power IRQ stays GIC-unconnected (no ISR, no storm). The mask is
                    // still enabled in the driver so PWRCTL_STATUS latches the request.
                })
                Return (RBUF) /* \_SB_.SDC0._CRS.RBUF */
            }

            // Embedded eMMC device (non-removable), slot 0
            Device (EMMC)
            {
                Method (_ADR, 0, NotSerialized)  // _ADR: embedded slot 0
                {
                    Return (Zero)
                }

                Method (_RMV, 0, NotSerialized)  // _RMV: non-removable
                {
                    Return (Zero)
                }
            }
        }
    }
}
