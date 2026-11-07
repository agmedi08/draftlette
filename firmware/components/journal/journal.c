#include "journal.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

#define PATH_MAX_LEN 208
#define LINE_MAX_LEN (JOURNAL_TEXT_MAX + 32)
#define MOVED_MARK   " -> "
#define DATE_LEN     10

static char s_root[160] = "/sdcard";

void journal_set_root(const char *root)
{
    snprintf(s_root, sizeof(s_root), "%s", root);
}

static void day_path(char *out, size_t out_sz, int y, int m, int d)
{
    snprintf(out, out_sz, "%s/journal/%04d/%02d/%04d-%02d-%02d.md",
             s_root, y, m, y, m, d);
}

static bool file_exists(const char *path)
{
    struct stat st;
    return stat(path, &st) == 0;
}

static int make_dirs(int y, int m)
{
    char path[PATH_MAX_LEN];
    snprintf(path, sizeof(path), "%s/journal", s_root);
    if (mkdir(path, 0775) != 0 && errno != EEXIST) return JOURNAL_ERR_IO;
    snprintf(path, sizeof(path), "%s/journal/%04d", s_root, y);
    if (mkdir(path, 0775) != 0 && errno != EEXIST) return JOURNAL_ERR_IO;
    snprintf(path, sizeof(path), "%s/journal/%04d/%02d", s_root, y, m);
    if (mkdir(path, 0775) != 0 && errno != EEXIST) return JOURNAL_ERR_IO;
    return JOURNAL_OK;
}

/* Noon keeps these date sums clear of daylight-saving changes. */
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

void journal_date_add(int *year, int *month, int *day_of_month, int delta)
{
    struct tm t;
    date_to_tm(&t, *year, *month, *day_of_month + delta);
    *year = t.tm_year + 1900;
    *month = t.tm_mon + 1;
    *day_of_month = t.tm_mday;
}

static void format_header(char *out, size_t out_sz, int y, int m, int d)
{
    struct tm t;
    date_to_tm(&t, y, m, d);
    strftime(out, out_sz, "# %a %Y-%m-%d", &t);
}

static void set_text(journal_bullet_t *b, journal_kind_t kind, const char *text)
{
    b->kind = kind;
    b->moved_to[0] = '\0';
    snprintf(b->text, sizeof(b->text), "%s", text);
}

static void parse_line(const char *line, journal_bullet_t *b)
{
    static const struct {
        const char *prefix;
        journal_kind_t kind;
    } kinds[] = {
        { "- [ ] ", JOURNAL_TASK },
        { "- [x] ", JOURNAL_DONE },
        { "- [X] ", JOURNAL_DONE },
        { "- [-] ", JOURNAL_CANCELLED },
        { "- [>] ", JOURNAL_MIGRATED },
        { "- o ",   JOURNAL_EVENT },
        { "- ! ",   JOURNAL_TRACKER },
        { "- ",     JOURNAL_NOTE },
    };

    for (size_t i = 0; i < sizeof(kinds) / sizeof(kinds[0]); i++) {
        size_t n = strlen(kinds[i].prefix);
        if (strncmp(line, kinds[i].prefix, n) != 0 || !line[n]) continue;
        set_text(b, kinds[i].kind, line + n);
        if (b->kind == JOURNAL_MIGRATED) {
            size_t len = strlen(b->text);
            size_t tail = strlen(MOVED_MARK) + DATE_LEN;
            if (len > tail &&
                strncmp(b->text + len - tail, MOVED_MARK,
                        strlen(MOVED_MARK)) == 0) {
                memcpy(b->moved_to, b->text + len - DATE_LEN, DATE_LEN);
                b->moved_to[DATE_LEN] = '\0';
                b->text[len - tail] = '\0';
            }
        }
        return;
    }
    set_text(b, JOURNAL_RAW, line);
}

static void write_bullet(FILE *f, const journal_bullet_t *b)
{
    switch (b->kind) {
    case JOURNAL_TASK:      fprintf(f, "- [ ] %s\n", b->text); break;
    case JOURNAL_DONE:      fprintf(f, "- [x] %s\n", b->text); break;
    case JOURNAL_CANCELLED: fprintf(f, "- [-] %s\n", b->text); break;
    case JOURNAL_MIGRATED:
        if (b->moved_to[0]) {
            fprintf(f, "- [>] %s%s%s\n", b->text, MOVED_MARK, b->moved_to);
        } else {
            fprintf(f, "- [>] %s\n", b->text);
        }
        break;
    case JOURNAL_EVENT:     fprintf(f, "- o %s\n", b->text); break;
    case JOURNAL_TRACKER:   fprintf(f, "- ! %s\n", b->text); break;
    case JOURNAL_NOTE:      fprintf(f, "- %s\n", b->text); break;
    default:                fprintf(f, "%s\n", b->text); break;
    }
}

