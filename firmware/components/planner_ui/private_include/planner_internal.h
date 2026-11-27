#pragma once

/* Shared between planner_ui.c and diary_view.c: the title and status
 * bar every planner screen has, and the diary view's entry points. */

#include "planner_ui.h"

#define PLANNER_MIN_VALID_YEAR 2025
#define PLANNER_TITLE_Y        4
#define PLANNER_HINT_VIEWS \
    "F1:Menu  F2:Today  F3:Week  F4:Month  F5:Notes  F6:Diary"

/* The title, the lines under it and above the status bar, the hint at
 * the left of the status bar and the clock and battery at its right. */
typedef struct {
    lv_obj_t *title;
    lv_obj_t *hint;
    lv_obj_t *header_rule;
    lv_obj_t *status_rule;
    lv_obj_t *clock;
    lv_obj_t *batt;
} planner_chrome_t;

/* Make c's objects on scr. planner_ui_set_clock() and _set_battery()
 * keep every chrome made here up to date. Up to two chromes. */
void planner_chrome_build(lv_obj_t *scr, planner_chrome_t *c);

/* Fonts, colours and places. Returns the y of the status bar's line. */
int32_t planner_chrome_style(planner_chrome_t *c, const planner_ui_style_t *style);

/* The y of the line under the title, and of the first body row below
 * it. Both follow the title font's height: 25 / 30 with the 17 px
 * Greybeard title, lower with the taller Hack title on HIDPI boards. */
int32_t planner_header_rule_y(const planner_ui_style_t *style);
int32_t planner_body_top(const planner_ui_style_t *style);

/* ---- The diary view (diary_view.c) ---- */

/* Fill the diary screen and return it. today: show today's entry in
 * reading mode, not the day and mode last shown. */
lv_obj_t *diary_view_render(const planner_ui_style_t *style, bool today);
void diary_view_key(const kb_event_t *ev);

/* Leave a half-typed tag. */
void diary_view_commit(void);

void diary_view_set_writer(planner_diary_write_fn fn);
