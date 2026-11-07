#include "planner_ui.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "journal.h"
#include "kb_layout.h"
#include "sd_card.h"

#define MIN_VALID_YEAR 2025
#define BODY_TOP       30
#define ROW_MAX        20

/* The Week grid: Mon-Thu down the left column, Fri-Sun and the weekly
 * page down the right. */
#define GRID_BOXES     8
#define GRID_PER_COL   4
#define GRID_WEEKLY    7
#define GRID_ROW_MAX   6

#define HINT_VIEWS  "F1:Menu  F2:Today  F3:Week  F4:Month  F5:Notes"
#define HINT_LIST   "Knob:move  Enter:edit  Space:done  Bksp:close  Tab:type"
#define HINT_LIST_BACK  HINT_LIST "  Esc:week"
#define HINT_EMPTY  "Knob down or Enter: new bullet"
#define HINT_EMPTY_BACK HINT_EMPTY "  Esc:week"
#define HINT_GRID   "Knob:move  Enter:open  F1:Menu"
#define HINT_EDIT   "Enter:finish  Esc:cancel  Tab:type"
#define HINT_CLOSE  "Knob:move  Enter:pick  Esc:back"
#define HINT_DATE   "Knob:day  ? or ,:week  Enter:migrate  Esc:back"
#define HINT_DELETE "Enter:delete  Esc:keep"

typedef enum {
    MODE_LIST,
    MODE_EDIT,
    MODE_CLOSEOUT,
    MODE_DATE,
    MODE_DELETE,
} today_mode_t;

typedef enum {
    CLOSE_CANCEL,
    CLOSE_MIGRATE,
    CLOSE_REOPEN,
    CLOSE_BACK,
} close_item_t;

static lv_obj_t *s_scr;
static lv_obj_t *s_lbl_title;
static lv_obj_t *s_lbl_body;
static lv_obj_t *s_lbl_hint;
static lv_obj_t *s_rows[ROW_MAX];
static int s_visible_rows;
static planner_ui_style_t s_style;
static bool s_active;
static planner_view_t s_view = PLANNER_VIEW_TODAY;

/* Week grid. s_grid is the parent of every box, so one flag hides it. */
static lv_obj_t *s_grid;
static lv_obj_t *s_box[GRID_BOXES];
static lv_obj_t *s_box_title[GRID_BOXES];
static lv_obj_t *s_box_row[GRID_BOXES][GRID_ROW_MAX];
static int s_box_rows;              /* item lines that fit in a box */
static int s_grid_sel;
static int s_week_year, s_week_month, s_week_day;   /* its Monday */

/* The page on display: today's, or the one opened from the grid. s_day
 * is NULL until first needed, and the grid borrows it to read each
 * file; s_day_ok says it holds the page and may be changed and saved. */
