#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>
#include <time.h>

#include "esp_err.h"

/*
 * Battery-backed PCF85063 real-time clock.
 *
 * The chip keeps UTC. On boards that have one, main.cpp calls
 * rtc_clock_init() with the board's I2C pins and address, then
 * rtc_clock_restore_system_time() so the system clock is right after
 * a full power-off (the ESP32's own RTC timer domain only survives
 * deep sleep). Whenever the system clock is set from a better source
 * (SNTP, or the user in Settings), call rtc_clock_save_system_time().
 *
 * The I2C bus is opened only for each transfer and deleted again, so
 * this does not hold an I2C port. All functions are no-ops returning
 * ESP_ERR_INVALID_STATE until rtc_clock_init() has been called.
 */
esp_err_t rtc_clock_init(int sda_pin, int scl_pin, uint8_t addr);

/* True once rtc_clock_init() has been called (the board has an RTC). */
bool rtc_clock_available(void);

/*
 * Read the chip and set the system clock from it. Returns
 * ESP_ERR_INVALID_RESPONSE (and leaves the system clock alone) when the
 * chip reports that its oscillator stopped, i.e. the time was lost.
 */
esp_err_t rtc_clock_restore_system_time(void);

/* Write the current system time (UTC) to the chip. */
esp_err_t rtc_clock_save_system_time(void);

/* Set the system clock to `utc` and save it to the chip. */
esp_err_t rtc_clock_set_time(time_t utc);

#ifdef __cplusplus
}
#endif
