/*
 * ipmi-u-boot.c
 *
 * U-Boot: X2O IPMC/IPMB MMIO + DHCP Client ID (CMS Phase-2 spec) + MAC assignment
 */

#include <common.h>
#include <command.h>
#include <net.h>
#include <env.h>
#include <asm/io.h>
#include <linux/delay.h>
#include <spi.h>
#include <spi_flash.h>
#include <linux/ctype.h>
#include <linux/string.h>
#include <linux/types.h>
#include <malloc.h>

/* =========================
 * Exact memory mapping constants 
 * ========================= */
#define base_ipmb       0x0B0040000
#define base_slave_0    0x440000000
#define offset_slave_1  0x02000000
#define base_addr       0x00000000
#define set_MS          0x01200000
#define offset_ipmb     0x00000000
#define ha_offset       0x01220008

/* =========================
 * ATCA site/slot mapping & constants 
 * ========================= */
#define SITE_TYPE_ATCA          0x00
#define NUM_ATCA_SLOTS          14
#define SECONDARY_SITE_TYPE     0xC0
#define SECONDARY_SITE_NUMBER   0x01

/* IPMI constants */
#define IPMI_NETFN_PICMG        0x2C
#define IPMI_CMD_GET_SHELF_ADDR 0x05

/* DHCP/Client ID constants */
#define CMS_CID_TOTAL_LEN       46
#define CMS_CID_HD              0x3D
#define CMS_CID_LE              0x2C
#define CMS_CID_TYPE            0xFF
#define CMS_CID_SHELF_TL        0xD4
#define CMS_CID_SHELF_TYPE      0x00

/* QSPI Flash MAC addresses storage */
#define QSPI_MAC_OFFSET         0x100000
#define MAC_ADDRESS_SIZE        6
#define NUM_MAC_ADDRESSES       3

/* Slot map */
struct ipmb_slot_pair {
    unsigned char ipmb_addr;
    unsigned char slot_number;
};

static struct ipmb_slot_pair ipmb_slot_pairs[NUM_ATCA_SLOTS] = {
    { 0x9A,  1 }, { 0x96,  2 }, { 0x92,  3 }, { 0x8E,  4 },
    { 0x8A,  5 }, { 0x86,  6 }, { 0x82,  7 }, { 0x84,  8 },
    { 0x88,  9 }, { 0x8C, 10 }, { 0x90, 11 }, { 0x94, 12 },
    { 0x98, 13 }, { 0x9C, 14 },
};

/* =========================
 * Globals & basic MMIO helpers 
 * ========================= */
static void *devmem_ptr      = NULL;
static void *devmem_ipmb_ptr = NULL;

static inline void reg_write(void *reg_base, unsigned int offset, unsigned int value)
{
    writel(value, (void *)((char *)reg_base + offset));
}

static inline unsigned int reg_read(void *reg_base, unsigned int offset)
{
    return readl((void *)((char *)reg_base + offset));
}

static int dhcp_client_id_init_mem(void)
{
    devmem_ptr      = (void *)base_slave_0;
    devmem_ipmb_ptr = (void *)base_ipmb;

    printf("Memory mapped: slave=0x%llx, ipmb=0x%llx\n",
           (unsigned long long)devmem_ptr,
           (unsigned long long)devmem_ipmb_ptr);
    return 0;
}

/* =========================
 * Local geo info
 * ========================= */
static unsigned char read_local_ipmc_address(void)
{
    unsigned int addr_reg = reg_read(devmem_ptr, ha_offset);
    return (addr_reg & 0x7F) << 1;
}

static unsigned char get_hardware_address(void)
{
    unsigned char ipmc_addr = read_local_ipmc_address();
    return (ipmc_addr >> 1) & 0x7F;
}

static unsigned char read_site_type_primary(void)
{
    return SITE_TYPE_ATCA; 
}

