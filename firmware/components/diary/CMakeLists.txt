## Diary entries on the SD card: one prose file per day, with tags.
## The data behind the Diary view. No UI, and plain C stdio, so
## host_test/ can run it on a computer.

idf_component_register(
    SRCS "diary.c"
    INCLUDE_DIRS "include"
)
