#include "time_sync.h"

#include <stdlib.h>
#include <sys/time.h>

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>
#include <esp_log.h>
#include <esp_netif_sntp.h>

#include "rtc_clock.h"
#include "wifi_manager.h"

static const char *TAG = "time_sync";

#define AUTO_SYNC_TIMEOUT_MS 10000

static SemaphoreHandle_t s_mutex;
static time_sync_rtc_status_t s_rtc_status = TIME_SYNC_RTC_NONE;
static time_t s_last_sntp;
/* Set once SNTP was tried (successfully or not) on the current WiFi
 * connection; cleared when WiFi goes down. Keeps a dead NTP server to
 * one bounded wait per connection instead of one per caller. */
static volatile bool s_tried_this_conn;
static volatile bool s_task_running;

static esp_err_t sync_locked(uint32_t timeout_ms)
{
    if (!wifi_manager_is_connected()) return ESP_ERR_INVALID_STATE;
    s_tried_this_conn = true;

    esp_sntp_config_t cfg = ESP_NETIF_SNTP_DEFAULT_CONFIG("pool.ntp.org");
    esp_err_t err = esp_netif_sntp_init(&cfg);
    if (err != ESP_OK) return err;
    err = esp_netif_sntp_sync_wait(pdMS_TO_TICKS(timeout_ms));
    esp_netif_sntp_deinit();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "SNTP sync failed: %s", esp_err_to_name(err));
        return err;
    }

    s_last_sntp = time(NULL);
    ESP_LOGI(TAG, "SNTP time acquired");
    if (rtc_clock_available() && rtc_clock_save_system_time() == ESP_OK &&
        s_rtc_status == TIME_SYNC_RTC_TIME_LOST) {
        s_rtc_status = TIME_SYNC_RTC_OK;
    }
    return ESP_OK;
}

esp_err_t time_sync_now(uint32_t timeout_ms)
{
    if (!s_mutex) return ESP_ERR_INVALID_STATE;
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    esp_err_t err = sync_locked(timeout_ms);
    xSemaphoreGive(s_mutex);
    return err;
}

void time_sync_ensure(uint32_t timeout_ms)
{
    if (!s_mutex) return;
    /* Waits here while the on-connect task is still syncing. */
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    if (!s_tried_this_conn) sync_locked(timeout_ms);
    xSemaphoreGive(s_mutex);
}

esp_err_t time_sync_with_saved_wifi(uint32_t timeout_ms)
{
    if (wifi_manager_is_connected()) return time_sync_now(timeout_ms);

    esp_err_t err = wifi_manager_connect();
    if (err != ESP_OK) return err;
    /* The on-connect listener has queued a sync; ensure() either runs
     * it first or waits for it. */
    time_t before = s_last_sntp;
    time_sync_ensure(timeout_ms);
    err = (s_last_sntp != before) ? ESP_OK : ESP_ERR_TIMEOUT;
    wifi_manager_disconnect();
    return err;
}

static void auto_sync_task(void *arg)
{
    (void)arg;
    time_sync_ensure(AUTO_SYNC_TIMEOUT_MS);
    s_task_running = false;
    vTaskDelete(NULL);
}

/* WiFi listener: runs on the event task, so only kicks off a task. */
static void on_wifi_state(wifi_state_t st)
{
    if (st == WIFI_STATE_CONNECTED) {
        /* CONNECTED is re-fired when an IPv6 address arrives; one sync
         * per connection is enough. */
        if (s_tried_this_conn || s_task_running) return;
        s_task_running = true;
        if (xTaskCreate(auto_sync_task, "time_sync", 4096, NULL, 3, NULL) != pdPASS) {
            s_task_running = false;
        }
    } else if (st != WIFI_STATE_CONNECTING) {
        s_tried_this_conn = false;
    }
}

void time_sync_init(const char *tz, int rtc_sda_pin, int rtc_scl_pin,
                    uint8_t rtc_addr)
{
    if (!s_mutex) s_mutex = xSemaphoreCreateMutex();

    setenv("TZ", (tz && tz[0]) ? tz : "UTC0", 1);
    tzset();

    if (rtc_addr) {
        rtc_clock_init(rtc_sda_pin, rtc_scl_pin, rtc_addr);
        esp_err_t err = rtc_clock_restore_system_time();
        if (err == ESP_OK)                        s_rtc_status = TIME_SYNC_RTC_OK;
        else if (err == ESP_ERR_INVALID_RESPONSE) s_rtc_status = TIME_SYNC_RTC_TIME_LOST;
        else                                      s_rtc_status = TIME_SYNC_RTC_NO_RESPONSE;
    }

    wifi_manager_add_listener(on_wifi_state);
}

esp_err_t time_sync_set(time_t utc)
{
    /* rtc_clock_set_time() sets the system clock too, with or without
     * a chip present. */
    esp_err_t err = rtc_clock_set_time(utc);
    if (err == ESP_OK && s_rtc_status == TIME_SYNC_RTC_TIME_LOST) {
        s_rtc_status = TIME_SYNC_RTC_OK;
    }
    return err;
}

time_sync_rtc_status_t time_sync_rtc_status(void) { return s_rtc_status; }
time_t time_sync_last_sntp(void) { return s_last_sntp; }
