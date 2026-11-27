## Bullet-journal day files on the SD card: the data behind the Today,
## Week and Month views. No UI, and plain C stdio, so host_test/ can
## run it on a computer.

idf_component_register(
    SRCS "journal.c"
    INCLUDE_DIRS "include"
)
