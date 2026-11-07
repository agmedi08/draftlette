#include "planner_ui.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

#define MIN_VALID_YEAR 2025
#define BODY_TOP       34

static lv_obj_t *s_scr;
static lv_obj_t *s_lbl_title;
static lv_obj_t *s_lbl_body;
static lv_obj_t *s_lbl_hint;
static bool s_active;
static planner_view_t s_view = PLANNER_VIEW_TODAY;

static void build_screen(void)
{
    int32_t w = lv_display_get_horizontal_resolution(lv_display_get_default());

    s_scr = lv_obj_create(NULL);
    lv_obj_remove_flag(s_scr, LV_OBJ_FLAG_SCROLLABLE);

    s_lbl_title = lv_label_create(s_scr);
    lv_obj_set_pos(s_lbl_title, 6, 4);

    s_lbl_body = lv_label_create(s_scr);
    lv_obj_set_pos(s_lbl_body, 6, BODY_TOP);
    lv_obj_set_width(s_lbl_body, w - 12);
    lv_label_set_long_mode(s_lbl_body, LV_LABEL_LONG_WRAP);

    s_lbl_hint = lv_label_create(s_scr);
    lv_obj_set_width(s_lbl_hint, w - 4);
    lv_label_set_long_mode(s_lbl_hint, LV_LABEL_LONG_CLIP);
    lv_obj_align(s_lbl_hint, LV_ALIGN_BOTTOM_LEFT, 2, -2);
    lv_label_set_text(s_lbl_hint, "F1:Menu  F2:Today  F3:Week  F4:Month  F5:Notes");
}

static void apply_style(const planner_ui_style_t *style)
{
    lv_obj_set_style_bg_color(s_scr, style->bg, 0);
    lv_obj_set_style_text_font(s_lbl_title, style->title_font, 0);
    lv_obj_set_style_text_font(s_lbl_body, style->body_font, 0);
    lv_obj_set_style_text_font(s_lbl_hint, style->hint_font, 0);
    lv_obj_set_style_text_color(s_lbl_title, style->fg, 0);
    lv_obj_set_style_text_color(s_lbl_body, style->fg, 0);
    lv_obj_set_style_text_color(s_lbl_hint, style->fg, 0);
}

static void fill_today(const struct tm *now, char *out, size_t out_sz)
{
    char date[32];
    strftime(date, sizeof(date), "%a %Y-%m-%d", now);
    snprintf(out, out_sz, "%s\n\nTasks, events and notes will go here.", date);
}

/* Monday first. Stepping tm_mday at noon lets mktime() handle month
 * ends, and noon stays clear of daylight-saving changes. */
static void fill_week(const struct tm *now, char *out, size_t out_sz)
{
    struct tm day = *now;
    day.tm_hour = 12;
    day.tm_min = 0;
    day.tm_sec = 0;
    day.tm_isdst = -1;
    day.tm_mday -= (now->tm_wday + 6) % 7;

    size_t used = 0;
    for (int i = 0; i < 7; i++) {
        struct tm d = day;
        d.tm_mday += i;
        mktime(&d);
        char date[16];
        strftime(date, sizeof(date), "%a %m-%d", &d);
        bool today = d.tm_yday == now->tm_yday && d.tm_year == now->tm_year;
        int n = snprintf(out + used, out_sz - used, "%s %s\n",
                         today ? ">" : " ", date);
        if (n < 0 || (size_t)n >= out_sz - used) break;
        used += (size_t)n;
    }
}

static void fill_month(const struct tm *now, char *out, size_t out_sz)
{
    char date[32];
    strftime(date, sizeof(date), "%B %Y", now);
    snprintf(out, out_sz, "%s\n\nTracker symbols will go here.", date);
}

lv_obj_t *planner_ui_render(planner_view_t view, const planner_ui_style_t *style)
{
    static const char *const titles[] = { "Today", "Week", "Month" };

    if (!s_scr) build_screen();
    apply_style(style);

    time_t t = time(NULL);
    struct tm now;
    localtime_r(&t, &now);

    char body[192];
    if (now.tm_year + 1900 < MIN_VALID_YEAR) {
        snprintf(body, sizeof(body),
                 "The clock is not set.\n\nF1 > Settings > Date and time");
    } else if (view == PLANNER_VIEW_WEEK) {
        fill_week(&now, body, sizeof(body));
    } else if (view == PLANNER_VIEW_MONTH) {
        fill_month(&now, body, sizeof(body));
    } else {
        fill_today(&now, body, sizeof(body));
    }

    lv_label_set_text(s_lbl_title, titles[view]);
    lv_label_set_text(s_lbl_body, body);

    s_view = view;
    s_active = true;
    return s_scr;
}

bool planner_ui_is_active(void) { return s_active; }
planner_view_t planner_ui_current(void) { return s_view; }
void planner_ui_deactivate(void) { s_active = false; }
