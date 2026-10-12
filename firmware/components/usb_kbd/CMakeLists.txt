## USB HID host is used on ESP32-P4 (Tab5 BSP) and ESP32-S3 (native
## USB-OTG). Register an empty component on other targets so optional
## requirements stay a no-op where usb/hid_host.h is unavailable.
idf_build_get_property(target IDF_TARGET)
if(target STREQUAL "esp32p4" OR target STREQUAL "esp32s3")
    idf_component_register(
        SRCS "usb_kbd.cpp"
        INCLUDE_DIRS "include"
        PRIV_REQUIRES ble_keyboard usb driver
    )
else()
    idf_component_register()
endif()
