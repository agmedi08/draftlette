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

#define HINT_VIEWS  "F1:Menu  F2:Today  F3:Week  F4:Month  F5:Notes"
#define HINT_LIST   "Knob:move  Enter:edit  Space:done  Bksp:close  Tab:type"
#define HINT_EMPTY  "Knob down or Enter: new bullet"
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

/* Today. s_day is NULL until first shown; s_day_ok says it was loaded
 * and may be changed and saved. */
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

/* ---- Today ---- */

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
        lv_label_set_text(s_lbl_hint, HINT_EMPTY);
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
    lv_label_set_text(s_lbl_hint, s_mode == MODE_EDIT ? HINT_EDIT : HINT_LIST);
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

static void render_today(void)
{
    if (!s_day_ok) return;

    struct tm t;
    char title[40];
    date_to_tm(&t, s_day->year, s_day->month, s_day->day);
    strftime(title, sizeof(title), "Today  %a %Y-%m-%d", &t);
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
        s_msg = "This day is full.";
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
        s_mig_year = s_day->year;
        s_mig_month = s_day->month;
        s_mig_day = s_day->day;
        journal_date_add(&s_mig_year, &s_mig_month, &s_mig_day, 1);
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

void planner_ui_handle_key(const kb_event_t *ev)
{
    if (!s_active || s_view != PLANNER_VIEW_TODAY || !s_day_ok) return;

    s_msg = NULL;
    switch (s_mode) {
    case MODE_EDIT:     key_edit(ev);     break;
    case MODE_CLOSEOUT: key_closeout(ev); break;
    case MODE_DATE:     key_date(ev);     break;
    case MODE_DELETE:   key_delete(ev);   break;
    default:            key_list(ev);     break;
    }
    render_today();
}

void planner_ui_commit_pending(void)
{
    if (s_day_ok && s_mode == MODE_EDIT) finish_edit();
    s_mode = MODE_LIST;
}

/* Returns NULL when ready, or what to show instead of the day. */
static const char *load_today(const struct tm *now)
{
    s_day_ok = false;
    s_msg = NULL;

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

    int y = now->tm_year + 1900, m = now->tm_mon + 1, d = now->tm_mday;
    if (s_day->year != y || s_day->month != m || s_day->day != d) {
        s_sel = 0;
        s_top = 0;
    }

    journal_set_root(sd_card_get_mount_point());
    int ret = journal_load(s_day, y, m, d);
    if (ret == JOURNAL_ERR_TOO_BIG) {
        return "Today's file is too big to show here.\n\nOpen it in Notes.";
    }
    if (ret != JOURNAL_OK) return "Could not read today's file.";

    s_day_ok = true;
    clamp_sel();
    return NULL;
}

/* ---- Week and Month placeholders ---- */

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
    planner_ui_commit_pending();

    time_t t = time(NULL);
    struct tm now;
    localtime_r(&t, &now);

    s_view = view;
    s_active = true;
    lv_label_set_text(s_lbl_title, titles[view]);

    if (view == PLANNER_VIEW_TODAY) {
        const char *problem = load_today(&now);
        if (problem) show_body(problem, HINT_VIEWS);
        else render_today();
        return s_scr;
    }

    char body[192];
    if (now.tm_year + 1900 < MIN_VALID_YEAR) {
        snprintf(body, sizeof(body),
                 "The clock is not set.\n\nF1 > Settings > Date and time");
    } else if (view == PLANNER_VIEW_WEEK) {
        fill_week(&now, body, sizeof(body));
    } else {
        fill_month(&now, body, sizeof(body));
    }
    show_body(body, HINT_VIEWS);
    return s_scr;
}

bool planner_ui_is_active(void) { return s_active; }
planner_view_t planner_ui_current(void) { return s_view; }
void planner_ui_deactivate(void) { s_active = false; }
