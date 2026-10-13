/*
 * Serial transport for the Kermit server -- see kermit_port.h.
 *
 * Every build has its primary console on a UART (UART0 through the
 * board's USB-UART bridge, where it has one) and, on ESP32-S3 / P4,
 * a secondary console on the chip's USB-Serial/JTAG port, which is
 * the only serial port a PC sees on boards whose USB-C socket goes
 * straight to the chip. The server listens on both and answers on
 * whichever one the client is talking on.
 */

#include <stdarg.h>
#include <stdbool.h>
#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_rom_sys.h"
#include "esp_rom_serial_output.h"
#include "kermit_port.h"

#if defined(CONFIG_ESP_CONSOLE_UART)
#include "driver/uart.h"
#define KPORT_HAS_UART 1
#endif
#if defined(CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG) || \
    defined(CONFIG_ESP_CONSOLE_SECONDARY_USB_SERIAL_JTAG)
#include "driver/usb_serial_jtag.h"
#define KPORT_HAS_JTAG 1
#endif

static const char *TAG = "kermit";

enum { PORT_UART = 0, PORT_JTAG = 1, PORT_COUNT = 2 };

#define KPORT_RX_BUF 4096
#define KPORT_TX_BUF 4096

static bool          s_have[PORT_COUNT];
static int           s_active = -1;      /* port of the last input */
static vprintf_like_t s_prev_vprintf;

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
    s_have[PORT_JTAG] = false;

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
#if defined(KPORT_HAS_JTAG)
    usb_serial_jtag_driver_config_t jcfg = USB_SERIAL_JTAG_DRIVER_CONFIG_DEFAULT();
    jcfg.rx_buffer_size = KPORT_RX_BUF;
    jcfg.tx_buffer_size = KPORT_TX_BUF;
    esp_err_t jerr = usb_serial_jtag_driver_install(&jcfg);
    if (jerr == ESP_OK) s_have[PORT_JTAG] = true;
    else if (err == ESP_OK && !s_have[PORT_UART]) err = jerr;
#endif

    if (!s_have[PORT_UART] && !s_have[PORT_JTAG]) {
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
#if defined(KPORT_HAS_JTAG)
    if (s_have[PORT_JTAG]) {
        usb_serial_jtag_wait_tx_done(pdMS_TO_TICKS(200));
        usb_serial_jtag_driver_uninstall();
    }
#endif
    s_have[PORT_UART] = false;
    s_have[PORT_JTAG] = false;
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
#if defined(KPORT_HAS_JTAG)
    case PORT_JTAG:
        n = usb_serial_jtag_read_bytes(buf, max, ticks);
        break;
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
        if (wp < 0) wp = s_have[PORT_UART] ? PORT_UART : PORT_JTAG;
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
#if defined(KPORT_HAS_JTAG)
    case PORT_JTAG:
        while (len > 0) {
            int n = usb_serial_jtag_write_bytes(buf, len, pdMS_TO_TICKS(500));
            if (n <= 0) break;   /* host gone: drop the rest */
            buf += n;
            len -= (size_t)n;
        }
        break;
#endif
    default:
        break;
    }
}