static unsigned char read_slot_number(void)
{
    unsigned char la = read_local_ipmc_address();
    int i;
    for (i = 0; i < NUM_ATCA_SLOTS; i++) {
        if (ipmb_slot_pairs[i].ipmb_addr == la)
            return ipmb_slot_pairs[i].slot_number;
    }
    return 0; 
}

static unsigned char read_site_type_secondary(void)   { return SECONDARY_SITE_TYPE;   }
static unsigned char read_site_number_secondary(void) { return SECONDARY_SITE_NUMBER; }

/* =========================
 * IPMB helpers
 * ========================= */
#define ST_DONE_EVT    (1u << 0)
#define ST_ERROR       (1u << 1)
#define ST_IBUSY       (1u << 2)
#define ST_ARB_LOST    (1u << 3)
#define ST_SCL_HANG    (1u << 4)
#define ST_TIME_EXCESS (1u << 5)
#define ST_SDA_HANG    (1u << 6)
#define ST_CLR_LATCH   (1u << 16)

static inline uint8_t csum8(const uint8_t *p, int n)
{
    uint8_t s = 0; 
    while (n--) 
        s += *p++; 
    return (uint8_t)(-s);
}

static int send_ipmi_channel(int channel, uint8_t *message, int msg_len)
{
    uint32_t reg_val, rdb;
    int i, timeout;
    uint32_t channel_mask = (channel == 0) ? 0x01 : 0x02;
    uint32_t offset = 0x00;

    if (!devmem_ptr || !devmem_ipmb_ptr)
        return -1;

    reg_val = reg_read(devmem_ptr, set_MS);
    reg_val |= channel_mask;
    reg_write(devmem_ptr, set_MS, reg_val);

    reg_val = reg_read(devmem_ipmb_ptr, offset_ipmb);
    reg_val |= ST_CLR_LATCH;
    reg_write(devmem_ipmb_ptr, offset_ipmb, reg_val);

    for (i = 0; i < msg_len; i++) {
        offset += 0x04;
        reg_write(devmem_ipmb_ptr, offset, message[i]);
    }

    reg_val = reg_read(devmem_ipmb_ptr, offset_ipmb);
    reg_val |= ((uint32_t)msg_len << 8);
    reg_write(devmem_ipmb_ptr, offset_ipmb, reg_val);

    timeout = 10000;
    do {
        rdb = reg_read(devmem_ipmb_ptr, offset_ipmb);
        if (rdb & ST_DONE_EVT) break;
        if (rdb & ST_ERROR)     { printf("ERROR on IPMB bus ch%d\n", channel);      return -1; }
        if (rdb & ST_IBUSY)     { printf("IBUSY on IPMB bus ch%d\n", channel);      return -2; }
        if (rdb & ST_ARB_LOST)  { printf("ARBITRATION LOST on IPMB ch%d\n",channel);return -3; }
        if (rdb & ST_SCL_HANG)  { printf("SCL hang on IPMB ch%d\n", channel);       return -4; }
        if (rdb & ST_TIME_EXCESS){printf("TIME limit on IPMB ch%d\n", channel);     return -5; }
        if (rdb & ST_SDA_HANG)  { printf("SDA hang on IPMB ch%d\n", channel);       return -6; }
        udelay(100);
    } while (timeout--);

    if (timeout <= 0) { 
        printf("Timeout on IPMB ch%d\n", channel); 
        return -7; 
    }

    reg_val = reg_read(devmem_ipmb_ptr, offset_ipmb);
    reg_val |= ST_CLR_LATCH;
    reg_write(devmem_ipmb_ptr, offset_ipmb, reg_val);

    reg_val = reg_read(devmem_ptr, set_MS);
    reg_val &= ~channel_mask;
    reg_write(devmem_ptr, set_MS, reg_val);

    return 0;
}

