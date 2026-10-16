/*
 * Serial transport for the Kermit server -- see kermit_port.h.
 *
 * Every build has its primary console on a UART (UART0 through the
 * board's USB-UART bridge, where it has one). The server listens on
 * that UART and, on builds with TinyUSB CDC enabled (every board with
 * "Sync via browser"), on a TinyUSB CDC-ACM serial port on the
 * ESP32-S3's USB-OTG controller, and answers on whichever one the
 * client is talking on.
 *
 * Why not the USB-Serial/JTAG port that carries the console on these
 * boards: its CDC-ACM interface resets the chip in hardware when the
 * host drives RTS = 1 with DTR = 0 (esptool's reset pattern), and a
 * browser opening the port through Web Serial does that -- the device
 * reset (rst:0x15 USB_UART_CHIP_RESET) the moment Draftbridge
 * connected. The ESP32-S3 cannot switch that reset off
 * (RTC_CNTL_USB_RESET_DISABLE did not help; only the C6 and later have
 * a working disable bit), but USB-OTG has no such logic. So while the
 * server runs, the internal USB PHY is handed to USB-OTG (the computer
 * sees the console port go away and a new serial port appear), and
 * kport_close() hands it back to USB-Serial/JTAG, the same way
 * components/usb_msc does for "SD card via USB".
 */

#include <stdarg.h>
#include <stdbool.h>
#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_rom_sys.h"
#include "esp_rom_serial_output.h"
#include "kermit_port.h"

#if defined(CONFIG_ESP_CONSOLE_UART)
#include "driver/uart.h"
#define KPORT_HAS_UART 1
#endif
#if defined(CONFIG_TINYUSB_CDC_ENABLED)
#include "tinyusb.h"
#include "tinyusb_default_config.h"
#include "tinyusb_cdc_acm.h"
#include "hal/usb_serial_jtag_hal.h"
#define KPORT_HAS_USB 1
#endif

static const char *TAG = "kermit";

enum { PORT_UART = 0, PORT_USB = 1, PORT_COUNT = 2 };

#define KPORT_RX_BUF 4096
#define KPORT_TX_BUF 4096

static bool          s_have[PORT_COUNT];
static int           s_active = -1;      /* port of the last input */
static vprintf_like_t s_prev_vprintf;

#if defined(KPORT_HAS_USB)
/* USB descriptors of the Kermit serial port: one CDC-ACM interface
 * (with its IAD, hence the MISC device class), Espressif VID and PID
 * 0x4001 (esp_tinyusb's per-class PID map: CDC only; "SD card via
 * USB" uses 0x4002). A distinct PID keeps a computer from applying the
 * driver it chose for the mass-storage device. */
enum { USB_ITF_CDC = 0, USB_ITF_CDC_DATA, USB_ITF_TOTAL };
enum { USB_STRID_LANGID = 0, USB_STRID_MANUFACTURER, USB_STRID_PRODUCT,
       USB_STRID_SERIAL, USB_STRID_CDC, USB_STRID_COUNT };
#define USB_EP_CDC_NOTIF 1
#define USB_EP_CDC_DATA  2
#define USB_CONFIG_TOTAL_LEN (TUD_CONFIG_DESC_LEN + TUD_CDC_DESC_LEN)

static const tusb_desc_device_t s_usb_device_desc = {
    .bLength            = sizeof(tusb_desc_device_t),
    .bDescriptorType    = TUSB_DESC_DEVICE,
    .bcdUSB             = 0x0200,
    .bDeviceClass       = TUSB_CLASS_MISC,
    .bDeviceSubClass    = MISC_SUBCLASS_COMMON,
    .bDeviceProtocol    = MISC_PROTOCOL_IAD,
    .bMaxPacketSize0    = CFG_TUD_ENDPOINT0_SIZE,
    .idVendor           = TINYUSB_ESPRESSIF_VID,
    .idProduct          = 0x4001,
    .bcdDevice          = CONFIG_TINYUSB_DESC_BCD_DEVICE,
    .iManufacturer      = USB_STRID_MANUFACTURER,
    .iProduct           = USB_STRID_PRODUCT,
    .iSerialNumber      = USB_STRID_SERIAL,
    .bNumConfigurations = 1,
};

