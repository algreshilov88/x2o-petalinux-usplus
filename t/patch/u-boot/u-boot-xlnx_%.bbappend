FILESEXTRAPATHS:prepend := "${THISDIR}/files:"

SRC_URI:append = " file://platform-top.h file://bsp.cfg"

UBOOT_EXTRA_CONF += " \
    CONFIG_CMD_DHCP_CLIENT_ID=y \
    CONFIG_CMD_DHCP=y \
    CONFIG_CMD_NFS=y \
    CONFIG_CMD_PING=y \
    CONFIG_BOOTP_DNS=y \
    CONFIG_BOOTP_DNS2=y \
    CONFIG_BOOTP_SEND_HOSTNAME=y \
    CONFIG_BOOTP_SERVER_HOSTNAME='\"atca-ipmc\"' \
    CONFIG_CMD_SF=y \
    CONFIG_SPI_FLASH=y \
    CONFIG_SPI_FLASH_BAR=y \
    CONFIG_SF_DEFAULT_SPEED=1000000 \
    CONFIG_SF_DEFAULT_MODE=0x0 \
    CONFIG_SPI=y \
"

do_configure:append () {
	install ${WORKDIR}/platform-top.h ${S}/include/configs/
}

do_configure:append:microblaze () {
	if [ "${U_BOOT_AUTO_CONFIG}" = "1" ]; then
		install ${WORKDIR}/platform-auto.h ${S}/include/configs/
		install -d ${B}/source/board/xilinx/microblaze-generic/
		install ${WORKDIR}/config.mk ${B}/source/board/xilinx/microblaze-generic/
	fi
}
SRC_URI += "file://user_2025-03-03-13-15-00.cfg \
            file://0001-Set-timeout-on-SD-card-clock-enable.patch \
            file://0001-Added-client-ID-support-QSPI-MAC-addresses-mmc-fixes.patch \
            "

do_compile:append () {
    cp ${WORKDIR}/ipmi-u-boot.c ${S}/cmd/
}