static int check_slave_buffer(int channel, uint8_t *out, int *out_len)
{
    uint32_t pkg_size;
    uint32_t reg_val, reg;
    int i;

    if (!devmem_ptr)
        return -1;

    uint32_t slave_base   = (channel == 0) ? base_addr : offset_slave_1;
    uint32_t channel_mask = (channel == 0) ? 0x01      : 0x02;

    reg_val = reg_read(devmem_ptr, set_MS);
    reg_val &= ~channel_mask;
    reg_write(devmem_ptr, set_MS, reg_val);

    pkg_size = reg_read(devmem_ptr, slave_base);
    if (pkg_size == 0 || pkg_size >= (unsigned int)(*out_len))
        return -1;

    reg = slave_base;
    for (i = 0; i < (int)pkg_size - 1; i++) {
        reg += 0x004;
        out[i] = reg_read(devmem_ptr, reg) & 0xFF;
    }
    *out_len = pkg_size - 1;
    return 0;
}

/* =========================
 * PICMG Get Shelf Address Info
 * ========================= */
static int get_shelf_ascii20(uint8_t shelf_ascii[20])
{
    uint8_t rs_sa7 = 0x20;
    uint8_t rq_sa7 = (read_local_ipmc_address() >> 1);
    uint8_t seq_lun = 0x00;
    uint8_t req[8];
    int q = 0;

    uint8_t csum1_data[] = { (uint8_t)(rs_sa7 << 1), IPMI_NETFN_PICMG };
    
    req[q++] = IPMI_NETFN_PICMG;
    req[q++] = csum8(csum1_data, 2);
    req[q++] = (uint8_t)(rq_sa7 << 1);
    req[q++] = seq_lun;
    req[q++] = IPMI_CMD_GET_SHELF_ADDR;
    req[q++] = csum8(&req[2], q - 2);

    int ret = send_ipmi_channel(0, req, q);
    if (ret != 0) {
        printf("Ch0 send failed (%d), try ch1\n", ret);
        ret = send_ipmi_channel(1, req, q);
        if (ret != 0) 
            return ret;
    }
    mdelay(10);

    uint8_t rsp[64]; 
    int rlen, got = 0, ch;
    
    for (ch = 0; ch <= 1 && !got; ch++) {
        rlen = sizeof(rsp);
        if (check_slave_buffer(ch, rsp, &rlen) == 0) {
            if (rlen < 8) continue;
            
            uint8_t csum1_check[] = { (uint8_t)(rq_sa7 << 1), rsp[0] };
            if (csum8(csum1_check, 2) != rsp[1]) 
                continue;
            if (csum8(&rsp[2], rlen - 3) != rsp[rlen - 1]) 
                continue;
            if (((rsp[0] >> 2) & 0x3F) != (((IPMI_NETFN_PICMG & 0x3F) + 1) & 0x3F)) 
                continue;
            if (rsp[4] != IPMI_CMD_GET_SHELF_ADDR) 
                continue;
            if (rsp[5] != 0x00) 
                continue;

            memset(shelf_ascii, 0, 20);
            int avail = rlen - 2 - 6 - 1;
            if (avail > 0) {
                int ncopy = (avail > 20) ? 20 : avail;
                memcpy(shelf_ascii, &rsp[7], ncopy);
            }
            got = 1;
        }
    }

    if (!got) {
        memset(shelf_ascii, 0, 20);
        memcpy(shelf_ascii, "ATCA-UNKNOWN", 12);
        return -1;
    }
    return 0;
}

/* =========================
 * QSPI MAC address handling 
 * ========================= */
