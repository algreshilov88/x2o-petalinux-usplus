do_compile:prepend(){
install -m 0644 ${TOPDIR}/../project-spec/hw-description/psu_init.c ${B}/fsbl-firmware/psu_init.c
}
