## Kermit file-transfer server on the serial console (F1 menu ->
## "Sync via browser"). See include/kermit.h.
##
## Built only on boards with CONFIG_DRAFTLING_HAS_KERMIT; elsewhere it
## registers as an empty component. CONFIG_* reads empty during
## ESP-IDF's early, Kconfig-blind requirements pass (see
## components/ble_keyboard/CMakeLists.txt), so the requirements are added
## with idf_component_optional_requires() after registration.
if(CONFIG_DRAFTLING_HAS_KERMIT)
    idf_component_register(
        SRCS "kermit.c" "kermit_port.c"
        INCLUDE_DIRS "include"
    )
    ## espressif__esp_tinyusb is declared in components/usb_msc's
    ## idf_component.yml (every board with Kermit also has "SD card via
    ## USB"); esp_hal_usb provides hal/usb_serial_jtag_hal.h for handing
    ## the USB PHY back to USB-Serial/JTAG (see kermit_port.c).
    idf_component_optional_requires(PRIVATE esp_driver_uart esp_rom
                                    esp_timer log vfs
                                    espressif__esp_tinyusb esp_hal_usb)
else()
    idf_component_register()
endif()
