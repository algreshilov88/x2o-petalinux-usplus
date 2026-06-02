do_compile:prepend(){
install -m 0644 ${TOPDIR}/../project-spec/hw-description/psu_init.c ${B}/fsbl-firmware/psu_init.c
}

YAML_COMPILER_FLAGS:append = " -DUHS_MODE_ENABLE"

FILESEXTRAPATHS:prepend := "${THISDIR}/${PN}:"

SRC_URI += "file://0001-Updated-timeouts-for-SD-clock-stabilization.patch \
	    file://0001-zynqmp-fsbl-start-fpd-watchdog0-petalinux-2022.2.patch \
	    "