static const uint8_t s_usb_config_desc[] = {
    TUD_CONFIG_DESCRIPTOR(1, USB_ITF_TOTAL, 0, USB_CONFIG_TOTAL_LEN,
                          TUSB_DESC_CONFIG_ATT_REMOTE_WAKEUP, 100),
    TUD_CDC_DESCRIPTOR(USB_ITF_CDC, USB_STRID_CDC, 0x80 | USB_EP_CDC_NOTIF, 8,
                       USB_EP_CDC_DATA, 0x80 | USB_EP_CDC_DATA, 64),
};

static const char s_usb_langid[] = { 0x09, 0x04 };   /* English (0x0409) */
static const char *s_usb_strings[USB_STRID_COUNT] = {
    s_usb_langid,
    CONFIG_TINYUSB_DESC_MANUFACTURER_STRING,
    "Draftling",
    CONFIG_TINYUSB_DESC_SERIAL_STRING,
    "Draftling Kermit server",
};

/* Given by the TinyUSB task when data arrives; port_read() waits on it. */
static SemaphoreHandle_t s_usb_rx_sem;

static void usb_rx_cb(int itf, cdcacm_event_t *event)
{
    (void)itf;
    (void)event;
    if (s_usb_rx_sem) xSemaphoreGive(s_usb_rx_sem);
}

static esp_err_t usb_open(void)
{
    if (!s_usb_rx_sem) s_usb_rx_sem = xSemaphoreCreateBinary();
    if (!s_usb_rx_sem) return ESP_ERR_NO_MEM;
    xSemaphoreTake(s_usb_rx_sem, 0);

    tinyusb_config_t tcfg = TINYUSB_DEFAULT_CONFIG();
    tcfg.descriptor.device            = &s_usb_device_desc;
    tcfg.descriptor.full_speed_config = s_usb_config_desc;
    tcfg.descriptor.string            = s_usb_strings;
    tcfg.descriptor.string_count      = USB_STRID_COUNT;
    esp_err_t err = tinyusb_driver_install(&tcfg);
    if (err != ESP_OK) {
        usb_serial_jtag_hal_phy_set_external(NULL, false);
        return err;
    }
    const tinyusb_config_cdcacm_t acm = {
        .cdc_port = TINYUSB_CDC_ACM_0,
        .callback_rx = usb_rx_cb,
    };
    err = tinyusb_cdcacm_init(&acm);
    if (err != ESP_OK) {
        tinyusb_driver_uninstall();
        usb_serial_jtag_hal_phy_set_external(NULL, false);
    }
    return err;
}

/* Tear TinyUSB down and hand the internal PHY back to USB-Serial/JTAG
 * -- see teardown_locked() in components/usb_msc/usb_msc.cpp. */
static void usb_close(void)
{
    tinyusb_cdcacm_write_flush(TINYUSB_CDC_ACM_0, pdMS_TO_TICKS(200));
    tinyusb_cdcacm_deinit(TINYUSB_CDC_ACM_0);
    tinyusb_driver_uninstall();
    usb_serial_jtag_hal_phy_set_external(NULL, false);
}
#endif /* KPORT_HAS_USB */

static int null_vprintf(const char *fmt, va_list ap)
{
    (void)fmt;
    (void)ap;
    return 0;
}

static void null_putc(char c)
{
    (void)c;
}

static void console_silence(void)
{
    s_prev_vprintf = esp_log_set_vprintf(null_vprintf);
    esp_rom_install_channel_putc(1, null_putc);
    esp_rom_install_channel_putc(2, null_putc);
}

static void console_restore(void)
{
    /* Same ROM printf set-up as esp_system's start-up code. */
    esp_rom_install_uart_printf();
    esp_rom_output_set_as_console(CONFIG_ESP_CONSOLE_ROM_SERIAL_PORT_NUM);
    if (s_prev_vprintf) esp_log_set_vprintf(s_prev_vprintf);
    s_prev_vprintf = NULL;
}

esp_err_t kport_open(void)
{
    s_active = -1;
    s_have[PORT_UART] = false;
    s_have[PORT_USB] = false;

    ESP_LOGI(TAG, "Starting Kermit server; console output is off until "
                  "it is stopped");
    /* Let the line above drain before the port changes hands. */
    vTaskDelay(pdMS_TO_TICKS(50));
    console_silence();

    esp_err_t err = ESP_OK;
#if defined(KPORT_HAS_UART)
    err = uart_driver_install(CONFIG_ESP_CONSOLE_UART_NUM, KPORT_RX_BUF,
                              KPORT_TX_BUF, 0, NULL, 0);
    if (err == ESP_OK) {
        s_have[PORT_UART] = true;
        uart_flush_input(CONFIG_ESP_CONSOLE_UART_NUM);
    }
#endif
#if defined(KPORT_HAS_USB)
    esp_err_t uerr = usb_open();
    if (uerr == ESP_OK) s_have[PORT_USB] = true;
    else if (err == ESP_OK && !s_have[PORT_UART]) err = uerr;
#endif

    if (!s_have[PORT_UART] && !s_have[PORT_USB]) {
        kport_close();
        ESP_LOGE(TAG, "No console port for the Kermit server: %s",
                 esp_err_to_name(err == ESP_OK ? ESP_ERR_NOT_SUPPORTED : err));
        return err == ESP_OK ? ESP_ERR_NOT_SUPPORTED : err;
    }
    return ESP_OK;
}

