#pragma once
/*
 * Serial transport for the Kermit server: the console port(s).
 *
 * kport_open() silences every console output path (ESP_LOG, ROM
 * printf) and installs the UART driver on the console UART, plus the
 * USB-Serial/JTAG driver when the build routes a console there. The
 * server answers on whichever port the last bytes arrived on.
 * kport_close() removes the drivers and restores console output.
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
