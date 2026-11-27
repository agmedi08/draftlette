#pragma once

/* time_sync: the device's wall clock as a general service.
 *
 * - applies the local time zone (system time itself stays UTC),
 * - restores the clock from a battery-backed RTC chip at boot, when the
 *   board has one, and writes the time back whenever it is set,
 * - fetches the time over SNTP automatically every time WiFi connects,
 *   and on request.
 *
 * Features that need a trustworthy clock (Git commits, the journal)
 * call time_sync_ensure() instead of running SNTP themselves. */

#ifdef __cplusplus
extern "C" {
#endif

#include <esp_err.h>
#include <stdbool.h>
#include <stdint.h>
#include <time.h>

typedef enum {
    TIME_SYNC_RTC_NONE,         /* board has no RTC chip */
    TIME_SYNC_RTC_OK,           /* clock restored from the RTC at boot */
    TIME_SYNC_RTC_TIME_LOST,    /* RTC lost power; its time was invalid */
    TIME_SYNC_RTC_NO_RESPONSE,  /* RTC did not answer on I2C */
} time_sync_rtc_status_t;

/* Call once at boot, before anything reads the clock. tz is a POSIX TZ
 * string such as "CST6CDT,M3.2.0,M11.1.0". rtc_addr 0 means the board
 * has no RTC (the pins are then ignored). Also registers the WiFi
 * listener that syncs over SNTP on every connect. */
void time_sync_init(const char *tz, int rtc_sda_pin, int rtc_scl_pin,
                    uint8_t rtc_addr);

/* Blocking SNTP sync (WiFi must already be connected). Saves the new
 * time to the RTC on success. Returns ESP_ERR_TIMEOUT when no answer
 * arrives within timeout_ms, ESP_ERR_INVALID_STATE without WiFi. Call
 * it off the UI task. */
esp_err_t time_sync_now(uint32_t timeout_ms);

/* "Sync time now": like time_sync_now(), but first connects to the
 * saved WiFi network when not connected, and disconnects again
 * afterwards in that case. Blocks for up to ~30 s plus timeout_ms;
 * call it off the UI task. ESP_ERR_NOT_FOUND = no saved network. */
esp_err_t time_sync_with_saved_wifi(uint32_t timeout_ms);

/* For features about to stamp something with the time: returns at once
 * when SNTP already succeeded or was tried since WiFi last connected
 * (waiting for a sync still running), otherwise runs one, like
 * time_sync_now(). Never fails hard -- a failed sync just leaves the
 * clock as it was. */
void time_sync_ensure(uint32_t timeout_ms);

/* Set the clock by hand (e.g. from Settings); also saves to the RTC. */
esp_err_t time_sync_set(time_t utc);

time_sync_rtc_status_t time_sync_rtc_status(void);

/* Time of the last successful SNTP sync this boot, 0 if none. */
time_t time_sync_last_sntp(void);

#ifdef __cplusplus
}
#endif
