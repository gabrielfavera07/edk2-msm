#include <Library/BaseLib.h>
#include <Library/PlatformMemoryMapLib.h>

//
// Samsung Galaxy Tab A7 (SM-T500 / gta4l) - SM6115 "Bengal", 3 GB RAM
//
// Built region-by-region from the live device FDT (/sys/firmware/fdt):
//   /memory reg = <0x40000000 0x3E580000>  Bank 0 (hole at 0x7E580000!)
//                 <0xA0000000 0x60000000>  \ contiguous:
//                 <0x80000000 0x20000000>  / 0x80000000 - 0x100000000
//
// The generic sm6225 (spes) map assumes 4 GB and maps RAM up to
// 0x140000000 - phantom memory on this device. EDK2 allocates pages
// top-down, so the first allocation lands in nonexistent DRAM = crash.
//
static ARM_MEMORY_REGION_DESCRIPTOR_EX gDeviceMemoryDescriptorEx[] = {
/*                                                    EFI_RESOURCE_ EFI_RESOURCE_ATTRIBUTE_ EFI_MEMORY_TYPE ARM_REGION_ATTRIBUTE_
     MemLabel(32 Char.),  MemBase,    MemSize, BuildHob, ResourceType, ResourceAttribute, MemoryType, CacheAttributes
--------------------- DDR Bank 0: 0x40000000 - 0x7E580000 --------------------- */
  /* Kernel load window: Mu-Silicium gta4l RESERVES this; we marked it Conv, which
     made the Windows kernel use it post-EBS and die pre-KdInitSystem. Reserve it. */
  {"Kernel",                0x40000000, 0x05700000, AddMem, SYS_MEM, SYS_MEM_CAP, Reserv, WRITE_BACK_XN},
  {"Hypervisor",            0x45700000, 0x00600000, AddMem, SYS_MEM, SYS_MEM_CAP, Reserv, WRITE_BACK_XN},
  /* Boot Info: Mu reserves the first 128 KB as BsData */
  {"Boot Info",             0x45D00000, 0x00020000, AddMem, SYS_MEM, SYS_MEM_CAP, BsData, WRITE_BACK_XN},
  {"RAM Partition",         0x45D20000, 0x000E0000, AddMem, SYS_MEM, SYS_MEM_CAP, Conv,   WRITE_BACK_XN},
  {"XBL AOP",               0x45E00000, 0x00140000, AddMem, MEM_RES, WRITE_COMBINEABLE,   Reserv, UNCACHED_UNBUFFERED_XN},
  {"RAM Partition",         0x45F40000, 0x000BF000, AddMem, SYS_MEM, SYS_MEM_CAP, Conv,   WRITE_BACK_XN},
  {"Sec Apps",              0x45FFF000, 0x00001000, AddMem, MEM_RES, WRITE_COMBINEABLE,   Reserv, UNCACHED_UNBUFFERED_XN},
  {"SMEM",                  0x46000000, 0x00200000, AddMem, MEM_RES, WRITE_COMBINEABLE,   Reserv, UNCACHED_UNBUFFERED_XN},
  {"RAM Partition",         0x46200000, 0x04900000, AddMem, SYS_MEM, SYS_MEM_CAP, Conv,   WRITE_BACK_XN},
  /* modem 0x4AB00000 + video/wlan/cdsp/adsp/ipa/gpu PIL chain, ends 0x55617000 */
  {"PIL Reserved",          0x4AB00000, 0x0AB17000, AddMem, MEM_RES, WRITE_COMBINEABLE,   Reserv, UNCACHED_UNBUFFERED_XN},
  {"RAM Partition",         0x55617000, 0x029E9000, AddMem, SYS_MEM, SYS_MEM_CAP, Conv,   WRITE_BACK_XN},
  /* UEFI FD runs HERE (low sandbox, executable) - FD_BASE 0x58000000, 7 MB */
  {"UEFI FD",               0x58000000, 0x00700000, AddMem, SYS_MEM, SYS_MEM_CAP, BsData, WRITE_BACK},
  {"RAM Partition",         0x58700000, 0x03900000, AddMem, SYS_MEM, SYS_MEM_CAP, Conv,   WRITE_BACK_XN},
  /* cont_splash_region@5c000000 - the live framebuffer */
  {"Display Reserved",      0x5C000000, 0x00F00000, AddMem, MEM_RES, SYS_MEM_CAP, Reserv, WRITE_THROUGH_XN},
  {"DFPS Data",             0x5CF00000, 0x00100000, AddMem, MEM_RES, WRITE_COMBINEABLE,   Reserv, UNCACHED_UNBUFFERED_XN},
  {"RAM Partition",         0x5D000000, 0x02F00000, AddMem, SYS_MEM, SYS_MEM_CAP, Conv,   WRITE_BACK_XN},
  {"SEC Heap",              0x5FF00000, 0x0008C000, AddMem, SYS_MEM, SYS_MEM_CAP, BsData, WRITE_BACK_XN},
  {"CPU Vectors",           0x5FF8C000, 0x00001000, AddMem, SYS_MEM, SYS_MEM_CAP, BsData, WRITE_BACK},
  {"MMU PageTables",        0x5FF8D000, 0x00003000, AddMem, SYS_MEM, SYS_MEM_CAP, BsData, WRITE_BACK_XN},
  {"UEFI Stack",            0x5FF90000, 0x00040000, AddMem, SYS_MEM, SYS_MEM_CAP, BsData, WRITE_BACK_XN},
  {"RAM Partition",         0x5FFD0000, 0x00020000, AddMem, SYS_MEM, SYS_MEM_CAP, Conv,   WRITE_BACK_XN},
  /* *** THE HANDOFF FIX *** Windows ARM64 requires EVERY runtime (RtData/RtCode)
     entry to be 64 KB-aligned in BOTH base and size. The old Log Buffer
     (0x5FFF7000) + Info Blk (0x5FFFF000) were 4 KB-aligned, so winload aborts at
     ExitBootServices: "A RUNTIME memory entry is not on a proper alignment"
     (Fatal 0x1, STATUS_INVALID_PARAMETER) and the kernel never starts. Merge them
     into one 64 KB-aligned runtime block 0x5FFF0000..0x60000000. The actual log/info
     buffers (0x5FFF7000 / 0x5FFFF000) still live inside it, still runtime. */
  {"UEFI Runtime Blk",      0x5FFF0000, 0x00010000, AddMem, SYS_MEM, SYS_MEM_CAP, RtData, WRITE_BACK_XN},
  /* removed_region@60000000 - secure no-map hole, do NOT use as RAM */
  {"Removed Region",        0x60000000, 0x03900000, AddMem, MEM_RES, WRITE_COMBINEABLE,   Reserv, UNCACHED_UNBUFFERED_XN},
  /* DXE heap sized to stop exactly at Samsung ss_plog@71100000 */
  {"DXE Heap",              0x63900000, 0x0D800000, AddMem, SYS_MEM, SYS_MEM_CAP, Conv,   WRITE_BACK},
  /* ss_plog + ramoops + sec_debug chain: 0x71100000 - 0x71C00000 */
  {"SS PLOG",               0x71100000, 0x00B00000, AddMem, MEM_RES, WRITE_COMBINEABLE,   Reserv, UNCACHED_UNBUFFERED_XN},
  {"RAM Partition",         0x71C00000, 0x0C980000, AddMem, SYS_MEM, SYS_MEM_CAP, Conv,   WRITE_BACK_XN},
  /* hole 0x7E580000 - 0x80000000: NOT mapped (no DRAM there) */

/*--------------------- DDR Banks 1+2: 0x80000000 - 0x100000000 (2 GB) ---------------------*/
  /* FD no longer lives up here - now in the low sandbox at 0x58000000 */
  {"RAM Partition",         0x80000000, 0x80000000, AddMem, SYS_MEM, SYS_MEM_CAP, Conv,   WRITE_BACK_XN},

/*--------------------- Other ---------------------*/
  {"RPM_SS_MSG_RAM",        0x045F0000, 0x00007000, NoHob,  MMAP_IO, INITIALIZED, Conv,   NS_DEVICE},
  {"IMEM Base",             0x0C100000, 0x00026000, NoHob,  MMAP_IO, INITIALIZED, Conv,   NS_DEVICE},
  {"IMEM Cookie Base",      0x0C125000, 0x00001000, AddDev, MMAP_IO, INITIALIZED, Conv,   NS_DEVICE},

  /* Register regions - verified identical to live /proc/iomem on SM-T500 */
  {"TCSR_TCSR_REGS",        0x003C0000, 0x00040000, AddDev, MMAP_IO, UNCACHEABLE, MmIO,   NS_DEVICE},
  {"TLMM_WEST",             0x00500000, 0x00300000, AddDev, MMAP_IO, UNCACHEABLE, MmIO,   NS_DEVICE},
  {"TLMM_SOUTH",            0x00900000, 0x00300000, AddDev, MMAP_IO, UNCACHEABLE, MmIO,   NS_DEVICE},
  {"TLMM_EAST",             0x00D00000, 0x00300000, AddDev, MMAP_IO, UNCACHEABLE, MmIO,   NS_DEVICE},
  {"GCC CLK CTL",           0x01400000, 0x00200000, AddDev, MMAP_IO, UNCACHEABLE, MmIO,   NS_DEVICE},
  {"PMIC ARB SPMI",         0x01C00000, 0x02800000, AddDev, MMAP_IO, UNCACHEABLE, MmIO,   NS_DEVICE},
  {"MMCX_CPR3",             0x01648000, 0x00008000, AddDev, MMAP_IO, UNCACHEABLE, MmIO,   NS_DEVICE},
  {"CRYPTO0 CRYPTO",        0x01B00000, 0x00040000, AddDev, MMAP_IO, UNCACHEABLE, MmIO,   NS_DEVICE},
  {"SECURITY CONTROL",      0x01B40000, 0x00010000, AddDev, MMAP_IO, UNCACHEABLE, MmIO,   NS_DEVICE},
  {"PRNG_CFG_PRNG",         0x01B50000, 0x00010000, AddDev, MMAP_IO, UNCACHEABLE, MmIO,   NS_DEVICE},
  {"SLP_CNTR",              0x04403000, 0x00001000, AddDev, MMAP_IO, UNCACHEABLE, MmIO,   NS_DEVICE},
  {"TSENS0",                0x04410000, 0x00001000, AddDev, MMAP_IO, UNCACHEABLE, MmIO,   NS_DEVICE},
  {"TSENS0_TM",             0x04411000, 0x00001000, AddDev, MMAP_IO, UNCACHEABLE, MmIO,   NS_DEVICE},
  {"PSHOLD",                0x0440B000, 0x00001000, AddDev, MMAP_IO, UNCACHEABLE, MmIO,   NS_DEVICE},
  {"QUPV3_0_GSI",           0x04A00000, 0x000D0000, AddDev, MMAP_IO, UNCACHEABLE, MmIO,   NS_DEVICE},
  {"QUPV3_1_GSI",           0x04C00000, 0x000D0000, AddDev, MMAP_IO, UNCACHEABLE, MmIO,   NS_DEVICE},
  {"UFS UFS REGS",          0x04800000, 0x00020000, AddDev, MMAP_IO, UNCACHEABLE, MmIO,   NS_DEVICE},
  {"PERIPH_SS",             0x04700000, 0x00200000, AddDev, MMAP_IO, UNCACHEABLE, MmIO,   NS_DEVICE},
  {"USB30_PRIM",            0x04E00000, 0x00200000, AddDev, MMAP_IO, UNCACHEABLE, MmIO,   NS_DEVICE},
  {"GPU_GMU_CX_BLK",        0x0597D000, 0x0000C000, AddDev, MMAP_IO, UNCACHEABLE, MmIO,   NS_DEVICE},
  {"GPU_CC",                0x05990000, 0x00009000, AddDev, MMAP_IO, UNCACHEABLE, MmIO,   NS_DEVICE},
  {"VIDEO_CC",              0x05B00000, 0x00020000, AddDev, MMAP_IO, UNCACHEABLE, MmIO,   NS_DEVICE},
  {"MDSS",                  0x05E00000, 0x00200000, AddDev, MMAP_IO, UNCACHEABLE, MmIO,   NS_DEVICE},
  {"DISP_CC_DISP_CC",       0x05F00000, 0x00020000, AddDev, MMAP_IO, UNCACHEABLE, MmIO,   NS_DEVICE},
  {"SMMU",                  0x0C600000, 0x00080000, AddDev, MMAP_IO, UNCACHEABLE, MmIO,   NS_DEVICE},
  {"APSS_WDT_TMR1",         0x0F017000, 0x00001000, AddDev, MMAP_IO, UNCACHEABLE, MmIO,   NS_DEVICE},
  {"QTIMER",                0x0F020000, 0x00110000, AddDev, MMAP_IO, UNCACHEABLE, MmIO,   NS_DEVICE},
  {"APCS_ALIAS0_GLB",       0x0F111000, 0x00001000, AddDev, MMAP_IO, UNCACHEABLE, MmIO,   NS_DEVICE},
  {"APSS_GIC500_GICD",      0x0F200000, 0x00010000, AddDev, MMAP_IO, UNCACHEABLE, MmIO,   NS_DEVICE},
  {"APSS_GIC500_GICR",      0x0F300000, 0x00020000, AddDev, MMAP_IO, UNCACHEABLE, MmIO,   NS_DEVICE},
  {"OSM_RAIL",              0x0F520000, 0x00020000, AddDev, MMAP_IO, UNCACHEABLE, MmIO,   NS_DEVICE},
  {"APSS_ACTPM_WRAP",       0x0F500000, 0x000B0000, AddDev, MMAP_IO, UNCACHEABLE, MmIO,   NS_DEVICE},
  {"USB2",                  0x01610000, 0x00010000, AddDev, MMAP_IO, UNCACHEABLE, MmIO,   NS_DEVICE},
  {"MCCC_MCCC_MSTR",        0x0447D000, 0x00001000, AddDev, MMAP_IO, UNCACHEABLE, MmIO,   NS_DEVICE},

    /* Terminator for MMU */
    {"Terminator", 0, 0, 0, 0, 0, 0, 0}};

ARM_MEMORY_REGION_DESCRIPTOR_EX *GetPlatformMemoryMap()
{
  return gDeviceMemoryDescriptorEx;
}
