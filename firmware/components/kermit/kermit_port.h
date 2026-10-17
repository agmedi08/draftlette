#pragma once
/*
 * Serial transport for the Kermit server: the console port(s).
 *
 * kport_open() silences every console output path (ESP_LOG, ROM
 * printf) and installs the UART driver on the console UART, plus a
 * TinyUSB CDC-ACM serial port on the USB-OTG controller when the build
 * enables TinyUSB CDC (it takes the USB port over from USB-Serial/JTAG
 * -- see kermit_port.c for why). The server answers on whichever port
 * the last bytes arrived on. kport_close() removes the drivers, hands
 * the USB port back to USB-Serial/JTAG and restores console output.
 */

#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

esp_err_t kport_open(void);
void kport_close(void);

/* Read up to max bytes, waiting at most timeout_ms. Returns the
 * number of bytes read (0 on timeout). */
int kport_read(uint8_t *buf, size_t max, uint32_t timeout_ms);

/* Write to the port the last input came from (dropped if no input has
 * arrived yet). */
void kport_write(const uint8_t *buf, size_t len);

/* 1 while a USB host has configured the TinyUSB serial port (and the
 * bus is not suspended), 0 otherwise, -1 when the build has no USB
 * port to watch. */
int kport_usb_connected(void);
