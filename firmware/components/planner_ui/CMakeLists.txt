## Planner views (Today, Week, Month): screens of their own, kept apart
## from the Notes editor. editor_ui owns screen loading and routes keys
## here; day data comes from the journal service.

idf_component_register(
    SRCS "planner_ui.c"
    INCLUDE_DIRS "include"
    REQUIRES lvgl__lvgl ble_keyboard
    PRIV_REQUIRES journal kb_layout sd_card
)
