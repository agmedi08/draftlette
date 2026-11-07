#pragma once

/* journal: one Markdown file per day, <root>/journal/YYYY/MM/YYYY-MM-DD.md
 *
 *   # Fri 2026-11-06
 *
 *   - [ ] open task
 *   - [x] finished task
 *   - [-] cancelled task
 *   - [>] migrated task -> 2026-11-07
 *   - o event
 *   - ! tracker
 *   - note
 *
 * Any other line is kept untouched as JOURNAL_RAW, so hand edits made
 * in Notes survive. Blank lines are not kept.
 *
 * A weekly page holds items for a week but no particular day. It has
 * the same bullets, is named after the week's Monday, and sits beside
 * that day's file: <root>/journal/YYYY/MM/YYYY-MM-DD-week.md, headed
 * "# 2026-11-02 to 2026-11-08". */

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>

#define JOURNAL_TEXT_MAX    200
#define JOURNAL_MAX_BULLETS 96

#define JOURNAL_OK           0
#define JOURNAL_ERR_IO      -1
#define JOURNAL_ERR_TOO_BIG -2  /* file exceeds the limits above; not loaded */
#define JOURNAL_ERR_FULL    -3  /* the day has no room for another bullet */
#define JOURNAL_ERR_ARG     -4

typedef enum {
    JOURNAL_TASK,
    JOURNAL_DONE,
    JOURNAL_CANCELLED,
    JOURNAL_MIGRATED,
    JOURNAL_EVENT,
    JOURNAL_NOTE,
    JOURNAL_TRACKER,
    JOURNAL_RAW,
} journal_kind_t;

typedef struct {
    journal_kind_t kind;
    char text[JOURNAL_TEXT_MAX];
    char moved_to[11];      /* JOURNAL_MIGRATED only: YYYY-MM-DD */
} journal_bullet_t;

typedef struct {
    int year, month, day;   /* a weekly page: its Monday */
    bool is_week;
    int count;
    journal_bullet_t bullets[JOURNAL_MAX_BULLETS];
} journal_day_t;

/* Directory holding journal/, e.g. the SD card mount point. */
void journal_set_root(const char *root);

/* A day with no file loads as empty. Returns JOURNAL_ERR_TOO_BIG rather
 * than a partial day, because saving a partial day would lose lines. */
int journal_load(journal_day_t *day, int year, int month, int day_of_month);

/* Load the weekly page for the week holding the given date. A week
 * with no file loads as empty. */
int journal_load_week(journal_day_t *week, int year, int month,
                      int day_of_month);

/* Writes the whole day. An empty day with no file creates nothing. */
int journal_save(const journal_day_t *day);

/* Make sure the day has a file, even with no bullets yet. A file that
 * already exists is left alone. */
int journal_create(const journal_day_t *day);

/* Copy bullet idx to the given day as an open item, then mark it
 * migrated in src. The copy is saved first, so a failure can duplicate
 * a bullet but never lose one. */
int journal_migrate(journal_day_t *src, int idx, int year, int month,
                    int day_of_month);

/* Move a date by delta days. */
void journal_date_add(int *year, int *month, int *day_of_month, int delta);

/* Move a date back to the Monday of its week. */
void journal_week_start(int *year, int *month, int *day_of_month);

#ifdef __cplusplus
}
#endif
