/* The Diary view: one prose entry a day.
 *
 * Reading mode shows a day's entry. Enter writes in it (in the Notes
 * editor, through the writer editor_ui registers), Left / Right go to
 * the previous / next day that has an entry, Shift+Left / Right to the
 * previous / next day whatever it holds, T adds or removes a two-letter
 * tag and C opens the calendar: a month of days, with a check mark on
 * the ones written on and each day's tags. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "diary.h"
#include "journal.h"
#include "kb_layout.h"
#include "planner_internal.h"
#include "sd_card.h"

#define CAL_COLS     7
#define CAL_ROWS     6
#define CAL_CELLS    (CAL_COLS * CAL_ROWS)
#define SCROLL_LINES 3          /* Up / Down in reading mode */
#define PATH_LEN     208

#define HINT_READ "Enter:write  Left/Right:entry  Shift+Left/Right:day  " \
                  "C:calendar  T:tag  Up/Down:scroll"
#define HINT_CAL  "Arrows:day  ? or ,:month  Enter:open  Home:today  Esc:back"
#define TRUNCATED_NOTE \
    "\n\n[The rest is too long to show here. Enter opens all of it.]"

typedef enum {
    DV_READ,
    DV_TAG,         /* typing a tag in reading mode */
    DV_CALENDAR,
} dv_mode_t;

static lv_obj_t *s_scr;
static planner_chrome_t s_chrome;
static lv_obj_t *s_page;            /* scrolls the entry's text */
static lv_obj_t *s_text;
static lv_obj_t *s_cal;             /* the calendar, parent of its cells */
static lv_obj_t *s_cal_head[CAL_COLS];
static lv_obj_t *s_cell[CAL_CELLS];
static lv_obj_t *s_cell_num[CAL_CELLS];
static lv_obj_t *s_cell_tags[CAL_CELLS];
static lv_obj_t *s_cell_check[CAL_CELLS];
static planner_ui_style_t s_style;
static planner_diary_write_fn s_writer;

static bool s_ready;                /* clock and card usable */
static dv_mode_t s_mode = DV_READ;
static int s_y, s_m, s_d;           /* the day in reading mode; s_y 0: none yet */
static int s_shown_y, s_shown_m, s_shown_d;     /* the day s_text holds */
static diary_info_t s_shown;        /* what that day holds */
static int s_cy, s_cm, s_cd;        /* the day picked on the calendar */
static int s_info_y, s_info_m;      /* the month s_info holds; 0: reload */
static diary_info_t s_info[31];
static char s_tag[DIARY_TAG_LEN + 1];
static const char *s_msg;           /* replaces the hint until the next key */
static char s_msg_buf[48];

/* A check mark, drawn: the font has no glyph for one. */
static const lv_point_precise_t s_check_pts[] = { { 0, 5 }, { 4, 9 }, { 11, 0 } };

/* ---- Dates ---- */

static void read_clock(struct tm *now)
{
    time_t t = time(NULL);
    localtime_r(&t, now);
}

/* Noon keeps these clear of daylight-saving changes. */
static void date_to_tm(struct tm *t, int y, int m, int d)
{
    memset(t, 0, sizeof(*t));
    t->tm_year = y - 1900;
    t->tm_mon = m - 1;
    t->tm_mday = d;
    t->tm_hour = 12;
    t->tm_isdst = -1;
    mktime(t);
}

static bool is_today(int y, int m, int d)
{
    struct tm now;
    read_clock(&now);
    return now.tm_year + 1900 == y && now.tm_mon + 1 == m && now.tm_mday == d;
}

static int days_in_month(int y, int m)
{
    static const int days[12] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
    bool leap = (y % 4 == 0 && y % 100 != 0) || y % 400 == 0;
    return m == 2 && leap ? 29 : days[m - 1];
}

/* The column of the month's 1st, Monday first as in the Week grid. */
static int first_column(int y, int m)
{
    struct tm t;
    date_to_tm(&t, y, m, 1);
    return (t.tm_wday + 6) % 7;
}

/* ---- Screen ---- */

