# Device-specific boot.img packaging for Samsung Galaxy Tab A7 (SM-T500 / gta4l).
#
# The generic hook (Silicon/platform.sh.inc) packs every load address at
# base 0x10000000 with page_size 2048. The SM-T500 (Bengal) ABL is proven to
# load the stock kernel, which uses a very different geometry:
#   kernel_addr 0x00008000  ramdisk_addr 0x20000000  tags_addr 0x01e00000
#   page_size 4096
# Matching that geometry exactly maximises the chance the Samsung ABL parses,
# loads and launches our UEFI payload identically to the stock kernel.
#
# This overrides platform_build_bootimg (sourced after the generic one).
function platform_build_bootimg(){
		python3 "${ROOTDIR}/tools/mkbootimg.py" \
		--kernel "${WORKSPACE}/uefi-${DEVICE}-kernel" \
		--ramdisk ramdisk \
		--base 0x00000000 \
		--kernel_offset 0x00008000 \
		--ramdisk_offset 0x20000000 \
		--tags_offset 0x01e00000 \
		--pagesize 4096 \
		--os_version "${BOOTIMG_OS_VERSION}" \
		--os_patch_level "${BOOTIMG_OS_PATCH_LEVEL}" \
		--header_version "${BOOTIMG_HEADER_VERSION}" \
		-o "${OUTDIR}/boot-${DEVICE}${EXT}.img" \
		||return "$?"
}
