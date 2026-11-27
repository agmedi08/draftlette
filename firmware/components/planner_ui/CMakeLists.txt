## Planner views (Today, Week, Month, Diary): screens of their own, kept apart
## from the Notes editor. editor_ui owns screen loading and routes keys
## here; day data comes from the journal service, diary entries from
## the diary service.

idf_component_register(
    SRCS "planner_ui.c" "diary_view.c"
    INCLUDE_DIRS "include"
    PRIV_INCLUDE_DIRS "private_include"
    REQUIRES lvgl__lvgl ble_keyboard
    PRIV_REQUIRES diary journal kb_layout sd_card status_bar
)
