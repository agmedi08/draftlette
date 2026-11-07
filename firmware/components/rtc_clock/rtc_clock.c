/*
 * PCF85063 real-time clock: restore and save the system clock.
 *
 * Register map (NXP PCF85063A datasheet, sec. 8):
 *   0x04 Seconds  bit 7 = OS (oscillator stopped, time not valid)
 *   0x05 Minutes
 *   0x06 Hours    24-hour mode (Control_1 12_24 = 0, the reset value)
 *   0x07 Days
 *   0x08 Weekdays 0 = Sunday
 *   0x09 Months   1-12
 *   0x0A Years    0-99, taken as 2000-2099
 * All BCD. Writing the Seconds register clears OS.
 */

#include "rtc_clock.h"

#include <string.h>
#include <sys/time.h>

#include "driver/i2c_master.h"
#include "esp_log.h"

static const char *TAG = "rtc_clock";

#define REG_SECONDS 0x04
#define OS_BIT      0x80

static bool    s_ready;
static int     s_sda;
static int     s_scl;
static uint8_t s_addr;

static uint8_t bcd2bin(uint8_t v) { return (uint8_t)((v >> 4) * 10 + (v & 0x0F)); }
static uint8_t bin2bcd(uint8_t v) { return (uint8_t)(((v / 10) << 4) | (v % 10)); }

/* Open the bus and device for one transfer; *bus / *dev are NULL on error. */
static esp_err_t open_dev(i2c_master_bus_handle_t *bus, i2c_master_dev_handle_t *dev)
{
    *bus = NULL;
    *dev = NULL;

    i2c_master_bus_config_t bus_cfg = {};
    bus_cfg.i2c_port          = -1;
    bus_cfg.sda_io_num        = (gpio_num_t)s_sda;
    bus_cfg.scl_io_num        = (gpio_num_t)s_scl;
    bus_cfg.clk_source        = I2C_CLK_SRC_DEFAULT;
    bus_cfg.glitch_ignore_cnt = 7;
    bus_cfg.flags.enable_internal_pullup = true;
    esp_err_t err = i2c_new_master_bus(&bus_cfg, bus);
    if (err != ESP_OK) return err;

    i2c_device_config_t dev_cfg = {};
    dev_cfg.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    dev_cfg.device_address  = s_addr;
    dev_cfg.scl_speed_hz    = 100000;
    err = i2c_master_bus_add_device(*bus, &dev_cfg, dev);
    if (err != ESP_OK) {
        i2c_del_master_bus(*bus);
        *bus = NULL;
    }
    return err;
}

static void close_dev(i2c_master_bus_handle_t bus, i2c_master_dev_handle_t dev)
{
    if (dev) i2c_master_bus_rm_device(dev);
    if (bus) i2c_del_master_bus(bus);
}

esp_err_t rtc_clock_init(int sda_pin, int scl_pin, uint8_t addr)
{
    s_sda   = sda_pin;
    s_scl   = scl_pin;
    s_addr  = addr;
    s_ready = true;
    return ESP_OK;
}

bool rtc_clock_available(void)
{
    return s_ready;
}

esp_err_t rtc_clock_restore_system_time(void)
{
    if (!s_ready) return ESP_ERR_INVALID_STATE;

    i2c_master_bus_handle_t bus;
    i2c_master_dev_handle_t dev;
    esp_err_t err = open_dev(&bus, &dev);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "I2C open failed: %s", esp_err_to_name(err));
        return err;
    }

    uint8_t reg = REG_SECONDS;
    uint8_t r[7] = {0};
    err = i2c_master_transmit_receive(dev, &reg, 1, r, sizeof(r), 50);
    close_dev(bus, dev);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "read failed: %s", esp_err_to_name(err));
        return err;
    }
    if (r[0] & OS_BIT) {
        ESP_LOGW(TAG, "oscillator stopped; RTC time not valid");
        return ESP_ERR_INVALID_RESPONSE;
    }

    struct tm tm = {};
    tm.tm_sec  = bcd2bin(r[0] & 0x7F);
    tm.tm_min  = bcd2bin(r[1] & 0x7F);
    tm.tm_hour = bcd2bin(r[2] & 0x3F);
    tm.tm_mday = bcd2bin(r[3] & 0x3F);
    tm.tm_mon  = bcd2bin(r[5] & 0x1F) - 1;
    tm.tm_year = bcd2bin(r[6]) + 100;

    /* The chip keeps UTC; timegm-equivalent without depending on TZ. */
    time_t days = 0;
    int year = tm.tm_year + 1900;
    for (int y = 1970; y < year; y++) {
        days += (y % 4 == 0 && (y % 100 != 0 || y % 400 == 0)) ? 366 : 365;
    }
    static const int mdays[] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
    for (int m = 0; m < tm.tm_mon; m++) {
        days += mdays[m];
        if (m == 1 && (year % 4 == 0 && (year % 100 != 0 || year % 400 == 0))) days++;
    }
    days += tm.tm_mday - 1;
    time_t utc = days * 86400 + tm.tm_hour * 3600 + tm.tm_min * 60 + tm.tm_sec;

    struct timeval tv = { .tv_sec = utc, .tv_usec = 0 };
    settimeofday(&tv, NULL);
    ESP_LOGI(TAG, "system time set from RTC: %04d-%02d-%02d %02d:%02d:%02d UTC",
             year, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec);
    return ESP_OK;
}

esp_err_t rtc_clock_save_system_time(void)
{
    if (!s_ready) return ESP_ERR_INVALID_STATE;

    time_t now = time(NULL);
    struct tm tm;
    gmtime_r(&now, &tm);
    if (tm.tm_year < 100 || tm.tm_year > 199) {
        /* Outside 2000-2099: the chip cannot hold it (and the system
         * clock is almost certainly unset). */
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t w[8];
    w[0] = REG_SECONDS;
    w[1] = bin2bcd((uint8_t)tm.tm_sec);   /* also clears OS */
    w[2] = bin2bcd((uint8_t)tm.tm_min);
    w[3] = bin2bcd((uint8_t)tm.tm_hour);
    w[4] = bin2bcd((uint8_t)tm.tm_mday);
    w[5] = (uint8_t)tm.tm_wday;
    w[6] = bin2bcd((uint8_t)(tm.tm_mon + 1));
    w[7] = bin2bcd((uint8_t)(tm.tm_year - 100));

    i2c_master_bus_handle_t bus;
    i2c_master_dev_handle_t dev;
    esp_err_t err = open_dev(&bus, &dev);
    if (err != ESP_OK) return err;
    err = i2c_master_transmit(dev, w, sizeof(w), 50);
    close_dev(bus, dev);
    ESP_LOGI(TAG, "save to RTC: %s", esp_err_to_name(err));
    return err;
}

esp_err_t rtc_clock_set_time(time_t utc)
{
    struct timeval tv = { .tv_sec = utc, .tv_usec = 0 };
    settimeofday(&tv, NULL);
    return s_ready ? rtc_clock_save_system_time() : ESP_OK;
}
