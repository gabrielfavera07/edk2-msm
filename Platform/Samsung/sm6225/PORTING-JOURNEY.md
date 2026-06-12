# Porting edk2-msm (Renegade UEFI) to a new device — the full journey

**Worked example: Samsung Galaxy Tab A7 Wi-Fi (SM-T500 / `gta4l` / SoC SM6115 "Bengal" / Adreno 610)**

This document is a complete, reproducible log of porting the open Qualcomm UEFI
(edk2-msm / Renegade Project) to a brand-new device and SoC. It is written so it
can be **replayed on any other Qualcomm device** — the device-specific values
change, but the *method* does not.

It is deliberately heavy on the **debugging methodology**, because for a first
boot the hard part is never "writing the config" — it is *seeing what the silicon
is doing when you have no serial console*. Every technique below was used for
real on this device.

> Status at time of writing: the UEFI **compiles**, the boot image is accepted by
> the bootloader, and we are deep in **first-boot bring-up**. The build/packaging
> chapters are proven; the bring-up chapter is a living log. Read it as "how to
> investigate", not "guaranteed to boot".

---

## 0. TL;DR mental model

```
XBL (primary BL, signed)  ->  ABL (aboot, Linux-kernel loader)  ->  [ boot partition ]
                                                                         |
                          our boot.img: header v2  =  [ gzip(BootShim + UEFI.fd) ] + [ DTB section ] + [ ramdisk ]
                                                                         |
                          ABL decompresses the "kernel", jumps to it  -> BootShim
                                                                         |
                          BootShim relocates UEFI.fd to FD_BASE, jumps -> EDK2 PrePi -> DXE -> BDS -> UEFI shell / OS loader
```