static int s_open_year, s_open_month, s_open_day;
static journal_day_t *s_day;
static bool s_day_ok;
static today_mode_t s_mode = MODE_LIST;
static int s_sel;
static int s_top;
static const char *s_msg;           /* replaces the hint until the next key */
static char s_edit[JOURNAL_TEXT_MAX];
static bool s_edit_is_new;
static journal_kind_t s_edit_old_kind;
static close_item_t s_close_items[3];
static int s_close_count;
static int s_close_sel;
static int s_mig_year, s_mig_month, s_mig_day;

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

    for (int i = 0; i < ROW_MAX; i++) {
        s_rows[i] = lv_label_create(s_scr);
        lv_label_set_long_mode(s_rows[i], LV_LABEL_LONG_DOT);
        lv_obj_set_style_bg_opa(s_rows[i], LV_OPA_COVER, 0);
        lv_obj_set_style_pad_left(s_rows[i], 2, 0);
    }

    s_grid = lv_obj_create(s_scr);
    lv_obj_remove_style_all(s_grid);
    lv_obj_remove_flag(s_grid, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_grid, LV_OBJ_FLAG_HIDDEN);
    for (int i = 0; i < GRID_BOXES; i++) {
        s_box[i] = lv_obj_create(s_grid);
        lv_obj_remove_style_all(s_box[i]);
        lv_obj_remove_flag(s_box[i], LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_bg_opa(s_box[i], LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(s_box[i], 1, 0);
        s_box_title[i] = lv_label_create(s_box[i]);
        lv_label_set_long_mode(s_box_title[i], LV_LABEL_LONG_CLIP);
        for (int r = 0; r < GRID_ROW_MAX; r++) {
            s_box_row[i][r] = lv_label_create(s_box[i]);
            lv_label_set_long_mode(s_box_row[i][r], LV_LABEL_LONG_DOT);
        }
    }

    s_lbl_hint = lv_label_create(s_scr);
    lv_obj_set_width(s_lbl_hint, w - 4);
    lv_label_set_long_mode(s_lbl_hint, LV_LABEL_LONG_CLIP);
    lv_obj_align(s_lbl_hint, LV_ALIGN_BOTTOM_LEFT, 2, -2);
}

static void apply_style(const planner_ui_style_t *style)
{
    s_style = *style;
    lv_obj_set_style_bg_color(s_scr, style->bg, 0);
    lv_obj_set_style_text_font(s_lbl_title, style->title_font, 0);
    lv_obj_set_style_text_font(s_lbl_body, style->body_font, 0);
    lv_obj_set_style_text_font(s_lbl_hint, style->hint_font, 0);
    lv_obj_set_style_text_color(s_lbl_title, style->fg, 0);
    lv_obj_set_style_text_color(s_lbl_body, style->fg, 0);
    lv_obj_set_style_text_color(s_lbl_hint, style->fg, 0);

    int32_t w = lv_display_get_horizontal_resolution(lv_display_get_default());
    int32_t h = lv_display_get_vertical_resolution(lv_display_get_default());
    int32_t row_h = lv_font_get_line_height(style->body_font) + 3;
    int32_t room = h - BODY_TOP - lv_font_get_line_height(style->hint_font) - 6;
    s_visible_rows = (int)(room / row_h);
    if (s_visible_rows > ROW_MAX) s_visible_rows = ROW_MAX;
    if (s_visible_rows < 1) s_visible_rows = 1;

    for (int i = 0; i < ROW_MAX; i++) {
        lv_obj_set_style_text_font(s_rows[i], style->body_font, 0);
        lv_obj_set_pos(s_rows[i], 4, BODY_TOP + i * row_h);
        lv_obj_set_size(s_rows[i], w - 8, row_h);
    }

    /* The boxes use the small font: it is what fits a useful number of
     * items in an eighth of the screen. */
    int32_t box_w = (w - 12) / 2;
    int32_t box_h = room / GRID_PER_COL;
    int32_t line_h = lv_font_get_line_height(style->hint_font) + 1;
    s_box_rows = (int)((box_h - 3) / line_h) - 1;
    if (s_box_rows > GRID_ROW_MAX) s_box_rows = GRID_ROW_MAX;
    if (s_box_rows < 1) s_box_rows = 1;

    lv_obj_set_pos(s_grid, 4, BODY_TOP - 2);
    lv_obj_set_size(s_grid, w - 8, box_h * GRID_PER_COL);
    for (int i = 0; i < GRID_BOXES; i++) {
        lv_obj_set_pos(s_box[i], (i / GRID_PER_COL) * (box_w + 4),
                       (i % GRID_PER_COL) * box_h);
        lv_obj_set_size(s_box[i], box_w, box_h - 1);
        lv_obj_set_style_border_color(s_box[i], style->fg, 0);
        lv_obj_set_style_text_font(s_box_title[i], style->hint_font, 0);
        lv_obj_set_pos(s_box_title[i], 2, 0);
        lv_obj_set_size(s_box_title[i], box_w - 6, line_h);
        for (int r = 0; r < GRID_ROW_MAX; r++) {
            lv_obj_set_style_text_font(s_box_row[i][r], style->hint_font, 0);
            lv_obj_set_pos(s_box_row[i][r], 2, (r + 1) * line_h);
            lv_obj_set_size(s_box_row[i][r], box_w - 6, line_h);
        }
    }
}

static void set_row(int i, const char *text, bool selected)
{
    lv_obj_remove_flag(s_rows[i], LV_OBJ_FLAG_HIDDEN);
    lv_label_set_text(s_rows[i], text);
    lv_obj_set_style_bg_color(s_rows[i], selected ? s_style.fg : s_style.bg, 0);
    lv_obj_set_style_text_color(s_rows[i], selected ? s_style.bg : s_style.fg, 0);
}

static void hide_rows_from(int first)
{
    for (int i = first; i < ROW_MAX; i++) {
        lv_obj_add_flag(s_rows[i], LV_OBJ_FLAG_HIDDEN);
    }
}

static void show_body(const char *text, const char *hint)
{
    lv_obj_add_flag(s_grid, LV_OBJ_FLAG_HIDDEN);
    hide_rows_from(0);
    lv_obj_remove_flag(s_lbl_body, LV_OBJ_FLAG_HIDDEN);
    lv_label_set_text(s_lbl_body, text);
    lv_label_set_text(s_lbl_hint, hint);
}

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

static void read_clock(struct tm *now)
{
    time_t t = time(NULL);
    localtime_r(&t, now);
}

/* Day and weekly pages opened from the Week grid: Esc goes back to it. */
static bool opened_from_grid(void)
{
    return s_view == PLANNER_VIEW_DAY || s_view == PLANNER_VIEW_WEEKLY;
}

/* ---- Bullet pages: Today, and a day or week opened from the grid ---- */

static const char *bullet_prefix(journal_kind_t kind)
{
    switch (kind) {
    case JOURNAL_TASK:      return "[ ] ";
    case JOURNAL_DONE:      return "[x] ";
    case JOURNAL_CANCELLED: return "[-] ";
    case JOURNAL_MIGRATED:  return "[>] ";
    case JOURNAL_EVENT:     return " o  ";
    case JOURNAL_NOTE:      return " -  ";
    case JOURNAL_TRACKER:   return " !  ";
    default:                return "";
    }
}

/* The end of the text being typed, sized to fit the row. */
static const char *edit_tail(size_t prefix_len)
{
    int32_t w = lv_display_get_horizontal_resolution(lv_display_get_default());
    int32_t glyph = lv_font_get_glyph_width(s_style.body_font, 'M', 0);
    size_t fit = (size_t)((w - 16) / (glyph > 0 ? glyph : 8));
    fit = fit > prefix_len + 1 ? fit - prefix_len - 1 : 1;

    size_t len = strlen(s_edit);
    if (len <= fit) return s_edit;
    const char *p = s_edit + len - fit;
    while ((*p & 0xC0) == 0x80) p++;    /* not mid UTF-8 character */
    return p;
}

static void render_bullets(void)
{
    char line[JOURNAL_TEXT_MAX + 32];

    if (s_day->count == 0) {
        set_row(0, "Nothing here yet.", false);
        hide_rows_from(1);
        lv_label_set_text(s_lbl_hint,
                          opened_from_grid() ? HINT_EMPTY_BACK : HINT_EMPTY);
        return;
    }

    if (s_sel < s_top) s_top = s_sel;
    if (s_sel >= s_top + s_visible_rows) s_top = s_sel - s_visible_rows + 1;

    int row = 0;
    for (int i = s_top; i < s_day->count && row < s_visible_rows; i++, row++) {
        const journal_bullet_t *b = &s_day->bullets[i];
        const char *prefix = bullet_prefix(b->kind);
        if (s_mode == MODE_EDIT && i == s_sel) {
            snprintf(line, sizeof(line), "%s%s_", prefix,
                     edit_tail(strlen(prefix)));
        } else if (b->kind == JOURNAL_MIGRATED && b->moved_to[0]) {
            snprintf(line, sizeof(line), "%s%s -> %s", prefix, b->text,
                     b->moved_to + 5);
        } else {
            snprintf(line, sizeof(line), "%s%s", prefix, b->text);
        }
        set_row(row, line, i == s_sel);
    }
    hide_rows_from(row);
    lv_label_set_text(s_lbl_hint,
                      s_mode == MODE_EDIT ? HINT_EDIT :
                      opened_from_grid()  ? HINT_LIST_BACK : HINT_LIST);
}

static void render_closeout(void)
{
    static const char *const labels[] = {
        [CLOSE_CANCEL]  = "Cancel it",
        [CLOSE_MIGRATE] = "Migrate to a date...",
        [CLOSE_REOPEN]  = "Reopen it",
        [CLOSE_BACK]    = "Back",
    };
    char line[JOURNAL_TEXT_MAX + 8];

    snprintf(line, sizeof(line), "%s", s_day->bullets[s_sel].text);
    set_row(0, line, false);
    for (int i = 0; i < s_close_count; i++) {
        set_row(i + 1, labels[s_close_items[i]], i == s_close_sel);
    }
    hide_rows_from(s_close_count + 1);
    lv_label_set_text(s_lbl_hint, HINT_CLOSE);
}

static void render_date(void)
{
    struct tm t;
    char line[32];

    date_to_tm(&t, s_mig_year, s_mig_month, s_mig_day);
    strftime(line, sizeof(line), "%a %Y-%m-%d", &t);
    set_row(0, "Migrate to:", false);
    set_row(1, line, true);
    hide_rows_from(2);
    lv_label_set_text(s_lbl_hint, HINT_DATE);
}

static void render_delete(void)
{
    set_row(0, "Delete this bullet?", false);
    set_row(1, s_day->bullets[s_sel].text, true);
    hide_rows_from(2);
    lv_label_set_text(s_lbl_hint, HINT_DELETE);
}

static void format_page_title(char *out, size_t out_sz)
{
    struct tm t;
    char date[20];

    date_to_tm(&t, s_day->year, s_day->month, s_day->day);
    strftime(date, sizeof(date), "%a %Y-%m-%d", &t);
    if (s_day->is_week) {
        struct tm sunday;
        char last[20];
        date_to_tm(&sunday, s_day->year, s_day->month, s_day->day + 6);
        strftime(last, sizeof(last), "%a %Y-%m-%d", &sunday);
        snprintf(out, out_sz, "%s - %s", date, last);
    } else {
        snprintf(out, out_sz, "%s%s",
                 s_view == PLANNER_VIEW_TODAY ? "Today  " : "", date);
    }
}

static void render_page(void)
{
    if (!s_day_ok) return;

    char title[48];
    format_page_title(title, sizeof(title) - 6);
    if (kb_layout_caps()) strcat(title, "  CAPS");
    lv_label_set_text(s_lbl_title, title);
    lv_obj_add_flag(s_lbl_body, LV_OBJ_FLAG_HIDDEN);

    switch (s_mode) {
    case MODE_CLOSEOUT: render_closeout(); break;
    case MODE_DATE:     render_date();     break;
    case MODE_DELETE:   render_delete();   break;
    default:            render_bullets();  break;
    }
    if (s_msg) lv_label_set_text(s_lbl_hint, s_msg);
}

static void save_day(void)
{
    if (journal_save(s_day) != JOURNAL_OK) {
        s_msg = "Save failed. Check the SD card.";
    }
}

static void clamp_sel(void)
{
    if (s_sel >= s_day->count) s_sel = s_day->count - 1;
    if (s_sel < 0) s_sel = 0;
}

static void remove_bullet(int idx)
{
    memmove(&s_day->bullets[idx], &s_day->bullets[idx + 1],
            (size_t)(s_day->count - idx - 1) * sizeof(s_day->bullets[0]));
    s_day->count--;
    clamp_sel();
}

static void start_new_bullet(void)
{
    if (s_day->count == JOURNAL_MAX_BULLETS) {
        s_msg = "This page is full.";
        return;
    }
    journal_bullet_t *b = &s_day->bullets[s_day->count];
    memset(b, 0, sizeof(*b));
    b->kind = JOURNAL_TASK;
    s_sel = s_day->count++;
    s_edit[0] = '\0';
    s_edit_is_new = true;
    s_edit_old_kind = JOURNAL_TASK;
    s_mode = MODE_EDIT;
}

static bool is_editable(journal_kind_t kind)
{
    return kind != JOURNAL_MIGRATED && kind != JOURNAL_RAW;
}

static void start_edit(void)
{
    journal_bullet_t *b = &s_day->bullets[s_sel];
    if (!is_editable(b->kind)) return;
    snprintf(s_edit, sizeof(s_edit), "%s", b->text);
    s_edit_is_new = false;
    s_edit_old_kind = b->kind;
    s_mode = MODE_EDIT;
}

static void cancel_edit(void)
{
    if (s_edit_is_new) {
        remove_bullet(s_sel);
    } else {
        s_day->bullets[s_sel].kind = s_edit_old_kind;
    }
    s_mode = MODE_LIST;
}

/* Returns false when there was no text, so nothing was kept. */
static bool finish_edit(void)
{
    if (!s_edit[0]) {
        cancel_edit();
        return false;
    }
    snprintf(s_day->bullets[s_sel].text, JOURNAL_TEXT_MAX, "%s", s_edit);
    s_mode = MODE_LIST;
    save_day();
    return true;
}

static void cycle_kind(journal_bullet_t *b)
{
    switch (b->kind) {
    case JOURNAL_TASK:    b->kind = JOURNAL_EVENT;   break;
    case JOURNAL_EVENT:   b->kind = JOURNAL_NOTE;    break;
    case JOURNAL_NOTE:    b->kind = JOURNAL_TRACKER; break;
    case JOURNAL_TRACKER: b->kind = JOURNAL_TASK;    break;
    default: break;
    }
}

static void open_closeout(void)
{
    s_close_count = 0;
    switch (s_day->bullets[s_sel].kind) {
    case JOURNAL_TASK:
        s_close_items[s_close_count++] = CLOSE_CANCEL;
        s_close_items[s_close_count++] = CLOSE_MIGRATE;
        break;
    case JOURNAL_EVENT:
        s_close_items[s_close_count++] = CLOSE_MIGRATE;
        break;
    case JOURNAL_DONE:
    case JOURNAL_CANCELLED:
        s_close_items[s_close_count++] = CLOSE_REOPEN;
        break;
    default:
        s_msg = "Nothing to close out here.";
        return;
    }
    s_close_items[s_close_count++] = CLOSE_BACK;
    s_close_sel = 0;
    s_mode = MODE_CLOSEOUT;
}

/* A day's bullet starts at the next day. A weekly item starts at today,
 * or at the Monday of a week that is not this one. */
static void start_migrate_date(void)
{
    s_mig_year = s_day->year;
    s_mig_month = s_day->month;
    s_mig_day = s_day->day;
    if (!s_day->is_week) {
        journal_date_add(&s_mig_year, &s_mig_month, &s_mig_day, 1);
        return;
    }

    struct tm now;
    read_clock(&now);
    int y = now.tm_year + 1900, m = now.tm_mon + 1, d = now.tm_mday;
    int wy = y, wm = m, wd = d;
    journal_week_start(&wy, &wm, &wd);
    if (wy == s_day->year && wm == s_day->month && wd == s_day->day) {
        s_mig_year = y;
        s_mig_month = m;
        s_mig_day = d;
    }
}

static void pick_closeout(void)
{
    journal_bullet_t *b = &s_day->bullets[s_sel];
    s_mode = MODE_LIST;
    switch (s_close_items[s_close_sel]) {
    case CLOSE_CANCEL:
        b->kind = JOURNAL_CANCELLED;
        save_day();
        break;
    case CLOSE_REOPEN:
        b->kind = JOURNAL_TASK;
        save_day();
        break;
    case CLOSE_MIGRATE:
        start_migrate_date();
        s_mode = MODE_DATE;
        break;
    default:
        break;
    }
}

static void migrate_bullet(void)
{
    int ret = journal_migrate(s_day, s_sel, s_mig_year, s_mig_month, s_mig_day);
    if (ret == JOURNAL_ERR_ARG) {
        s_msg = "Pick a different day.";
        return;
    }
    s_mode = MODE_LIST;
    if (ret == JOURNAL_ERR_FULL)         s_msg = "That day is full.";
    else if (ret == JOURNAL_ERR_TOO_BIG) s_msg = "That day's file is too big.";
    else if (ret != JOURNAL_OK)          s_msg = "Migrate failed. Check the SD card.";
}

static bool ctrl_held(const kb_event_t *ev)
{
    return (ev->modifier & (KB_MOD_LCTRL | KB_MOD_RCTRL)) != 0;
}

static void edit_backspace(bool whole_word)
{
    size_t len = strlen(s_edit);
    if (whole_word) {
        while (len && s_edit[len - 1] == ' ') len--;
        while (len && s_edit[len - 1] != ' ') len--;
    } else {
        while (len && (s_edit[len - 1] & 0xC0) == 0x80) len--;
        if (len) len--;
    }
    s_edit[len] = '\0';
}

static void key_edit(const kb_event_t *ev)
{
    switch (ev->keycode) {
    case KB_KEY_ENTER:
        finish_edit();
        return;
    case KB_KEY_ESCAPE:
        cancel_edit();
        return;
    case KB_KEY_UP:
        if (finish_edit() && s_sel > 0) s_sel--;
        return;
    case KB_KEY_DOWN:
        /* Keep going: knob down from the last bullet starts the next. */
        if (finish_edit()) {
            if (s_sel < s_day->count - 1) s_sel++;
            else start_new_bullet();
        }
        return;
    case KB_KEY_TAB:
        cycle_kind(&s_day->bullets[s_sel]);
        return;
    case KB_KEY_BACKSPACE:
        edit_backspace(ctrl_held(ev));
        return;
    default:
        break;
    }

    if (ctrl_held(ev)) return;
    const char *text = kb_layout_translate(ev->keycode, ev->modifier);
    if (!text || (unsigned char)text[0] < 0x20) return;
    size_t len = strlen(s_edit);
    size_t add = strlen(text);
    if (len + add < sizeof(s_edit)) memcpy(s_edit + len, text, add + 1);
}

static void key_list(const kb_event_t *ev)
{
    bool empty = s_day->count == 0;

    switch (ev->keycode) {
    case KB_KEY_UP:
        if (s_sel > 0) s_sel--;
        break;
    case KB_KEY_DOWN:
        if (s_sel < s_day->count - 1) s_sel++;
        else start_new_bullet();
        break;
    case KB_KEY_ENTER:
        if (empty) start_new_bullet();
        else start_edit();
        break;
    case KB_KEY_SPACE: {
        if (empty) break;
        journal_bullet_t *b = &s_day->bullets[s_sel];
        if (b->kind == JOURNAL_TASK) b->kind = JOURNAL_DONE;
        else if (b->kind == JOURNAL_DONE) b->kind = JOURNAL_TASK;
        else break;
        save_day();
        break;
    }
    case KB_KEY_TAB:
        if (empty) break;
        cycle_kind(&s_day->bullets[s_sel]);
        save_day();
        break;
    case KB_KEY_BACKSPACE:
        if (empty) break;
        if (ctrl_held(ev)) s_mode = MODE_DELETE;
        else open_closeout();
        break;
    case KB_KEY_DELETE:
        if (!empty) s_mode = MODE_DELETE;
        break;
    default:
        break;
    }
}

static void key_closeout(const kb_event_t *ev)
{
    switch (ev->keycode) {
    case KB_KEY_UP:
        if (s_close_sel > 0) s_close_sel--;
        break;
    case KB_KEY_DOWN:
        if (s_close_sel < s_close_count - 1) s_close_sel++;
        break;
    case KB_KEY_ENTER:
        pick_closeout();
        break;
    case KB_KEY_ESCAPE:
    case KB_KEY_BACKSPACE:
        s_mode = MODE_LIST;
        break;
    default:
        break;
    }
}

static void key_date(const kb_event_t *ev)
{
    int delta = 0;

    switch (ev->keycode) {
    case KB_KEY_UP:   delta = -1; break;
    case KB_KEY_DOWN: delta = 1;  break;
    case KB_KEY_ENTER:
        migrate_bullet();
        return;
    case KB_KEY_ESCAPE:
    case KB_KEY_BACKSPACE:
        s_mode = MODE_LIST;
        return;
    default: {
        const char *text = kb_layout_translate(ev->keycode, ev->modifier);
        if (text && text[0] == '?') delta = 7;
        else if (text && text[0] == ',') delta = -7;
        break;
    }
    }
    if (delta) journal_date_add(&s_mig_year, &s_mig_month, &s_mig_day, delta);
}

static void key_delete(const kb_event_t *ev)
{
    if (ev->keycode == KB_KEY_ENTER) {
        remove_bullet(s_sel);
        save_day();
        s_mode = MODE_LIST;
    } else if (ev->keycode == KB_KEY_ESCAPE) {
        s_mode = MODE_LIST;
    }
}

/* Returns NULL when the clock and the card are usable and s_day is
 * allocated, or what to show instead. */
static const char *check_ready(const struct tm *now)
{
    if (now->tm_year + 1900 < MIN_VALID_YEAR) {
        return "The clock is not set.\n\nF1 > Settings > Date and time";
    }
    if (!sd_card_is_ready()) {
        return "No SD card.\n\nInsert one and restart.";
    }
    if (!s_day) {
        s_day = calloc(1, sizeof(*s_day));
        if (!s_day) return "Out of memory.";
    }
    journal_set_root(sd_card_get_mount_point());
    return NULL;
}

/* Returns NULL when ready, or what to show instead of the page. */
static const char *load_page(const struct tm *now, int y, int m, int d,
                             bool is_week)
{
    s_day_ok = false;
    s_msg = NULL;

    const char *problem = check_ready(now);
    if (problem) return problem;

    if (s_day->year != y || s_day->month != m || s_day->day != d ||
        s_day->is_week != is_week) {
        s_sel = 0;
        s_top = 0;
    }

    int ret = is_week ? journal_load_week(s_day, y, m, d)
                      : journal_load(s_day, y, m, d);
    if (ret == JOURNAL_ERR_TOO_BIG) {
        return "This file is too big to show here.\n\nOpen it in Notes.";
    }
    if (ret != JOURNAL_OK) return "Could not read this file.";

    s_day_ok = true;
    clamp_sel();
    return NULL;
}

/* Show the page s_view and s_open_* name. */
static void show_page(const struct tm *now, bool create_file)
{
    lv_obj_add_flag(s_grid, LV_OBJ_FLAG_HIDDEN);

    const char *problem = load_page(now, s_open_year, s_open_month, s_open_day,
                                    s_view == PLANNER_VIEW_WEEKLY);
    if (problem) {
        show_body(problem, HINT_VIEWS);
        return;
    }
    if (create_file && journal_create(s_day) != JOURNAL_OK) {
        s_msg = "Could not create the file. Check the SD card.";
    }
    render_page();
}

/* ---- Week grid ---- */

static bool shows_in_grid(journal_kind_t kind)
{
    return kind == JOURNAL_TASK || kind == JOURNAL_DONE ||
           kind == JOURNAL_EVENT || kind == JOURNAL_TRACKER;
}

static void paint_box(int idx)
{
    bool selected = idx == s_grid_sel;
    lv_color_t fg = selected ? s_style.bg : s_style.fg;

    lv_obj_set_style_bg_color(s_box[idx], selected ? s_style.fg : s_style.bg, 0);
    lv_obj_set_style_text_color(s_box_title[idx], fg, 0);
    for (int r = 0; r < GRID_ROW_MAX; r++) {
        lv_obj_set_style_text_color(s_box_row[idx][r], fg, 0);
    }
}

/* List what s_day holds in a box. When it does not all fit, the last
 * line says how much is left out. */
static void fill_box_items(int idx)
{
    char line[JOURNAL_TEXT_MAX + 8];
    int total = 0;
    int row = 0;

    for (int i = 0; i < s_day->count; i++) {
        if (shows_in_grid(s_day->bullets[i].kind)) total++;
    }
    int room = total > s_box_rows ? s_box_rows - 1 : total;

    for (int i = 0; i < s_day->count && row < room; i++) {
        const journal_bullet_t *b = &s_day->bullets[i];
        if (!shows_in_grid(b->kind)) continue;
        snprintf(line, sizeof(line), "%s%s", bullet_prefix(b->kind), b->text);
        lv_label_set_text(s_box_row[idx][row++], line);
    }
    if (total > room) {
        snprintf(line, sizeof(line), "+%d more", total - room);
        lv_label_set_text(s_box_row[idx][row++], line);
    }
    while (row < GRID_ROW_MAX) lv_label_set_text(s_box_row[idx][row++], "");
}

static void fill_box(int idx, const char *title, int load_result)
{
    lv_label_set_text(s_box_title[idx], title);
    fill_box_items(idx);    /* a failed load leaves nothing to list */
    if (load_result != JOURNAL_OK) {
        lv_label_set_text(s_box_row[idx][0],
                          load_result == JOURNAL_ERR_TOO_BIG
                              ? "Too big. Open it in Notes."
                              : "Could not read this file.");
    }
    paint_box(idx);
}

/* keep_sel: stay on the same box, if this is still the same week. */
static void show_grid(const struct tm *now, bool keep_sel)
{
    lv_label_set_text(s_lbl_title, "Week");

    const char *problem = check_ready(now);
    if (problem) {
        show_body(problem, HINT_VIEWS);
        return;
    }

    int y = now->tm_year + 1900, m = now->tm_mon + 1, d = now->tm_mday;
    int today = (now->tm_wday + 6) % 7;
    journal_week_start(&y, &m, &d);
    if (!keep_sel || y != s_week_year || m != s_week_month || d != s_week_day) {
        s_grid_sel = today;
    }
    s_week_year = y;
    s_week_month = m;
    s_week_day = d;

    /* s_day is only a place to read into here. Loading the weekly page
     * last leaves no day that a later page could mistake for its own. */
    s_day_ok = false;
    for (int i = 0; i < GRID_WEEKLY; i++) {
        struct tm t;
        char date[16];
        char title[20];
        int by = y, bm = m, bd = d;

        journal_date_add(&by, &bm, &bd, i);
        date_to_tm(&t, by, bm, bd);
        strftime(date, sizeof(date), "%a %m-%d", &t);
        snprintf(title, sizeof(title), "%s%s", i == today ? "> " : "", date);
        fill_box(i, title, journal_load(s_day, by, bm, bd));
    }
    fill_box(GRID_WEEKLY, "This week", journal_load_week(s_day, y, m, d));

    hide_rows_from(0);
    lv_obj_add_flag(s_lbl_body, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(s_grid, LV_OBJ_FLAG_HIDDEN);
    lv_label_set_text(s_lbl_hint, HINT_GRID);
}

static void open_box(int idx)
{
    struct tm now;
    read_clock(&now);

    s_open_year = s_week_year;
    s_open_month = s_week_month;
    s_open_day = s_week_day;
    if (idx == GRID_WEEKLY) {
        s_view = PLANNER_VIEW_WEEKLY;
    } else {
        s_view = PLANNER_VIEW_DAY;
        journal_date_add(&s_open_year, &s_open_month, &s_open_day, idx);
    }
    s_sel = 0;
    s_top = 0;
    s_mode = MODE_LIST;
    show_page(&now, true);
}

static void key_grid(const kb_event_t *ev)
{
    int old = s_grid_sel;

    switch (ev->keycode) {
    case KB_KEY_UP:
        s_grid_sel = (s_grid_sel + GRID_BOXES - 1) % GRID_BOXES;
        break;
    case KB_KEY_DOWN:
        s_grid_sel = (s_grid_sel + 1) % GRID_BOXES;
        break;
    case KB_KEY_ENTER:
        open_box(s_grid_sel);
        return;
    default:
        return;
    }
    paint_box(old);
    paint_box(s_grid_sel);
}

static void back_to_grid(void)
{
    struct tm now;
    read_clock(&now);
    s_view = PLANNER_VIEW_WEEK;
    show_grid(&now, true);
}

void planner_ui_handle_key(const kb_event_t *ev)
{
    if (!s_active) return;

    if (s_view == PLANNER_VIEW_WEEK) {
        /* No boxes up: the clock or the card stopped it. */
        if (!lv_obj_has_flag(s_grid, LV_OBJ_FLAG_HIDDEN)) key_grid(ev);
        return;
    }
    if (opened_from_grid() && s_mode == MODE_LIST &&
        ev->keycode == KB_KEY_ESCAPE) {
        back_to_grid();
        return;
    }
    if (s_view == PLANNER_VIEW_MONTH || !s_day_ok) return;

    s_msg = NULL;
    switch (s_mode) {
    case MODE_EDIT:     key_edit(ev);     break;
    case MODE_CLOSEOUT: key_closeout(ev); break;
    case MODE_DATE:     key_date(ev);     break;
    case MODE_DELETE:   key_delete(ev);   break;
    default:            key_list(ev);     break;
    }
    render_page();
}

void planner_ui_commit_pending(void)
{
    if (s_day_ok && s_mode == MODE_EDIT) finish_edit();
    s_mode = MODE_LIST;
}

/* ---- Month placeholder ---- */

static void fill_month(const struct tm *now, char *out, size_t out_sz)
{
    char date[32];
    strftime(date, sizeof(date), "%B %Y", now);
    snprintf(out, out_sz, "%s\n\nTracker symbols will go here.", date);
}

lv_obj_t *planner_ui_render(planner_view_t view, const planner_ui_style_t *style)
{
    if (!s_scr) build_screen();
    apply_style(style);
    planner_ui_commit_pending();

    struct tm now;
    read_clock(&now);

    /* Coming back from a page or the menu, not arriving from elsewhere. */
    bool keep_sel = s_active && (s_view == PLANNER_VIEW_WEEK ||
                                 opened_from_grid());

    s_view = view;
    s_active = true;

    switch (view) {
    case PLANNER_VIEW_WEEK:
        show_grid(&now, keep_sel);
        break;
    case PLANNER_VIEW_TODAY:
        s_open_year = now.tm_year + 1900;
        s_open_month = now.tm_mon + 1;
        s_open_day = now.tm_mday;
        lv_label_set_text(s_lbl_title, "Today");
        show_page(&now, false);
        break;
    case PLANNER_VIEW_DAY:
    case PLANNER_VIEW_WEEKLY:
        show_page(&now, false);
        break;
    default: {
        char body[192];
        lv_label_set_text(s_lbl_title, "Month");
        if (now.tm_year + 1900 < MIN_VALID_YEAR) {
            snprintf(body, sizeof(body),
                     "The clock is not set.\n\nF1 > Settings > Date and time");
        } else {
            fill_month(&now, body, sizeof(body));
        }
        show_body(body, HINT_VIEWS);
        break;
    }
    }
    return s_scr;
}

bool planner_ui_is_active(void) { return s_active; }
planner_view_t planner_ui_current(void) { return s_view; }
void planner_ui_deactivate(void) { s_active = false; }
