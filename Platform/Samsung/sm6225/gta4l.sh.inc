# Device-specific boot.img packaging for Samsung Galaxy Tab A7 (SM-T500 / gta4l).
#
# Comparison against the device's own recovery partition (which the Bengal ABL
# launches successfully) showed two things our image got wrong:
#   1. The generic platform_build_kernel APPENDS the dtb after the gzip stream
#      (cat uefi.img.gz device.dtb). That made sense for the old v1 appended-dtb
#      scheme, but with header v2 the dtb lives in its own section, so the
#      trailing raw dtb is just garbage after the gzip EOF - and the Samsung
#      ABL's gzip decompressor chokes on it, so it never launches the kernel.
#      The recovery kernel is a CLEAN single gzip stream. We match that.
#   2. The board name was empty; recovery uses "SRPTC24A006".
# Plus the stock load geometry (kernel 0x8000 / ramdisk 0x20000000 /
# tags 0x1e00000 / dtb 0x1f00000 / page 4096), already matching recovery.
#
# Both hooks are sourced after the generic Silicon/platform.sh.inc and override it.

# Clean gzip kernel - NO appended dtb (the dtb goes in the v2 section instead).
function platform_build_kernel(){
	cat \
		"${ROOTDIR}/tools/BootShim/BootShim.bin" \
		"${WORKSPACE}/Build/${DEVICE}/${_MODE}_${TOOLCHAIN}/FV/${SOC_PLATFORM}_UEFI.fd" \
		> "${WORKSPACE}/Build/${DEVICE}/${_MODE}_${TOOLCHAIN}/FV/${SOC_PLATFORM}_UEFI.fd-bootshim" \
		||return "$?"
	gzip -c \
		< "${WORKSPACE}/Build/${DEVICE}/${_MODE}_${TOOLCHAIN}/FV/${SOC_PLATFORM}_UEFI.fd-bootshim" \
		> "${WORKSPACE}/uefi-${DEVICE}-kernel" \
		||return "$?"
}

function platform_build_bootimg(){
		python3 "${ROOTDIR}/tools/mkbootimg.py" \
		--kernel "${WORKSPACE}/uefi-${DEVICE}-kernel" \
		--ramdisk ramdisk \
		--dtb "${ROOTDIR}/Platform/Samsung/sm6225/FdtBlob_compat/gta4l.dtb" \
		--board "SRPTC24A006" \
		--base 0x00000000 \
		--kernel_offset 0x00008000 \
		--ramdisk_offset 0x20000000 \
		--tags_offset 0x01e00000 \
		--dtb_offset 0x01f00000 \
		--pagesize 4096 \
		--os_version "${BOOTIMG_OS_VERSION}" \
		--os_patch_level "${BOOTIMG_OS_PATCH_LEVEL}" \
		--header_version "${BOOTIMG_HEADER_VERSION}" \
		-o "${OUTDIR}/boot-${DEVICE}${EXT}.img" \
		||return "$?"
}
