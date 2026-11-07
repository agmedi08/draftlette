## Wall-clock service: time zone, battery-backed RTC restore/save and
## SNTP over WiFi. Independent of any feature (Git, journal, ...); those
## call into it.

idf_component_register(
    SRCS "time_sync.c"
    INCLUDE_DIRS "include"
    PRIV_REQUIRES esp_netif rtc_clock wifi_manager
)