static void build_screen(void)
{
    s_scr = lv_obj_create(NULL);
    lv_obj_remove_flag(s_scr, LV_OBJ_FLAG_SCROLLABLE);

    s_page = lv_obj_create(s_scr);
    lv_obj_remove_style_all(s_page);
    lv_obj_set_scroll_dir(s_page, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(s_page, LV_SCROLLBAR_MODE_OFF);
    s_text = lv_label_create(s_page);
    lv_obj_set_width(s_text, lv_pct(100));
    lv_label_set_long_mode(s_text, LV_LABEL_LONG_WRAP);

    s_cal = lv_obj_create(s_scr);
    lv_obj_remove_style_all(s_cal);
    lv_obj_remove_flag(s_cal, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_cal, LV_OBJ_FLAG_HIDDEN);

    static const char *const names[CAL_COLS] = {
        "Mon", "Tue", "Wed", "Thu", "Fri", "Sat", "Sun",
    };
    for (int i = 0; i < CAL_COLS; i++) {
        s_cal_head[i] = lv_label_create(s_cal);
        lv_label_set_long_mode(s_cal_head[i], LV_LABEL_LONG_CLIP);
        lv_obj_set_style_text_align(s_cal_head[i], LV_TEXT_ALIGN_CENTER, 0);
        lv_label_set_text(s_cal_head[i], names[i]);
    }
    for (int i = 0; i < CAL_CELLS; i++) {
        s_cell[i] = lv_obj_create(s_cal);
        lv_obj_remove_style_all(s_cell[i]);
        lv_obj_remove_flag(s_cell[i], LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_bg_opa(s_cell[i], LV_OPA_COVER, 0);
        s_cell_num[i] = lv_label_create(s_cell[i]);
        s_cell_tags[i] = lv_label_create(s_cell[i]);
        lv_label_set_long_mode(s_cell_tags[i], LV_LABEL_LONG_WRAP);
        s_cell_check[i] = lv_line_create(s_cell[i]);
        lv_line_set_points(s_cell_check[i], s_check_pts,
                           sizeof(s_check_pts) / sizeof(s_check_pts[0]));
        lv_obj_set_style_line_width(s_cell_check[i], 2, 0);
    }

    planner_chrome_build(s_scr, &s_chrome);
}

static void apply_style(const planner_ui_style_t *style)
{
    s_style = *style;
    lv_obj_set_style_bg_color(s_scr, style->bg, 0);
    int32_t bar_y = planner_chrome_style(&s_chrome, style);
    int32_t w = lv_display_get_horizontal_resolution(lv_display_get_default());
    int32_t body_top = planner_body_top(style);

    lv_obj_set_pos(s_page, 6, body_top);
    lv_obj_set_size(s_page, w - 12, bar_y - 2 - body_top);
    lv_obj_set_style_text_font(s_text, style->body_font, 0);
    lv_obj_set_style_text_color(s_text, style->fg, 0);

    int32_t cal_top = body_top - 2;
    int32_t head_h = lv_font_get_line_height(style->hint_font) + 2;
    int32_t cell_w = (w - 8) / CAL_COLS;
    int32_t cell_h = (bar_y - 1 - cal_top - head_h) / CAL_ROWS;
    int32_t num_h = lv_font_get_line_height(style->body_font) + 2;

    lv_obj_set_pos(s_cal, 4, cal_top);
    lv_obj_set_size(s_cal, cell_w * CAL_COLS, head_h + cell_h * CAL_ROWS);
    for (int i = 0; i < CAL_COLS; i++) {
        lv_obj_set_style_text_font(s_cal_head[i], style->hint_font, 0);
        lv_obj_set_style_text_color(s_cal_head[i], style->fg, 0);
        lv_obj_set_pos(s_cal_head[i], i * cell_w, 0);
        lv_obj_set_width(s_cal_head[i], cell_w);
    }
    for (int i = 0; i < CAL_CELLS; i++) {
        lv_obj_set_pos(s_cell[i], (i % CAL_COLS) * cell_w,
                       head_h + (i / CAL_COLS) * cell_h);
        lv_obj_set_size(s_cell[i], cell_w - 2, cell_h - 2);
        lv_obj_set_style_border_color(s_cell[i], style->fg, 0);
        lv_obj_set_style_text_font(s_cell_num[i], style->body_font, 0);
        lv_obj_set_pos(s_cell_num[i], 4, 2);
        lv_obj_set_style_text_font(s_cell_tags[i], style->body_font, 0);
        lv_obj_set_pos(s_cell_tags[i], 4, num_h + 2);
        lv_obj_set_size(s_cell_tags[i], cell_w - 10, cell_h - num_h - 6);
        lv_obj_set_pos(s_cell_check[i], cell_w - 2 - 18, 5);
    }
}

static void update_hint(void)
{
    static char hint[64];
    const char *text = HINT_READ;

    if (s_msg) {
        text = s_msg;
    } else if (s_mode == DV_CALENDAR) {
        text = HINT_CAL;
    } else if (s_mode == DV_TAG) {
        bool have = false;
        for (int i = 0; i < s_shown.tag_count; i++) {
            if (strcmp(s_shown.tags[i], s_tag) == 0) have = true;
        }
        snprintf(hint, sizeof(hint), "Tag: %s_  Enter:%s  Esc:cancel", s_tag,
                 strlen(s_tag) < DIARY_TAG_LEN ? "add" : have ? "remove" : "add");
        text = hint;
    }
    lv_label_set_text(s_chrome.hint, text);
}

/* ---- Reading mode ---- */

static void show_entry(void)
{
    lv_obj_add_flag(s_cal, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(s_page, LV_OBJ_FLAG_HIDDEN);

    /* Coming back from writing nothing leaves a file with no text. */
    diary_remove_if_blank(s_y, s_m, s_d);

    bool same_day = s_y == s_shown_y && s_m == s_shown_m && s_d == s_shown_d;
    int32_t scroll = same_day ? lv_obj_get_scroll_y(s_page) : 0;

    diary_entry_t e = { 0 };
    int ret = diary_load(&e, s_y, s_m, s_d);
    s_shown = e.info;
    s_shown_y = s_y;
    s_shown_m = s_m;
    s_shown_d = s_d;

    struct tm t;
    char date[20];
    char title[96];
    date_to_tm(&t, s_y, s_m, s_d);
    strftime(date, sizeof(date), "%a %Y-%m-%d", &t);
    snprintf(title, sizeof(title), "Diary  %s%s", is_today(s_y, s_m, s_d) ? "Today  " : "",
             date);
    if (s_shown.tag_count) {
        strncat(title, "    Tags:", sizeof(title) - strlen(title) - 1);
        for (int i = 0; i < s_shown.tag_count; i++) {
            strncat(title, " ", sizeof(title) - strlen(title) - 1);
            strncat(title, s_shown.tags[i], sizeof(title) - strlen(title) - 1);
        }
    }
    lv_label_set_text(s_chrome.title, title);

    if (ret != DIARY_OK) {
        lv_label_set_text(s_text, "Could not read this entry.");
    } else if (e.body) {
        /* The label keeps its own copy. */
        lv_label_set_text(s_text, e.body);
        if (e.truncated) lv_label_ins_text(s_text, LV_LABEL_POS_LAST, TRUNCATED_NOTE);
    } else {
        lv_label_set_text(s_text, "Nothing written on this day yet.");
    }
    diary_entry_free(&e);

    lv_obj_update_layout(s_page);
    lv_obj_scroll_to_y(s_page, 0, LV_ANIM_OFF);
    if (scroll) lv_obj_scroll_by_bounded(s_page, 0, -scroll, LV_ANIM_OFF);
    update_hint();
}

static void scroll_page(int32_t dy)
{
    /* A positive scroll_by moves the content down, towards the top. */
    lv_obj_scroll_by_bounded(s_page, 0, -dy, LV_ANIM_OFF);
}

static void start_writing(void)
{
    if (!s_writer) {
        s_msg = "Writing is not available here.";
        return;
    }
    bool existed = s_shown.exists;
    char path[PATH_LEN];
    if (diary_create(s_y, s_m, s_d) != DIARY_OK ||
        diary_path(path, sizeof(path), s_y, s_m, s_d) != DIARY_OK) {
        s_msg = "Could not make the entry. Check the SD card.";
        return;
    }
    const char *err = s_writer(path);
    if (!err) return;       /* the editor has the display now */
    if (!existed) diary_remove_if_blank(s_y, s_m, s_d);
    s_msg = err;
}

static void finish_tag(void)
{
    if (strlen(s_tag) != DIARY_TAG_LEN) {
        s_msg = "A tag is two letters or digits.";
        return;
    }
    bool added = false;
    int ret = diary_toggle_tag(s_y, s_m, s_d, s_tag, &added);
    s_mode = DV_READ;
    if (ret == DIARY_ERR_FULL) {
        snprintf(s_msg_buf, sizeof(s_msg_buf), "An entry has at most %d tags.",
                 DIARY_MAX_TAGS);
    } else if (ret != DIARY_OK) {
        snprintf(s_msg_buf, sizeof(s_msg_buf), "Could not save the tag. Check the SD card.");
    } else {
        snprintf(s_msg_buf, sizeof(s_msg_buf), "Tag %s %s.", s_tag,
                 added ? "added" : "removed");
    }
    s_msg = s_msg_buf;
    show_entry();
}

static void key_tag(const kb_event_t *ev)
{
    size_t len = strlen(s_tag);
    switch (ev->keycode) {
    case KB_KEY_ENTER:
        finish_tag();
        return;
    case KB_KEY_ESCAPE:
        s_mode = DV_READ;
        break;
    case KB_KEY_BACKSPACE:
        if (len) s_tag[len - 1] = '\0';
        break;
    default: {
        if (ev->modifier & (KB_MOD_LCTRL | KB_MOD_RCTRL)) break;
        const char *text = kb_layout_translate(ev->keycode, ev->modifier);
        if (!text || !text[0] || text[1] || len >= DIARY_TAG_LEN) break;
        char c = text[0];
        if (c >= 'a' && c <= 'z') c = (char)(c - 'a' + 'A');
        if ((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')) {
            s_tag[len] = c;
            s_tag[len + 1] = '\0';
        }
        break;
    }
    }
    update_hint();
}

static void open_calendar(void);

/* Letter commands: no Ctrl, Alt or Win, Shift or not. */
static char command_letter(const kb_event_t *ev)
{
    if (ev->modifier & (KB_MOD_LCTRL | KB_MOD_RCTRL | KB_MOD_LALT | KB_MOD_RALT |
                        KB_MOD_LGUI | KB_MOD_RGUI)) {
        return 0;
    }
    return kb_layout_shortcut_char(ev->keycode);
}

static void key_read(const kb_event_t *ev)
{
    bool shift = (ev->modifier & (KB_MOD_LSHIFT | KB_MOD_RSHIFT)) != 0;
    int32_t line_h = lv_font_get_line_height(s_style.body_font);
    int32_t page_h = lv_obj_get_height(s_page) - line_h;

    switch (ev->keycode) {
    case KB_KEY_LEFT:
    case KB_KEY_RIGHT: {
        int dir = ev->keycode == KB_KEY_RIGHT ? 1 : -1;
        if (shift) {
            /* Any day; no file until something is written or tagged. */
            journal_date_add(&s_y, &s_m, &s_d, dir);
        } else if (diary_find_entry(&s_y, &s_m, &s_d, dir) != DIARY_OK) {
            s_msg = dir > 0 ? "No later entry.  Shift+Right: next day"
                            : "No earlier entry.  Shift+Left: previous day";
            break;
        }
        show_entry();
        return;
    }
    case KB_KEY_UP:       scroll_page(-SCROLL_LINES * line_h); break;
    case KB_KEY_DOWN:     scroll_page(SCROLL_LINES * line_h);  break;
    case KB_KEY_PAGEUP:   scroll_page(-page_h);                break;
    case KB_KEY_PAGEDOWN: scroll_page(page_h);                 break;
    case KB_KEY_HOME:     lv_obj_scroll_to_y(s_page, 0, LV_ANIM_OFF); break;
    case KB_KEY_END:
        lv_obj_scroll_by_bounded(s_page, 0, -lv_obj_get_scroll_bottom(s_page),
                                 LV_ANIM_OFF);
        break;
    case KB_KEY_ENTER:
        start_writing();
        if (!planner_ui_is_active()) return;    /* the editor took over */
        break;
    default:
        switch (command_letter(ev)) {
        case 'c':
            open_calendar();
            return;
        case 't':
            s_tag[0] = '\0';
            s_mode = DV_TAG;
            break;
        default:
            break;
        }
        break;
    }
    update_hint();
}

/* ---- Calendar ---- */

static void paint_cell(int i)
{
    int day = i - first_column(s_cy, s_cm) + 1;
    if (day < 1 || day > days_in_month(s_cy, s_cm)) {
        lv_obj_add_flag(s_cell[i], LV_OBJ_FLAG_HIDDEN);
        return;
    }
    lv_obj_remove_flag(s_cell[i], LV_OBJ_FLAG_HIDDEN);

    const diary_info_t *info = &s_info[day - 1];
    bool selected = day == s_cd;
    lv_color_t fg = selected ? s_style.bg : s_style.fg;
    lv_obj_set_style_bg_color(s_cell[i], selected ? s_style.fg : s_style.bg, 0);
    lv_obj_set_style_border_width(s_cell[i], is_today(s_cy, s_cm, day) ? 3 : 1, 0);

    char text[3 * DIARY_MAX_TAGS + 1];
    snprintf(text, sizeof(text), "%d", day);
    lv_label_set_text(s_cell_num[i], text);
    lv_obj_set_style_text_color(s_cell_num[i], fg, 0);

    text[0] = '\0';
    for (int t = 0; t < info->tag_count; t++) {
        if (t) strncat(text, " ", sizeof(text) - strlen(text) - 1);
        strncat(text, info->tags[t], sizeof(text) - strlen(text) - 1);
    }
    lv_label_set_text(s_cell_tags[i], text);
    lv_obj_set_style_text_color(s_cell_tags[i], fg, 0);

    lv_obj_set_style_line_color(s_cell_check[i], fg, 0);
    lv_obj_set_hidden(s_cell_check[i], !info->has_text);
}

static void show_calendar(void)
{
    if (s_info_y != s_cy || s_info_m != s_cm) {
        if (diary_read_month(s_cy, s_cm, s_info) != DIARY_OK) {
            s_msg = "Some entries could not be read.";
        }
        s_info_y = s_cy;
        s_info_m = s_cm;
    }

    struct tm t;
    char month[24];
    char title[48];
    date_to_tm(&t, s_cy, s_cm, 1);
    strftime(month, sizeof(month), "%B %Y", &t);
    snprintf(title, sizeof(title), "Diary  %s", month);
    lv_label_set_text(s_chrome.title, title);

    lv_obj_add_flag(s_page, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(s_cal, LV_OBJ_FLAG_HIDDEN);
    for (int i = 0; i < CAL_CELLS; i++) paint_cell(i);
    update_hint();
}

static void open_calendar(void)
{
    s_cy = s_y;
    s_cm = s_m;
    s_cd = s_d;
    s_info_y = 0;           /* entries may have changed since */
    s_mode = DV_CALENDAR;
    show_calendar();
}

/* ? forward and , back, as in the planner's paging. */
static int month_key(const kb_event_t *ev)
{
    if (ev->keycode == KB_KEY_PAGEDOWN) return 1;
    if (ev->keycode == KB_KEY_PAGEUP) return -1;
    if (ev->modifier & (KB_MOD_LCTRL | KB_MOD_RCTRL)) return 0;
    const char *text = kb_layout_translate(ev->keycode, ev->modifier);
    if (text && text[0] == '?') return 1;
    if (text && text[0] == ',') return -1;
    return 0;
}

static void back_to_reading(void)
{
    s_mode = DV_READ;
    show_entry();
}

static void key_calendar(const kb_event_t *ev)
{
    int old_y = s_cy, old_m = s_cm, old_d = s_cd;
    int dir = month_key(ev);

    if (dir) {
        s_cm += dir;
        if (s_cm < 1) { s_cm = 12; s_cy--; }
        if (s_cm > 12) { s_cm = 1; s_cy++; }
        int last = days_in_month(s_cy, s_cm);
        if (s_cd > last) s_cd = last;
    } else {
        switch (ev->keycode) {
        case KB_KEY_LEFT:  journal_date_add(&s_cy, &s_cm, &s_cd, -1); break;
        case KB_KEY_RIGHT: journal_date_add(&s_cy, &s_cm, &s_cd, 1);  break;
        case KB_KEY_UP:    journal_date_add(&s_cy, &s_cm, &s_cd, -7); break;
        case KB_KEY_DOWN:  journal_date_add(&s_cy, &s_cm, &s_cd, 7);  break;
        case KB_KEY_HOME: {
            struct tm now;
            read_clock(&now);
            s_cy = now.tm_year + 1900;
            s_cm = now.tm_mon + 1;
            s_cd = now.tm_mday;
            break;
        }
        case KB_KEY_ENTER:
            s_y = s_cy;
            s_m = s_cm;
            s_d = s_cd;
            back_to_reading();
            return;
        case KB_KEY_ESCAPE:
            back_to_reading();
            return;
        default:
            if (command_letter(ev) == 'c') {
                back_to_reading();
                return;
            }
            update_hint();
            return;
        }
    }

    if (s_cy != old_y || s_cm != old_m) {
        show_calendar();
        return;
    }
    /* Same month: repaint just the two cells that changed. */
    int col = first_column(s_cy, s_cm) - 1;
    paint_cell(col + old_d);
    paint_cell(col + s_cd);
    update_hint();
}

/* ---- Entry points ---- */

/* NULL when the clock and the card are usable, or what to show. */
static const char *check_ready(const struct tm *now)
{
    if (now->tm_year + 1900 < PLANNER_MIN_VALID_YEAR) {
        return "The clock is not set.\n\nF1 > Settings > Date and time";
    }
    if (!sd_card_is_ready()) {
        return "No SD card.\n\nInsert one and restart.";
    }
    diary_set_root(sd_card_get_mount_point());
    return NULL;
}

lv_obj_t *diary_view_render(const planner_ui_style_t *style, bool today)
{
    if (!s_scr) build_screen();
    apply_style(style);

    struct tm now;
    read_clock(&now);
    s_msg = NULL;

    const char *problem = check_ready(&now);
    s_ready = problem == NULL;
    if (problem) {
        lv_obj_add_flag(s_cal, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(s_page, LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text(s_chrome.title, "Diary");
        lv_label_set_text(s_text, problem);
        lv_label_set_text(s_chrome.hint, PLANNER_HINT_VIEWS);
        s_shown_y = 0;
        return s_scr;
    }

    if (today || s_y == 0) {
        s_y = now.tm_year + 1900;
        s_m = now.tm_mon + 1;
        s_d = now.tm_mday;
        s_mode = DV_READ;
    }
    if (s_mode == DV_CALENDAR) {
        s_info_y = 0;       /* re-read: the menu may have run a Git sync */
        show_calendar();
    } else {
        s_mode = DV_READ;
        show_entry();
    }
    return s_scr;
}

void diary_view_key(const kb_event_t *ev)
{
    if (!s_ready) return;
    s_msg = NULL;
    switch (s_mode) {
    case DV_TAG:      key_tag(ev);      break;
    case DV_CALENDAR: key_calendar(ev); break;
    default:          key_read(ev);     break;
    }
}

void diary_view_commit(void)
{
    if (s_mode == DV_TAG) s_mode = DV_READ;
}

void diary_view_set_writer(planner_diary_write_fn fn)
{
    s_writer = fn;
}
