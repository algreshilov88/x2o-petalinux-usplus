FILESEXTRAPATHS:prepend := "${THISDIR}/${PN}:"

SRC_URI:append = " file://bsp.cfg"
KERNEL_FEATURES:append = " bsp.cfg"
SRC_URI += "file://user_2022-07-18-17-25-00.cfg \
            file://user_2022-12-13-20-36-00.cfg \
            file://user_2022-12-14-21-57-00.cfg \
            file://0001-i2c-cdns-timeout-set-to-3ms-pm-timeout-set-to-15ms.patch \
            file://0001-Add-RXCsum-support-Add-Txcsum-offload-support-for-xx.patch \
            file://0001-User-patch-for-VSC8562-to-enable-auto-negotiation.patch \
            file://0001-arm64-fix-kernel-panic-on-serror-exception-caused-by.patch \
            file://user_2023-10-17-14-32-00.cfg \
            "
