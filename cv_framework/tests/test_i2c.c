/*
 * test_i2c.c - group_i2c: I2C bus controller transfer tests
 *
 * Mock IP: register-based I2C controller simulated in software.
 * Each TEST_CASE receives a `i2c_transfer_param_t *` via the `data` parameter,
 * set up through constructor(104) calling cv_case_set_data().
 */
#include "cv_macros.h"
#include <stdio.h>
#include <stdint.h>
#include <string.h>

/* ---- Mock I2C controller registers ---- */

#define I2C_STATUS_IDLE      0x00
#define I2C_STATUS_START     0x01
#define I2C_STATUS_ADDR_ACK  0x02
#define I2C_STATUS_WR_ACK    0x04
#define I2C_STATUS_RD_ACK    0x08
#define I2C_STATUS_STOP      0x10
#define I2C_STATUS_ERROR     0x80

typedef struct {
    uint8_t slave_addr;   /* target slave address          */
    uint8_t addr_reg;     /* address register (written)    */
    uint8_t data_reg;     /* data register (r/w)           */
    uint8_t status;       /* status flags                  */
    uint8_t buf[256];     /* internal slave buffer         */
    int     buf_len;      /* valid bytes in slave buffer   */
} i2c_bus_t;

static i2c_bus_t g_i2c;

/* Simulate I2C write-byte transfer */
static int i2c_write_byte(i2c_bus_t *bus, uint8_t addr, uint8_t data)
{
    bus->status = I2C_STATUS_IDLE;
    if (addr == 0x00) {
        bus->status = I2C_STATUS_ERROR;
        return -1;
    }
    bus->addr_reg = addr;
    bus->data_reg = data;
    bus->status = I2C_STATUS_START | I2C_STATUS_ADDR_ACK | I2C_STATUS_WR_ACK | I2C_STATUS_STOP;
    /* Store into slave buffer */
    if (bus->buf_len < (int)sizeof(bus->buf)) {
        bus->buf[bus->buf_len++] = data;
    }
    return 0;
}

/* Simulate I2C read-byte transfer */
static int i2c_read_byte(i2c_bus_t *bus, uint8_t addr, uint8_t *out)
{
    bus->status = I2C_STATUS_IDLE;
    if (addr == 0x00 || !out) {
        bus->status = I2C_STATUS_ERROR;
        return -1;
    }
    bus->addr_reg = addr;
    bus->status = I2C_STATUS_START | I2C_STATUS_ADDR_ACK | I2C_STATUS_RD_ACK | I2C_STATUS_STOP;
    /* Return last byte from slave buffer (or default 0xFF) */
    *out = bus->buf_len > 0 ? bus->buf[bus->buf_len - 1] : 0xFF;
    return 0;
}

/* Simulate I2C multi-byte write */
static int i2c_multi_write(i2c_bus_t *bus, uint8_t addr, const uint8_t *buf, int len)
{
    bus->status = I2C_STATUS_IDLE;
    if (addr == 0x00 || !buf || len <= 0) {
        bus->status = I2C_STATUS_ERROR;
        return -1;
    }
    bus->addr_reg = addr;
    bus->status = I2C_STATUS_START | I2C_STATUS_ADDR_ACK | I2C_STATUS_WR_ACK | I2C_STATUS_STOP;
    for (int i = 0; i < len && bus->buf_len < (int)sizeof(bus->buf); i++) {
        bus->buf[bus->buf_len++] = buf[i];
    }
    bus->data_reg = buf[len - 1];
    return 0;
}

/* ---- Test parameter struct (passed via void *data) ---- */

typedef struct {
    uint8_t slave_addr;
    uint8_t data;
    uint8_t wr_buf[16];
    int     wr_len;
} i2c_transfer_param_t;

/* ---- Group / Module registration ---- */

TEST_GROUP(group_i2c);

static void i2c_group_pre(void *data)
{
    (void)data;
    printf("  [GROUP PRE_TEST] group_i2c: init I2C bus controller...\n");
    memset(&g_i2c, 0, sizeof(g_i2c));
}

static void i2c_group_post(void *data)
{
    (void)data;
    printf("  [GROUP POST_TEST] group_i2c: reset I2C bus controller...\n");
    memset(&g_i2c, 0, sizeof(g_i2c));
}

