#pragma once

/* planner_ui: the Today, Week, Month and Diary screens, plus the day and
 * weekly pages opened from the Week grid.
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
    PLANNER_VIEW_DAY,       /* reached from the Week grid or by paging */
    PLANNER_VIEW_WEEKLY,    /* reached only from the Week grid */
    PLANNER_VIEW_DIARY,     /* one prose entry a day, and its calendar */
} planner_view_t;

/* Writing a diary entry happens in the Notes editor. The diary calls
 * this with the entry's file, which exists by then. Returns NULL when
 * the editor has taken the display, or a message to show instead. */
typedef const char *(*planner_diary_write_fn)(const char *path);

typedef struct {
    const lv_font_t *title_font;
    const lv_font_t *body_font;
    const lv_font_t *hint_font;
    lv_color_t fg;
    lv_color_t bg;
    int32_t status_h;       /* height of the bottom status bar */
    bool has_battery;       /* leave a battery slot in the status bar */
} planner_ui_style_t;

/* Build (first call) and fill the planner screen for view, mark the
 * planner active, and return the screen for the caller to load. Dates
 * are read from the clock on every call. */
lv_obj_t *planner_ui_render(planner_view_t view, const planner_ui_style_t *style);

/* The next Week render shows the current week with today selected,
 * instead of the week last shown. For the user asking for Week, not for
 * redrawing it after the menu. */
void planner_ui_go_to_this_week(void);

/* The next Diary render shows today's entry in reading mode, instead of
 * the day and mode last shown. For the user asking for the Diary, not
 * for coming back to it from writing or the menu. */
void planner_ui_diary_go_to_today(void);

void planner_ui_set_diary_writer(planner_diary_write_fn fn);

/* True while a planner view owns the display, including while the menu
 * or help is open on top of it. */
bool planner_ui_is_active(void);

planner_view_t planner_ui_current(void);

/* The display went to another view. Does not load anything. */
void planner_ui_deactivate(void);

/* The text of the status bar's clock ("14:05") and battery ("85%")
 * slots. The caller owns the readings and calls these whenever they
 * change; the planner keeps the latest text and shows it on its screen,
 * also when the screen is built later. A label is only rewritten when
 * its text changes. Touches LVGL, so call from the LVGL task. */
void planner_ui_set_clock(const char *text);
void planner_ui_set_battery(const char *text);

/* A key for the planner screen on display. Touches LVGL, so call it
 * from the LVGL task. */
void planner_ui_handle_key(const kb_event_t *ev);

/* Save a bullet still being typed and leave any prompt. Call before
 * something else takes over the display or the device sleeps. */
void planner_ui_commit_pending(void);

#ifdef __cplusplus
}
#endif
