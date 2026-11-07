## Planner views (Today, Week, Month): screens of their own, kept apart
## from the Notes editor. editor_ui owns screen loading and key routing.

idf_component_register(
    SRCS "planner_ui.c"
    INCLUDE_DIRS "include"
    REQUIRES lvgl__lvgl
)