static int set_mac_addresses_to_env(const uint8_t *mac1, const uint8_t *mac2, const uint8_t *mac3)
{
    char buf[3][18];

    snprintf(buf[0], sizeof(buf[0]), "%02x:%02x:%02x:%02x:%02x:%02x",
             mac1[0], mac1[1], mac1[2], mac1[3], mac1[4], mac1[5]);
    snprintf(buf[1], sizeof(buf[1]), "%02x:%02x:%02x:%02x:%02x:%02x",
             mac2[0], mac2[1], mac2[2], mac2[3], mac2[4], mac2[5]);
    snprintf(buf[2], sizeof(buf[2]), "%02x:%02x:%02x:%02x:%02x:%02x",
             mac3[0], mac3[1], mac3[2], mac3[3], mac3[4], mac3[5]);

    env_set("ethaddr",  buf[0]);
    env_set("eth1addr", buf[1]);
    env_set("eth2addr", buf[2]);

    printf("Set MACs: ethaddr=%s eth1addr=%s eth2addr=%s\n", buf[0], buf[1], buf[2]);
    return 0;
}

static int read_mac_addresses_from_qspi(uint8_t *mac1, uint8_t *mac2, uint8_t *mac3)
{
    struct spi_flash *flash;
    uint8_t *mac_buffer;
    int ret;

    printf("Reading MAC addresses from QSPI flash\n");
    
    mac_buffer = malloc(NUM_MAC_ADDRESSES * MAC_ADDRESS_SIZE);
    if (!mac_buffer) {
        printf("Failed to allocate memory for MAC buffer\n");
        return -ENOMEM;
    }

    flash = spi_flash_probe(0, 0, 1000000, SPI_MODE_3);
    if (!flash) {
        printf("QSPI probe failed\n");
        free(mac_buffer);
        return -ENODEV;
    }

    ret = spi_flash_read(flash, QSPI_MAC_OFFSET, NUM_MAC_ADDRESSES * MAC_ADDRESS_SIZE, mac_buffer);
    if (ret) {
        printf("QSPI read failed (%d)\n", ret);
        spi_flash_free(flash);
        free(mac_buffer);
        return ret;
    }

    memcpy(mac1, &mac_buffer[0], MAC_ADDRESS_SIZE);
    memcpy(mac2, &mac_buffer[MAC_ADDRESS_SIZE], MAC_ADDRESS_SIZE);
    memcpy(mac3, &mac_buffer[2 * MAC_ADDRESS_SIZE], MAC_ADDRESS_SIZE);

    spi_flash_free(flash);
    free(mac_buffer);
    return 0;
}

static int init_mac_addresses(void)
{
    uint8_t mac1[MAC_ADDRESS_SIZE], mac2[MAC_ADDRESS_SIZE], mac3[MAC_ADDRESS_SIZE];
    int ret;

    if (env_get("ethaddr") && env_get("eth1addr") && env_get("eth2addr")) {
        printf("MAC addresses already set in environment\n");
        return 0;
    }

    ret = read_mac_addresses_from_qspi(mac1, mac2, mac3);
    if (ret != 0) {
        printf("Warning: Using derived MAC addresses\n");
        uint8_t hw = get_hardware_address();

        mac1[0] = 0x02; mac1[1] = 0x0A; mac1[2] = 0x35; 
        mac1[3] = 0x00; mac1[4] = 0x00; mac1[5] = hw;
        
        mac2[0] = 0x02; mac2[1] = 0x0A; mac2[2] = 0x35; 
        mac2[3] = 0x00; mac2[4] = 0x00; mac2[5] = hw + 1;
        
        mac3[0] = 0x02; mac3[1] = 0x0A; mac3[2] = 0x35; 
        mac3[3] = 0x00; mac3[4] = 0x00; mac3[5] = hw + 2;
    }

    return set_mac_addresses_to_env(mac1, mac2, mac3);
}

/* =========================
 * Build the 46-byte CMS/HPM.3 Client ID
 * ========================= */