The whole port is: **make the ABL launch our fake "kernel", then make EDK2 run on
this silicon.** Two completely separate problems — do not conflate them (we wasted
~6 boot cycles fixing problem #2 when the wall was problem #1).

---

## 1. Why this was *much* easier than it sounds — find the silicon sibling

edk2-msm only ships full SoC support packages for a handful of chips. Bengal
(SM6115) was **not** one of them. A from-scratch SoC bring-up is a months-long
effort. **But** the repo *does* ship `sm6225` ("Khaje", Snapdragon 680/685),
which is the **same Qualcomm "Bengal" platform family** as SM6115.

**Step 1 of any port: find the closest already-supported SoC, then PROVE it is
close by cross-checking register bases.** We dumped the live device memory map and
compared it to `Silicon/Qualcomm/sm6225/Library/PlatformMemoryMapLib`:

| Block        | Live SM-T500 `/proc/iomem` | edk2 `sm6225` | Match |
|--------------|----------------------------|---------------|-------|
| GCC clocks   | `0x01400000`               | `0x01400000`  | exact |
| USB2 / EUD   | `0x01610000`               | `0x01610000`  | exact |
| Security ctl | `0x01b40000`               | `0x01B40000`  | exact |
| TSENS        | `0x04410000`               | `0x04410000`  | exact |
| USB30        | `0x04e00000`               | `0x04E00000`  | exact |
| GPU_CC       | `0x05990000`               | `0x05990000`  | exact |
| DISP_CC      | `0x05f00000`               | `0x05F00000`  | exact |

Identical → the entire SoC support layer transfers. The port collapses from
"write an SoC from scratch" to "write a thin **device package**".

> Generalising: for *your* device, identify the SoC, then look in
> `Silicon/Qualcomm/` for the same chip **or a sibling on the same platform**
> (Qualcomm "platform" codenames: bengal, lahaina, kona, lito, trinket…). If the
> register bases match, reuse that SoC package.

---

## 2. Collect the device's ground-truth artifacts (do this FIRST)

Everything below was pulled from the live device over `adb` (rooted, or from a
custom recovery shell which is already root). **Never guess a value you can read.**

```bash
ADB=adb   # or full path to adb.exe

# SoC identity
adb shell "getprop ro.board.platform; cat /sys/devices/soc0/soc_id"

# 2.1 The device tree the firmware actually uses (THE key artifact)
adb exec-out "su -c 'cat /sys/firmware/fdt'" > device.dtb      # ~800 KB

# 2.2 The MMIO / DDR map (sanity-check against the SoC package)
adb shell "su -c 'cat /proc/iomem'"

# 2.3 The bootchain partitions (find boot, recovery, xbl, dtbo…)
adb shell "su -c 'ls -l /dev/block/by-name/'"

# 2.4 Panel resolution + density (framebuffer PCDs)
adb shell "wm size; wm density"

# 2.5 The stock boot.img header (load geometry the ABL is PROVEN to accept)
adb shell "su -c 'dd if=/dev/block/by-name/boot bs=4096 count=1' " | xxd | head

# 2.6 THE AUTHORITATIVE MEMORY MAP — uefiplat.cfg lives inside the xbl ELF
adb shell "su -c 'dd if=/dev/block/by-name/xbl of=/data/local/tmp/xbl.img'"
adb pull /data/local/tmp/xbl.img
# then extract the text block that starts at "MaxMemoryRegions ="
python3 - <<'PY'
d=open('xbl.img','rb').read(); s=d.find(b'MaxMemoryRegions = ')
print(d[s:s+0x3000].decode('latin1'))
PY
```

`uefiplat.cfg` is gold: it is the **exact** memory map the OEM's own UEFI uses
(every reserved region, the framebuffer address, where it runs the UEFI FD, etc.).
The Renegade porting guide literally says "start from `uefiplat.cfg` inside xbl".
We skipped it at first and paid for it — **don't**.

---

## 3. The device package — only four small files

For SoC `sm6225`, vendor `Samsung`, device `gta4l`, the entire device package is:

```
Platform/Samsung/sm6225/
├── gta4l.dsc                       # framebuffer res, DPI, device strings, lib overrides
├── gta4l.fdf.inc                   # points the build at the DTB blob
├── gta4l.sh.inc                    # device-specific boot.img packaging hooks (see §6)
├── FdtBlob_compat/gta4l.dtb        # the /sys/firmware/fdt we pulled
└── Library/gta4l/PlatformMemoryMapLib/   # optional per-device memory map override
configs/devices/gta4l.conf          # SOC_PLATFORM, header version, FD_BASE override
```

### 3.1 `configs/devices/gta4l.conf`
```sh
SOC_PLATFORM="SM6225"            # reuse the sibling SoC package
VENDOR_NAME="Samsung"
PLATFORM_NAME="gta4l"
BOOTIMG_OS_PATCH_LEVEL="2026-05"
BOOTIMG_OS_VERSION=16.0.0
BOOTIMG_HEADER_VERSION=2         # see §6 — Samsung wants v2 + a real DTB section
# FD_BASE / FD_SIZE here OVERRIDE configs/sm6225.conf (build.sh re-sources the
# device conf after the SoC conf), e.g. to run UEFI in low memory.
```

### 3.2 `gta4l.dsc` (the important knobs)
```
!include Platform/Qualcomm/sm6225/sm6225.dsc        # inherit the whole SoC layer
[PcdsFixedAtBuild.common]
  gQcomTokenSpaceGuid.PcdMipiFrameBufferWidth|1200  # from `wm size`
  gQcomTokenSpaceGuid.PcdMipiFrameBufferHeight|2000
  gSimpleInitTokenSpaceGuid.PcdGuiDefaultDPI|240    # from `wm density`
[LibraryClasses.common]                             # last definition wins -> override
  PlatformMemoryMapLib|Platform/Samsung/sm6225/Library/gta4l/.../PlatformMemoryMapLib.inf
```

### 3.3 The memory map (`PlatformMemoryMapLib.c`)
Build it from **two** sources:
* the **reserved/MMIO regions** come from `uefiplat.cfg` (authoritative);
* the **DDR bank sizes** come from the device tree `/memory` node, because a 3 GB
  device must NOT declare the 4 GB layout the generic Khaje map assumes (EDK2
  allocates top-down and will fault into phantom DRAM).

Decode the DTB `/memory reg` yourself:
```python
# /memory reg = <0x40000000 0x3E580000> <0xA0000000 0x60000000> <0x80000000 0x20000000>
# => banks 0x40000000-0x7E580000 and 0x80000000-0x100000000, ~3 GB. Map only these.
```

---

## 4. Build — GitHub Actions, no local toolchain

edk2-msm builds need a full Linux toolchain + heavy submodules (edk2, edk2-platforms,
SimpleInit, boringssl…). Don't fight it locally — fork the repo and let CI build.

`.github/workflows/build-gta4l.yml` (essentials):
```yaml
- run: |
    sudo apt-get install -y build-essential uuid-dev clang llvm iasl nasm \
      gcc-aarch64-linux-gnu abootimg python3-pil python3-git gettext \
      libgcc-s1:i386 libstdc++6:i386 winehq-stable libsdl2-2.0-0
- run: ./build.sh --device gta4l --release DEBUG --installer-zip
- uses: actions/upload-artifact@v4
  with: { name: gta4l-uefi, path: "./*.img" }
```

Output: `boot-<device>.img` (flashable) and `uefi-installer-<device>.zip`.

**DEBUG vs `--uart` matters for debugging** (see §7): with `--uart` the SerialPortLib
is the GENI UART (invisible without EUD/serial HW); *without* `--uart` it is
`FrameBufferSerialPortLib`, which prints the EDK2 log **on the panel**. For a blind
first boot, build **DEBUG without `--uart`**.

Trigger + collect:
```bash
gh repo fork edk2-porting/edk2-msm --clone=false
git remote add fork https://github.com/<you>/edk2-msm.git
git push fork HEAD:gta4l
gh api -X PUT repos/<you>/edk2-msm/actions/permissions -F enabled=true
gh workflow run build-gta4l.yml --repo <you>/edk2-msm --ref gta4l
gh run watch <run-id> --repo <you>/edk2-msm --exit-status
gh run download <run-id> --repo <you>/edk2-msm --dir out
```

---

## 5. Flash — safely, from recovery, always verified

We flash by `dd` from a custom recovery (root shell). **Every flash is hash-verified
by reading the partition back.** Always keep a backup and never trust a silent `dd`.

```bash
# from recovery (Advanced -> Enable ADB)
IMG=boot-gta4l.img
SZ=$(stat -c%s "$IMG"); M=$(md5sum "$IMG" | cut -d' ' -f1)

adb push "$IMG" /tmp/x.img
adb shell "dd if=/tmp/x.img of=/dev/block/by-name/boot bs=1048576 && sync"
# read it back and confirm the hash matches before rebooting:
adb shell "dd if=/dev/block/by-name/boot bs=$SZ count=1 2>/dev/null | md5sum"   # == $M
adb reboot
```

Restore at any time: `dd` your backed-up boot image back, or just stay in recovery
(it is a separate partition and is unaffected).

> Gotcha (Windows/Git-Bash): prefix adb shell paths with `MSYS_NO_PATHCONV=1` or the
> shell rewrites `/data/...` into `C:/Program Files/Git/data/...` and the push
> "succeeds" into nowhere.

---

## 6. The boot-image format wars (Samsung-specific, but instructive)

The generic edk2-msm packaging (header **v1**, gzip kernel with the **DTB appended**
after the gzip stream) boots on Xiaomi/OnePlus ABLs. The **Samsung Bengal ABL
rejected it silently.** What it actually wants — discovered by dumping the device's
own `recovery` partition (a boot image the ABL is *proven* to launch) and diffing it
field-by-field against ours:

| Field                  | Stock recovery (works) | Generic edk2-msm | Fix |
|------------------------|------------------------|------------------|-----|
| header version         | **v2**                 | v1               | `BOOTIMG_HEADER_VERSION=2` |
| DTB location           | dedicated **v2 dtb section** | appended to kernel | `mkbootimg --dtb device.dtb` |
| kernel stream          | **clean single gzip**  | gzip **+ trailing raw dtb** | override `platform_build_kernel`: don't append the dtb |
| page size              | `0x1000`               | `0x800`          | `--pagesize 4096` |
| load geometry          | kernel `0x8000`, ramdisk `0x20000000`, tags `0x1e00000` | all `0x10000000` | `--base 0 --kernel_offset 0x8000 …` |
| board name             | `SRPTC24A006`          | empty            | `--board SRPTC24A006` |
| BootShim ARM64 header  | `image_size` non-zero, `text_offset 0x80000`, `flags 0xa` | all **zero** | patch `tools/BootShim/BootShim.S` |

The single most damaging bug: with header v2 the DTB belongs in its **own section**,
so the *appended* dtb the generic hook adds becomes **trailing garbage after the
gzip EOF** — and the Samsung gzip decompressor chokes on it and never launches the
kernel. Quick check on any built image:
```python
import struct,gzip
d=open('boot-gta4l.img','rb').read(); d=d[d.find(b'ANDROID!'):]
ksz=struct.unpack('<I',d[8:12])[0]; page=struct.unpack('<I',d[36:40])[0]
gzip.decompress(d[page:page+ksz])   # must NOT raise "Not a gzipped file"
```

Device build hooks live in `Platform/Samsung/sm6225/gta4l.sh.inc` (sourced by
`build.sh` after the generic `Silicon/platform.sh.inc`, so they override it):
`platform_build_kernel` (clean gzip) and `platform_build_bootimg` (mkbootimg args).

---

## 7. Debugging a blind first boot — the methodology (the actually-reusable part)

You will get a black/splash screen and no idea why. Here is the decision tree we
used. **The golden rule: never change two things at once, and find a signal before
you "fix" anything.**

### 7.1 Characterise the hang without any code of yours running
Watch what the USB enumerates as while it is "stuck":
```powershell
Get-PnpDevice -PresentOnly | ? FriendlyName -match 'Qualcomm|9008|Samsung|CDC'
```
* Qualcomm `9008` (Sahara/EDL) → hard CPU fault / it entered emergency download.
* OEM bootloader USB (here Samsung CDC `04E8:685D`) → soft hang; the OEM BL is
  still alive and **your code has not taken over USB** (it never reached BDS).

### 7.2 "Does my code run AT ALL?" — two independent probes
Memory-map / FD_BASE / geometry changes that produce **zero** behavioural change are
themselves a clue: the crash is *before* those matter, i.e. either the ABL never
launches you, or you die in the first instructions. Prove which with a probe that
needs nothing but a couple of stores. Patch `tools/BootShim/BootShim.S` at `_Start`
(MMU is off here → direct physical writes):

* **Framebuffer paint** — write a colour to the continuous-splash framebuffer
  (its address is in the DTB `cont_splash` reserved-memory node):
  ```asm
  movz x10, #0x5C00, lsl #16      // framebuffer base
  movz x11, #0x07E0 ; movk x11, #0x07E0, lsl #16   // colour
  movz x12, #0x0080, lsl #16      // bytes to fill
  1: str w11, [x10], #4 ; subs x12, x12, #4 ; b.ne 1b
  ```
  Screen changes → your code runs (and the FB address is correct). Caveat: a wrong
  FB address gives a false negative, so also use:

* **PSHOLD reboot** (framebuffer-independent, unambiguous) — write 0 to the PSHOLD
  register (address from `/proc/iomem` "pshold", here `0x0440B000`):
  ```asm
  movz x10, #0xB000 ; movk x10, #0x0440, lsl #16
  str  wzr, [x10]                 // PMIC reset
  ```
  Device enters a **reboot loop** → your code definitely runs → the problem is
  downstream (UEFI/PrePi/framebuffer). Stays frozen → your code never runs →
  the problem is the boot.img format / ABL handoff (go to §6).

This single test cleanly partitions the entire problem space in one boot. It is the
most valuable thing in this document.

### 7.3 Once code runs, get EDK2's own log on screen
Build **DEBUG without `--uart`** → `FrameBufferSerialPortLib` prints the EDK2 boot
log straight onto the panel. No serial hardware required. If you *do* have serial:
the GENI UART base is in the stock kernel cmdline (`earlycon=msm_geni_serial,0x…`)
and on the EUD pins of the USB-C port (`eud_base` in `/proc/iomem`; host tooling at
`github.com/quic/eud`).

### 7.4 Always diff against a known-good image
When you cannot reason it out, **stop guessing and compare**. The device's own
`recovery` (or stock `boot`) partition is an image the ABL provably launches. Dump
it, parse its header, decompress its kernel, read its ARM64 header, and make yours
match field by field (§6 is literally the output of doing this).

---

## 8. Reproducing on a NEW device — the checklist

1. **Identify the SoC** (`ro.board.platform`, `soc_id`). Find it or a platform
   sibling under `Silicon/Qualcomm/`. Cross-check 4-5 register bases (§1).
2. **Pull artifacts** (§2): `device.dtb`, `/proc/iomem`, partition list, panel
   res/density, stock boot header, and `uefiplat.cfg` from xbl.
3. **Copy a device package** from a device on the same SoC (e.g. `Xiaomi/sm6225/spes`)
   → `Platform/<Vendor>/<soc>/<device>.{dsc,fdf.inc}` + `FdtBlob_compat/<device>.dtb`
   + `configs/devices/<device>.conf`. Set framebuffer res, DPI, strings.
4. **Memory map**: reserved regions from `uefiplat.cfg`, DDR banks from the DTB
   `/memory` node, sized to the real RAM. Override `PlatformMemoryMapLib` if the
   sibling assumes a different RAM size.
5. **Build on CI** (§4). Iterate until it compiles.
6. **Match the OEM boot.img format** (§6): dump the device's recovery/boot, diff,
   and write `<device>.sh.inc` hooks until your image is structurally identical
   (header version, dtb section vs appended, clean gzip, page size, geometry,
   board name, BootShim ARM64 header).
7. **Prove the handoff** with the PSHOLD probe (§7.2) before debugging any UEFI.
8. **Get the EDK2 log on screen** (DEBUG, no `--uart`) and work the bring-up:
   memory map → FD_BASE → display → ACPI/DSDT → OS loaders.

---

## 9. Artifact / file index (this port)

| Artifact | What it is |
|----------|------------|
| `Platform/Samsung/sm6225/gta4l.dsc` | device platform description |
| `Platform/Samsung/sm6225/gta4l.fdf.inc` | DTB inclusion |
| `Platform/Samsung/sm6225/gta4l.sh.inc` | boot.img packaging overrides |
| `Platform/Samsung/sm6225/FdtBlob_compat/gta4l.dtb` | device FDT (`/sys/firmware/fdt`) |
| `Platform/Samsung/sm6225/Library/gta4l/PlatformMemoryMapLib/` | 3 GB memory map |
| `configs/devices/gta4l.conf` | SoC selection, header v2, FD_BASE override |
| `.github/workflows/build-gta4l.yml` | CI build |
| `tools/BootShim/BootShim.S` | fake-kernel shim (+ diagnostic probes during bring-up) |
| (off-tree) `device.dtb`, `xbl.img`, `uefiplat-<dev>.cfg`, `recovery.img` | pulled references |

---

## 10. Boot-by-boot log (the real timeline, for honesty)

| # | Change | Result | Lesson |
|---|--------|--------|--------|
| 1 | first build, generic 4 GB map, FD 0xCE000000, v1 | splash | baseline |
| 2 | hand-rolled 3 GB memory map | splash (identical) | map didn't matter → crash earlier |
| 3 | DEBUG, no `--uart` (FB console) | splash, **no text** | dies before FB console; not a DEBUG ASSERT |
| 4 | RELEASE | splash | not an ASSERT |
| 5 | boot.img geometry = stock | splash | ABL parse wasn't it |
| 6 | FD_BASE moved low (0x58000000) | splash | not high-memory mapping |
| 7 | **BootShim paints FB green** | splash, **no green** | our code never runs (FB-addr caveat) |
| 8 | header v2 + `--dtb` section | splash | format closer, still rejected |
| 9 | BootShim ARM64 header fields filled | splash | image_size alone wasn't enough |
| 10 | **clean gzip** (no appended dtb) + board name | splash | now byte-matches recovery, still no run |
| 11 | **BootShim PSHOLD reboot probe** | _in progress_ | definitive run/no-run answer |

This table is the point: a first boot is an **investigation**, not a config edit.
Document every single attempt — the negative results are what tell you where the
wall actually is.

---

---

## 11. Samsung Verified Boot — the AVB chain (the second wall)

On a Samsung Snapdragon device the ABL **cryptographically verifies the boot image
even while the bootloader is unlocked**. Proven decisively: flashing the *genuine*
recovery kernel with **16 bytes changed at its entry** got rejected (stayed on the
splash). So nothing you put in `boot` runs unless it satisfies AVB. What we learned,
all from on-device dumps (`recovery`, `vbmeta` p11, `vbmeta_samsung` p19):

- **Two vbmeta partitions.** `vbmeta` (AOSP) had flags `0x3`
  (HASHTREE_DISABLED|VERIFICATION_DISABLED) — but Samsung's ABL does **not** honour
  the disable flag for `boot`/`recovery`; it still verifies them. `vbmeta_samsung`
  (flags `0x0`) covers `system`/`vendor`/`product` only.
- **The trust key is the AOSP test key.** The `vbmeta` partition and the recovery's
  embedded vbmeta share one AVB public key, sha1
  `2597c218aae470a130f61162feaae70afd97f011` — which is exactly
  `external/avb/test/data/testkey_rsa4096.pem` (unofficial LineageOS signs with it).
  So you *can* re-sign. Confirm by comparing
  `avbtool extract_public_key` sha1 against the pubkey pulled from the device vbmeta.
- **A bare or self-key-signed footer is not enough.** Hash-only (Algorithm NONE) and
  a footer signed with a freshly-generated key were both rejected; only the AOSP test
  key is trusted.
- **`vbmeta` is hardware write-protected.** `dd` from recovery silently no-ops (the
  eMMC rejects it; `ro` is 0 but the write doesn't stick). You must flash it from
  **download mode** (Odin / heimdall / Thor). On Windows: Thor 1.1.0 has no USB
  backend, Heimdall has no easy binary — **Odin** (uses the stock Samsung USB driver)
  was the practical tool. Pack a single image as a `ustar` tar (`vbmeta.img` inside)
  and flash it in the **AP** slot.

The recipe to make a custom `boot` pass AVB:
```bash
# sign boot's own footer with the device's trusted key, sized to the partition
avbtool add_hash_footer --image boot.img --partition_name boot \
  --partition_size <boot_bytes> --rollback_index 1 \
  --algorithm SHA256_RSA4096 --key testkey_rsa4096.pem
# regenerate vbmeta with OUR boot descriptor, same key
avbtool make_vbmeta_image --output vbmeta.img \
  --include_descriptors_from_image boot.img \
  --algorithm SHA256_RSA4096 --key testkey_rsa4096.pem \
  --rollback_index 1 --flags 2 --padding_size <vbmeta_bytes>
# then in download mode: Odin AP <- vbmeta.tar (and boot.tar)
```

## 12. The serial wall — where blind bring-up ends

After satisfying AVB (custom test-key vbmeta flashed via Odin, test-key boot footer)
the device **still sits on the Samsung splash**, including with a DEBUG
`FrameBufferSerialPortLib` build that should paint the EDK2 log onto the panel.

The honest conclusion: **the on-device visual diagnostics cannot resolve this.**
- The PSHOLD reboot probe is inconclusive — if PSHOLD only powers off (PMIC PON
  config) or the write is a no-op, a *running* BootShim that then hangs looks
  identical to one that never ran.
- The framebuffer paint / DEBUG console is inconclusive — if the real scanout isn't
  at `PcdMipiFrameBufferAddress` (0x5C000000) at our execution point, a running UEFI
  paints into nowhere.

So either the ABL still rejects the image for a reason we can't see, or PrePi faults
before the framebuffer console comes up. **Both need a real serial console to go
further** — there is no more signal to extract blind. On this SoC:
- **UART**: `ttyMSM0` @ `0x4a90000` (from the stock kernel cmdline
  `earlycon=msm_geni_serial,0x4a90000`). Build with `./build.sh --uart`.
- **EUD** (Embedded USB Debugger): `eud_base` @ `0x01610000` in `/proc/iomem` —
  exposes that UART over the USB-C port. Host tooling: `github.com/quic/eud`.
- Or solder to the UART test points.

That is the next step for anyone continuing this: get the UART, read PrePi, and the
remaining bring-up becomes sighted instead of blind.

---

*Generated as part of the gta4l (SM6115 Bengal) port. Reproduce freely; the method
generalises to any Qualcomm device edk2-msm can be coaxed onto.*
