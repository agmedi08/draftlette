idf_component_register(
    SRCS "rtc_clock.c"
    INCLUDE_DIRS "include"
    PRIV_REQUIRES esp_driver_i2c
)
