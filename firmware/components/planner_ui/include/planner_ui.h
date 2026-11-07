#pragma once

/* planner_ui: the Today, Week and Month screens.
 *
 * This component only draws them. The caller loads the returned screen
 * and routes keys, so it stays in charge of what is on the display. */

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <lvgl.h>

#include "ble_keyboard.h"

typedef enum {
    PLANNER_VIEW_TODAY,
    PLANNER_VIEW_WEEK,
    PLANNER_VIEW_MONTH,
} planner_view_t;

typedef struct {
    const lv_font_t *title_font;
    const lv_font_t *body_font;
    const lv_font_t *hint_font;
    lv_color_t fg;
    lv_color_t bg;
} planner_ui_style_t;

/* Build (first call) and fill the planner screen for view, mark the
 * planner active, and return the screen for the caller to load. Dates
 * are read from the clock on every call. */
lv_obj_t *planner_ui_render(planner_view_t view, const planner_ui_style_t *style);

/* True while a planner view owns the display, including while the menu
 * or help is open on top of it. */
bool planner_ui_is_active(void);

planner_view_t planner_ui_current(void);

/* The display went to another view. Does not load anything. */
void planner_ui_deactivate(void);

/* A key for the planner screen on display. Touches LVGL, so call it
 * from the LVGL task. */
void planner_ui_handle_key(const kb_event_t *ev);

/* Save a bullet still being typed and leave any prompt. Call before
 * something else takes over the display or the device sleeps. */
void planner_ui_commit_pending(void);

#ifdef __cplusplus
}
#endif
