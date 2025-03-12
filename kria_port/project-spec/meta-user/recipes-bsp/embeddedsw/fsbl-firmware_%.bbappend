do_compile:prepend(){
install -m 0644 ${TOPDIR}/../project-spec/hw-description/psu_init.c ${B}/fsbl-firmware/psu_init.c
}

YAML_COMPILER_FLAGS:append = " -DUHS_MODE_ENABLE"
