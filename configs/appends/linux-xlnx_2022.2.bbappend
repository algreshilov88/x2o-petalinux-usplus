FILESEXTRAPATHS:prepend := "${THISDIR}/${PN}:"
FILESPATH:prepend := "/home/greshilov/projects/kria_10g_07.02.2023/kria_port/components/yocto/workspace/sources/linux-xlnx/oe-local-files:"
# srctreebase: /home/greshilov/projects/kria_10g_07.02.2023/kria_port/components/yocto/workspace/sources/linux-xlnx

inherit externalsrc
# NOTE: We use pn- overrides here to avoid affecting multiple variants in the case where the recipe uses BBCLASSEXTEND
EXTERNALSRC:pn-linux-xlnx = "/home/greshilov/projects/kria_10g_07.02.2023/kria_port/components/yocto/workspace/sources/linux-xlnx"
SRCTREECOVEREDTASKS = "do_validate_branches do_kernel_checkout do_fetch do_unpack do_kernel_configcheck"

do_patch[noexec] = "1"

do_configure:append() {
    cp ${B}/.config ${S}/.config.baseline
    ln -sfT ${B}/.config ${S}/.config.new
}

do_kernel_configme:prepend() {
    if [ -e ${S}/.config ]; then
        mv ${S}/.config ${S}/.config.old
    fi
}

do_configure:append() {
    if [ ! ${DEVTOOL_DISABLE_MENUCONFIG} ]; then
        cp ${B}/.config ${S}/.config.baseline
        ln -sfT ${B}/.config ${S}/.config.new
    fi
}

# initial_rev: a8ae1351bbccf78edc78a9312589395eaa42458d