static int build_cms_hpm3_clientid(uint8_t out[CMS_CID_TOTAL_LEN])
{
    uint8_t shelf_ascii[20];
    uint8_t pt, pn, st, sn;

    if (dhcp_client_id_init_mem() != 0) {
        printf("ClientID: MMIO init failed\n");
        return -1;
    }

    get_shelf_ascii20(shelf_ascii);

    pt = read_site_type_primary();
    pn = read_slot_number();
    st = read_site_type_secondary();
    sn = read_site_number_secondary();

    int p = 0;
    out[p++] = CMS_CID_HD;
    out[p++] = CMS_CID_LE;
    out[p++] = CMS_CID_TYPE;

    out[p++] = 0x00; out[p++] = 0x00; out[p++] = 0x00; out[p++] = 0x02;

    out[p++] = 0x00; out[p++] = 0x02;

    out[p++] = 0x00; out[p++] = 0x00; out[p++] = 0x31; out[p++] = 0x5A;

    out[p++] = 'H'; out[p++] = 'P'; out[p++] = 'M'; out[p++] = '.'; 
    out[p++] = '3'; out[p++] = '-'; out[p++] = '1';

    out[p++] = CMS_CID_SHELF_TL;

    memcpy(&out[p], shelf_ascii, 20); 
    p += 20;

    out[p++] = CMS_CID_SHELF_TYPE;
    out[p++] = pt;
    out[p++] = pn;
    out[p++] = st;
    out[p++] = sn;

    if (p != CMS_CID_TOTAL_LEN) {
        printf("ClientID size mismatch (%d)\n", p);
        return -1;
    }

    printf("CMS ClientID (46 bytes): ");
    for (int i = 0; i < CMS_CID_TOTAL_LEN; i++) 
        printf("%02x", out[i]);
    printf("\n");
    
    return 0;
}

/* =========================
 * Export DHCP Option 61
 * ========================= */
int dhcp_client_id_append(uint8_t *buffer)
{
    uint8_t cid[CMS_CID_TOTAL_LEN];
    char cid_hex[CMS_CID_TOTAL_LEN * 2 + 1];
    int i;

    if (build_cms_hpm3_clientid(cid) != 0)
        return 0;

    buffer[0] = 61;
    buffer[1] = CMS_CID_TOTAL_LEN;
    memcpy(&buffer[2], cid, CMS_CID_TOTAL_LEN);

    for (i = 0; i < CMS_CID_TOTAL_LEN; i++)
        sprintf(&cid_hex[i * 2], "%02x", cid[i]);
    cid_hex[CMS_CID_TOTAL_LEN * 2] = '\0';

    env_set("dhcp_client_id", cid_hex);
    return CMS_CID_TOTAL_LEN + 2;
}

/* =========================
 * Public init function
 * ========================= */
void dhcp_client_id_init(void)
{
    const char *existing_id = env_get("dhcp_client_id");
    uint8_t dummy[64];

    printf("Initializing MACs and CMS DHCP Client ID...\n");

    init_mac_addresses();

    if (existing_id) {
        printf("DHCP client ID already set: %s\n", existing_id);
        return;
    }
    
    (void)dhcp_client_id_append(dummy);
}

/* =========================
 * U-Boot command
 * ========================= */
static int do_dhcp_client_id(struct cmd_tbl *cmdtp, int flag, int argc, char *const argv[])
{
    uint8_t cid[CMS_CID_TOTAL_LEN];
    char cid_hex[CMS_CID_TOTAL_LEN * 2 + 1];
    int i;

    init_mac_addresses();

    if (build_cms_hpm3_clientid(cid) != 0) {
        printf("Error: Failed to build CMS Client ID\n");
        return CMD_RET_FAILURE;
    }

    for (i = 0; i < CMS_CID_TOTAL_LEN; i++)
        sprintf(&cid_hex[i * 2], "%02x", cid[i]);
    cid_hex[CMS_CID_TOTAL_LEN * 2] = '\0';

    env_set("dhcp_client_id", cid_hex);
    printf("CMS DHCP Client ID: %s\n", cid_hex);
    printf("Environment updated: dhcp_client_id\n");
    
    return CMD_RET_SUCCESS;
}

U_BOOT_CMD(
    dhcp_client_id, 1, 1, do_dhcp_client_id,
    "Build CMS (HPM.3) DHCP client ID and set MAC addresses",
    ""
);