GROUP_PRE_TEST(group_i2c, i2c_group_pre);
GROUP_POST_TEST(group_i2c, i2c_group_post);

TEST_MODULE(group_i2c, module_i2c_transfer);

static void i2c_transfer_pre(void *data)
{
    (void)data;
    memset(&g_i2c, 0, sizeof(g_i2c));
}

static void i2c_transfer_post(void *data)
{
    (void)data;
    memset(&g_i2c, 0, sizeof(g_i2c));
}

MODULE_PRE_TEST(module_i2c_transfer, i2c_transfer_pre);
MODULE_POST_TEST(module_i2c_transfer, i2c_transfer_post);

/* ---- Test cases ---- */

TEST_CASE(module_i2c_transfer, test_i2c_write_byte)
{
    i2c_transfer_param_t *p = (i2c_transfer_param_t *)data;
    int ret = i2c_write_byte(&g_i2c, p->slave_addr, p->data);

    CV_ASSERT_EQ(ret, 0);
    CV_ASSERT_EQ(g_i2c.addr_reg, (long)p->slave_addr);
    CV_ASSERT_EQ(g_i2c.data_reg, (long)p->data);
    CV_ASSERT(g_i2c.status & I2C_STATUS_WR_ACK);
}

TEST_CASE(module_i2c_transfer, test_i2c_read_byte)
{
    i2c_transfer_param_t *p = (i2c_transfer_param_t *)data;
    /* Pre-load a byte via write */
    i2c_write_byte(&g_i2c, p->slave_addr, p->data);

    uint8_t rd = 0;
    int ret = i2c_read_byte(&g_i2c, p->slave_addr, &rd);

    CV_ASSERT_EQ(ret, 0);
    CV_ASSERT_EQ((long)rd, (long)p->data);
    CV_ASSERT(g_i2c.status & I2C_STATUS_RD_ACK);
}

TEST_CASE(module_i2c_transfer, test_i2c_multi_byte_transfer)
{
    i2c_transfer_param_t *p = (i2c_transfer_param_t *)data;
    g_i2c.buf_len = 0;  /* reset to isolate multi-byte test */
    int ret = i2c_multi_write(&g_i2c, p->slave_addr, p->wr_buf, p->wr_len);

    CV_ASSERT_EQ(ret, 0);
    CV_ASSERT_EQ(g_i2c.buf_len, p->wr_len);
    CV_ASSERT_EQ((long)g_i2c.data_reg, (long)p->wr_buf[p->wr_len - 1]);
}

TEST_CASE(module_i2c_transfer, test_i2c_invalid_addr)
{
    i2c_transfer_param_t *p = (i2c_transfer_param_t *)data;
    int ret = i2c_write_byte(&g_i2c, p->slave_addr, p->data);

    CV_ASSERT_EQ(ret, -1);
    CV_ASSERT(g_i2c.status & I2C_STATUS_ERROR);
}

/* ---- Parameter data and cv_case_set_data (constructor 104) ---- */

static i2c_transfer_param_t param_write = { .slave_addr = 0x50, .data = 0xAB };
static i2c_transfer_param_t param_read  = { .slave_addr = 0x50, .data = 0xCD };
static i2c_transfer_param_t param_multi = {
    .slave_addr = 0x50,
    .wr_buf = { 0x01, 0x02, 0x03, 0x04 },
    .wr_len = 4,
};
static i2c_transfer_param_t param_bad_addr = { .slave_addr = 0x00, .data = 0xFF };

__attribute__((constructor(104)))
static void __cv_setdata_i2c_write(void) {
    cv_case_set_data(test_i2c_write_byte_cvcase, &param_write);
}

__attribute__((constructor(104)))
static void __cv_setdata_i2c_read(void) {
    cv_case_set_data(test_i2c_read_byte_cvcase, &param_read);
}

__attribute__((constructor(104)))
static void __cv_setdata_i2c_multi(void) {
    cv_case_set_data(test_i2c_multi_byte_transfer_cvcase, &param_multi);
}

__attribute__((constructor(104)))
static void __cv_setdata_i2c_bad_addr(void) {
    cv_case_set_data(test_i2c_invalid_addr_cvcase, &param_bad_addr);
}