void kport_close(void)
{
#if defined(KPORT_HAS_UART)
    if (s_have[PORT_UART]) {
        uart_wait_tx_done(CONFIG_ESP_CONSOLE_UART_NUM, pdMS_TO_TICKS(200));
        uart_driver_delete(CONFIG_ESP_CONSOLE_UART_NUM);
    }
#endif
#if defined(KPORT_HAS_USB)
    if (s_have[PORT_USB]) usb_close();
#endif
    s_have[PORT_UART] = false;
    s_have[PORT_USB] = false;
    s_active = -1;
    console_restore();
    ESP_LOGI(TAG, "Kermit server stopped; console output restored");
}

static int port_read(int port, uint8_t *buf, size_t max, TickType_t ticks)
{
    if (!s_have[port]) return 0;
    int n = 0;
    switch (port) {
#if defined(KPORT_HAS_UART)
    case PORT_UART:
        n = uart_read_bytes(CONFIG_ESP_CONSOLE_UART_NUM, buf, max, ticks);
        break;
#endif
#if defined(KPORT_HAS_USB)
    case PORT_USB: {
        size_t got = 0;
        if (tinyusb_cdcacm_read(TINYUSB_CDC_ACM_0, buf, max, &got) == ESP_OK &&
            got == 0 && ticks > 0 &&
            xSemaphoreTake(s_usb_rx_sem, ticks) == pdTRUE)
            tinyusb_cdcacm_read(TINYUSB_CDC_ACM_0, buf, max, &got);
        n = (int)got;
        break;
    }
#endif
    default:
        break;
    }
    return n > 0 ? n : 0;
}

int kport_read(uint8_t *buf, size_t max, uint32_t timeout_ms)
{
    TickType_t start = xTaskGetTickCount();
    TickType_t limit = pdMS_TO_TICKS(timeout_ms);
    for (;;) {
        /* Poll every port without waiting, the active one first. */
        for (int i = 0; i < PORT_COUNT; i++) {
            int p = (s_active >= 0) ? (s_active + i) % PORT_COUNT : i;
            int n = port_read(p, buf, max, 0);
            if (n > 0) {
                s_active = p;
                return n;
            }
        }
        TickType_t elapsed = xTaskGetTickCount() - start;
        if (elapsed >= limit) return 0;
        /* Then block briefly on one of them (the active port, or the
         * first one present) so an idle server does not spin. */
        int wp = s_active;
        if (wp < 0) wp = s_have[PORT_USB] ? PORT_USB : PORT_UART;
        TickType_t slice = pdMS_TO_TICKS(10);
        if (slice < 1) slice = 1;
        if (slice > limit - elapsed) slice = limit - elapsed;
        int n = port_read(wp, buf, max, slice);
        if (n > 0) {
            s_active = wp;
            return n;
        }
    }
}

void kport_write(const uint8_t *buf, size_t len)
{
    switch (s_active) {
#if defined(KPORT_HAS_UART)
    case PORT_UART:
        uart_write_bytes(CONFIG_ESP_CONSOLE_UART_NUM, buf, len);
        break;
#endif
#if defined(KPORT_HAS_USB)
    case PORT_USB:
        while (len > 0) {
            size_t n = tinyusb_cdcacm_write_queue(TINYUSB_CDC_ACM_0, buf, len);
            if (n == 0) {
                /* FIFO full: send it, or give up if the host is gone. */
                if (tinyusb_cdcacm_write_flush(TINYUSB_CDC_ACM_0,
                                               pdMS_TO_TICKS(500)) != ESP_OK)
                    return;
                continue;
            }
            buf += n;
            len -= n;
        }
        tinyusb_cdcacm_write_flush(TINYUSB_CDC_ACM_0, 0);
        break;
#endif
    default:
        break;
    }
}