int journal_load(journal_day_t *day, int year, int month, int day_of_month)
{
    if (!day) return JOURNAL_ERR_ARG;
    day->year = year;
    day->month = month;
    day->day = day_of_month;
    day->count = 0;

    char path[PATH_MAX_LEN];
    char tmp[PATH_MAX_LEN + 4];
    day_path(path, sizeof(path), year, month, day_of_month);
    snprintf(tmp, sizeof(tmp), "%s.tmp", path);

    /* journal_save() was cut off between its remove and rename. */
    if (!file_exists(path) && file_exists(tmp)) rename(tmp, path);

    FILE *f = fopen(path, "r");
    if (!f) return file_exists(path) ? JOURNAL_ERR_IO : JOURNAL_OK;

    char header[32];
    format_header(header, sizeof(header), year, month, day_of_month);

    char line[LINE_MAX_LEN];
    int ret = JOURNAL_OK;
    bool first = true;
    while (fgets(line, sizeof(line), f)) {
        size_t len = strlen(line);
        if (len == sizeof(line) - 1 && line[len - 1] != '\n') {
            ret = JOURNAL_ERR_TOO_BIG;
            break;
        }
        while (len && (line[len - 1] == '\n' || line[len - 1] == '\r')) {
            line[--len] = '\0';
        }
        if (len == 0) continue;
        if (first) {
            first = false;
            if (strcmp(line, header) == 0) continue;
        }
        if (len >= JOURNAL_TEXT_MAX || day->count == JOURNAL_MAX_BULLETS) {
            ret = JOURNAL_ERR_TOO_BIG;
            break;
        }
        parse_line(line, &day->bullets[day->count++]);
    }
    if (ret == JOURNAL_OK && ferror(f)) ret = JOURNAL_ERR_IO;
    fclose(f);
    if (ret != JOURNAL_OK) day->count = 0;
    return ret;
}

int journal_save(const journal_day_t *day)
{
    if (!day) return JOURNAL_ERR_ARG;

    char path[PATH_MAX_LEN];
    char tmp[PATH_MAX_LEN + 4];
    day_path(path, sizeof(path), day->year, day->month, day->day);
    snprintf(tmp, sizeof(tmp), "%s.tmp", path);

    if (day->count == 0 && !file_exists(path)) return JOURNAL_OK;

    int ret = make_dirs(day->year, day->month);
    if (ret != JOURNAL_OK) return ret;

    /* FAT's rename will not replace a file, so the old one goes last,
     * once the new one is fully written. */
    FILE *f = fopen(tmp, "w");
    if (!f) return JOURNAL_ERR_IO;

    char header[32];
    format_header(header, sizeof(header), day->year, day->month, day->day);
    fprintf(f, "%s\n\n", header);
    for (int i = 0; i < day->count; i++) write_bullet(f, &day->bullets[i]);

    bool failed = ferror(f) != 0;
    if (fclose(f) != 0) failed = true;
    if (failed) {
        remove(tmp);
        return JOURNAL_ERR_IO;
    }

    if (file_exists(path) && remove(path) != 0) {
        remove(tmp);
        return JOURNAL_ERR_IO;
    }
    return rename(tmp, path) == 0 ? JOURNAL_OK : JOURNAL_ERR_IO;
}

int journal_migrate(journal_day_t *src, int idx, int year, int month,
                    int day_of_month)
{
    if (!src || idx < 0 || idx >= src->count) return JOURNAL_ERR_ARG;
    if (year == src->year && month == src->month && day_of_month == src->day) {
        return JOURNAL_ERR_ARG;
    }

    journal_day_t *dst = malloc(sizeof(*dst));
    if (!dst) return JOURNAL_ERR_IO;

    int ret = journal_load(dst, year, month, day_of_month);
    if (ret == JOURNAL_OK && dst->count == JOURNAL_MAX_BULLETS) {
        ret = JOURNAL_ERR_FULL;
    }
    if (ret == JOURNAL_OK) {
        journal_bullet_t *b = &src->bullets[idx];
        set_text(&dst->bullets[dst->count++],
                 b->kind == JOURNAL_EVENT ? JOURNAL_EVENT : JOURNAL_TASK,
                 b->text);
        ret = journal_save(dst);
    }
    free(dst);
    if (ret != JOURNAL_OK) return ret;

    journal_bullet_t *b = &src->bullets[idx];
    b->kind = JOURNAL_MIGRATED;
    snprintf(b->moved_to, sizeof(b->moved_to), "%04d-%02d-%02d",
             year, month, day_of_month);
    return journal_save(src);
}
