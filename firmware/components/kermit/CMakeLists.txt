## Kermit file-transfer server on the serial console (F1 menu ->
## "Sync via browser"). See include/kermit.h.
idf_component_register(
    SRCS "kermit.c" "kermit_port.c"
    INCLUDE_DIRS "include"
    PRIV_REQUIRES esp_driver_uart esp_driver_usb_serial_jtag esp_rom
                  esp_timer log vfs
)
