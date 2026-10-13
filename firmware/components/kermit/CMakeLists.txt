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
    idf_component_optional_requires(PRIVATE esp_driver_uart
                                    esp_driver_usb_serial_jtag esp_rom
                                    esp_timer log vfs)
else()
    idf_component_register()
endif()
