#include "diary.h"

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <time.h>

#define PATH_MAX_LEN   208
#define HEADING_MAX    40
#define INFO_READ_MAX  2048     /* enough for the heading, tags and a word */
#define HEAD_ALLOWANCE 1024     /* room for the heading and tags on a load */
#define YEARS_MAX      64
#define TAGS_PREFIX    "Tags:"

static char s_root[160] = "/sdcard";

void diary_set_root(const char *root)
{
    snprintf(s_root, sizeof(s_root), "%s", root);
}

int diary_path(char *out, size_t out_sz, int year, int month, int day)
{
    int n = snprintf(out, out_sz, "%s/diary/%04d/%02d/%04d-%02d-%02d.md",
                     s_root, year, month, year, month, day);
    return n > 0 && (size_t)n < out_sz ? DIARY_OK : DIARY_ERR_ARG;
}

/* The Notes editor keeps a cursor position in .<name>.meta beside the
 * file it opened. */
static void meta_path(char *out, size_t out_sz, int year, int month, int day)
{
    snprintf(out, out_sz, "%s/diary/%04d/%02d/.%04d-%02d-%02d.md.meta",
             s_root, year, month, year, month, day);
}

static bool file_exists(const char *path)
{
    struct stat st;
    return stat(path, &st) == 0;
}

static int make_dirs(int year, int month)
{
    char path[PATH_MAX_LEN];
    snprintf(path, sizeof(path), "%s/diary", s_root);
    if (mkdir(path, 0775) != 0 && errno != EEXIST) return DIARY_ERR_IO;
    snprintf(path, sizeof(path), "%s/diary/%04d", s_root, year);
    if (mkdir(path, 0775) != 0 && errno != EEXIST) return DIARY_ERR_IO;
    snprintf(path, sizeof(path), "%s/diary/%04d/%02d", s_root, year, month);
    if (mkdir(path, 0775) != 0 && errno != EEXIST) return DIARY_ERR_IO;
    return DIARY_OK;
}

/* Noon keeps the weekday clear of daylight-saving changes. */
static void format_heading(char *out, size_t out_sz, int year, int month, int day)
{
    struct tm t;
    memset(&t, 0, sizeof(t));
    t.tm_year = year - 1900;
    t.tm_mon = month - 1;
    t.tm_mday = day;
    t.tm_hour = 12;
    t.tm_isdst = -1;
    mktime(&t);
    strftime(out, out_sz, "# %a %Y-%m-%d", &t);
}

/* ---- Parsing ---- */

typedef struct {
    size_t heading_off, heading_len;    /* heading_len 0: no heading */
    size_t body_off;
} layout_t;

static bool is_blank_char(char c)
{
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

static size_t line_end(const char *buf, size_t len, size_t pos)
{
    while (pos < len && buf[pos] != '\n') pos++;
    return pos;
}

static size_t next_line(size_t len, size_t end)
{
    return end < len ? end + 1 : len;
}

static bool blank_range(const char *buf, size_t from, size_t to)
{
    for (size_t i = from; i < to; i++) {
        if (!is_blank_char(buf[i])) return false;
    }
    return true;
}

static size_t skip_blank_lines(const char *buf, size_t len, size_t pos)
{
    while (pos < len) {
        size_t end = line_end(buf, len, pos);
        if (!blank_range(buf, pos, end)) break;
        pos = next_line(len, end);
    }
    return pos;
}

bool diary_tag_normalize(char *code)
{
    if (!code || strlen(code) != DIARY_TAG_LEN) return false;
    for (int i = 0; i < DIARY_TAG_LEN; i++) {
        unsigned char c = (unsigned char)code[i];
        if (c >= 'a' && c <= 'z') c = (unsigned char)(c - 'a' + 'A');
        if (!((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9'))) return false;
        code[i] = (char)c;
    }
    return true;
}

static int find_tag(const diary_info_t *info, const char *code)
{
    for (int i = 0; i < info->tag_count; i++) {
        if (strcmp(info->tags[i], code) == 0) return i;
    }
    return -1;
}

/* A Tags line, read into info. False, leaving info alone, when the line
 * is not one: a word that is not a tag makes it the entry's text. */
static bool parse_tags_line(const char *line, size_t len, diary_info_t *info)
{
    size_t plen = strlen(TAGS_PREFIX);
    if (len < plen || strncasecmp(line, TAGS_PREFIX, plen) != 0) return false;

    diary_info_t found = *info;
    found.tag_count = 0;
    size_t pos = plen;
    while (pos < len) {
        char c = line[pos];
        if (c == ' ' || c == '\t' || c == ',' || c == '\r') {
            pos++;
            continue;
        }
        size_t start = pos;
        while (pos < len && !strchr(" \t,\r", line[pos])) pos++;
        char code[DIARY_TAG_LEN + 2];
        if (pos - start != DIARY_TAG_LEN) return false;
        memcpy(code, line + start, DIARY_TAG_LEN);
        code[DIARY_TAG_LEN] = '\0';
        if (!diary_tag_normalize(code)) return false;
        if (find_tag(&found, code) >= 0) continue;
        if (found.tag_count == DIARY_MAX_TAGS) return false;
        memcpy(found.tags[found.tag_count++], code, DIARY_TAG_LEN + 1);
    }
    *info = found;
    return true;
}

/* has_text covers only what buf holds; for a file read in part that is
 * enough, since a word right after the tags settles it. */
static void parse(const char *buf, size_t len, diary_info_t *info, layout_t *lay)
{
    memset(lay, 0, sizeof(*lay));
    info->tag_count = 0;

    size_t pos = skip_blank_lines(buf, len, 0);
    if (pos + 1 < len && buf[pos] == '#' && buf[pos + 1] == ' ') {
        size_t end = line_end(buf, len, pos);
        lay->heading_off = pos;
        lay->heading_len = end - pos;
        if (lay->heading_len && buf[pos + lay->heading_len - 1] == '\r') {
            lay->heading_len--;
        }
        pos = skip_blank_lines(buf, len, next_line(len, end));

        end = line_end(buf, len, pos);
        if (pos < len && parse_tags_line(buf + pos, end - pos, info)) {
            pos = skip_blank_lines(buf, len, next_line(len, end));
        }
    }
    lay->body_off = pos;
    info->has_text = !blank_range(buf, pos, len);
}

/* ---- Files ---- */

/* Read up to max bytes of path into a malloc'ed, NUL-terminated buffer.
 * *size is the whole file's size. DIARY_OK with *buf NULL: no file. */
static int read_file(const char *path, size_t max, char **buf, size_t *len,
                     size_t *size)
{
    *buf = NULL;
    *len = 0;
    *size = 0;

    struct stat st;
    if (stat(path, &st) != 0) return DIARY_OK;
    *size = (size_t)st.st_size;

    size_t want = *size < max ? *size : max;
    char *data = malloc(want + 1);
    if (!data) return DIARY_ERR_MEM;
    FILE *f = fopen(path, "rb");
    if (!f) {
        free(data);
        return DIARY_ERR_IO;
    }
    size_t n = fread(data, 1, want, f);
    bool failed = ferror(f) != 0;
    fclose(f);
    if (failed) {
        free(data);
        return DIARY_ERR_IO;
    }
    data[n] = '\0';
    *buf = data;
    *len = n;
    return DIARY_OK;
}

/* A rewrite was cut off between its remove and rename. */
static void recover_tmp(const char *path)
{
    char tmp[PATH_MAX_LEN + 4];
    snprintf(tmp, sizeof(tmp), "%s.tmp", path);
    if (!file_exists(path) && file_exists(tmp)) rename(tmp, path);
}

int diary_read_info(int year, int month, int day, diary_info_t *info)
{
    if (!info) return DIARY_ERR_ARG;
    memset(info, 0, sizeof(*info));

    char path[PATH_MAX_LEN];
    if (diary_path(path, sizeof(path), year, month, day) != DIARY_OK) {
        return DIARY_ERR_ARG;
    }
    recover_tmp(path);

    char *buf;
    size_t len, size;
    int ret = read_file(path, INFO_READ_MAX, &buf, &len, &size);
    if (ret != DIARY_OK || !buf) return ret;

    layout_t lay;
    parse(buf, len, info, &lay);
    info->exists = true;
    free(buf);
    return DIARY_OK;
}

void diary_entry_free(diary_entry_t *e)
{
    if (!e) return;
    free(e->body);
    e->body = NULL;
    e->body_len = 0;
}

int diary_load(diary_entry_t *e, int year, int month, int day)
{
    if (!e) return DIARY_ERR_ARG;
    diary_entry_free(e);
    memset(e, 0, sizeof(*e));
    e->year = year;
    e->month = month;
    e->day = day;

    char path[PATH_MAX_LEN];
    if (diary_path(path, sizeof(path), year, month, day) != DIARY_OK) {
        return DIARY_ERR_ARG;
    }
    recover_tmp(path);

    char *buf;
    size_t len, size;
    int ret = read_file(path, DIARY_BODY_MAX + HEAD_ALLOWANCE, &buf, &len, &size);
    if (ret != DIARY_OK || !buf) return ret;

    layout_t lay;
    parse(buf, len, &e->info, &lay);
    e->info.exists = true;
    if (!e->info.has_text) {
        free(buf);
        return DIARY_OK;
    }

    size_t body_len = len - lay.body_off;
    e->truncated = size > len;
    if (body_len > DIARY_BODY_MAX) {
        body_len = DIARY_BODY_MAX;
        e->truncated = true;
    }
    if (e->truncated) {
        /* Not in the middle of a UTF-8 character. */
        while (body_len && ((unsigned char)buf[lay.body_off + body_len] & 0xC0) == 0x80) {
            body_len--;
        }
    }
    while (body_len && is_blank_char(buf[lay.body_off + body_len - 1])) body_len--;

    memmove(buf, buf + lay.body_off, body_len);
    buf[body_len] = '\0';
    e->body = buf;
    e->body_len = body_len;
    return DIARY_OK;
}

int diary_create(int year, int month, int day)
{
    char path[PATH_MAX_LEN];
    if (diary_path(path, sizeof(path), year, month, day) != DIARY_OK) {
        return DIARY_ERR_ARG;
    }
    recover_tmp(path);
    if (file_exists(path)) return DIARY_OK;
    if (make_dirs(year, month) != DIARY_OK) return DIARY_ERR_IO;

    char heading[HEADING_MAX];
    format_heading(heading, sizeof(heading), year, month, day);
    FILE *f = fopen(path, "w");
    if (!f) return DIARY_ERR_IO;
    bool ok = fprintf(f, "%s\n\n", heading) > 0;
    if (fclose(f) != 0) ok = false;
    return ok ? DIARY_OK : DIARY_ERR_IO;
}

/* Write the heading, the tags and buf's text from body_off on, through
 * a temporary file so a failure leaves the old file. */
static int write_entry(const char *path, const char *heading, size_t heading_len,
                       const diary_info_t *info, const char *body, size_t body_len)
{
    char tmp[PATH_MAX_LEN + 4];
    snprintf(tmp, sizeof(tmp), "%s.tmp", path);

    FILE *f = fopen(tmp, "w");
    if (!f) return DIARY_ERR_IO;
    bool ok = fwrite(heading, 1, heading_len, f) == heading_len;
    ok = ok && fputc('\n', f) != EOF;
    if (info->tag_count) {
        ok = ok && fputs(TAGS_PREFIX, f) != EOF;
        for (int i = 0; i < info->tag_count; i++) {
            ok = ok && fprintf(f, " %s", info->tags[i]) > 0;
        }
        ok = ok && fputc('\n', f) != EOF;
    }
    ok = ok && fputc('\n', f) != EOF;
    if (body_len) ok = ok && fwrite(body, 1, body_len, f) == body_len;
    if (fclose(f) != 0) ok = false;
    if (!ok) {
        remove(tmp);
        return DIARY_ERR_IO;
    }

    /* FAT's rename does not replace an existing file. */
    if (file_exists(path) && remove(path) != 0) {
        remove(tmp);
        return DIARY_ERR_IO;
    }
    return rename(tmp, path) == 0 ? DIARY_OK : DIARY_ERR_IO;
}

int diary_toggle_tag(int year, int month, int day, const char *code,
                     bool *added)
{
    char tag[DIARY_TAG_LEN + 2];
    if (!code || strlen(code) != DIARY_TAG_LEN) return DIARY_ERR_ARG;
    memcpy(tag, code, DIARY_TAG_LEN + 1);
    if (!diary_tag_normalize(tag)) return DIARY_ERR_ARG;

    char path[PATH_MAX_LEN];
    if (diary_path(path, sizeof(path), year, month, day) != DIARY_OK) {
        return DIARY_ERR_ARG;
    }
    recover_tmp(path);

    char *buf;
    size_t len, size;
    int ret = read_file(path, (size_t)-1 / 2, &buf, &len, &size);
    if (ret != DIARY_OK) return ret;

    diary_info_t info;
    layout_t lay;
    memset(&info, 0, sizeof(info));
    memset(&lay, 0, sizeof(lay));
    if (buf) parse(buf, len, &info, &lay);

    int idx = find_tag(&info, tag);
    if (idx >= 0) {
        memmove(info.tags[idx], info.tags[idx + 1],
                (size_t)(info.tag_count - idx - 1) * sizeof(info.tags[0]));
        info.tag_count--;
    } else if (info.tag_count == DIARY_MAX_TAGS) {
        free(buf);
        return DIARY_ERR_FULL;
    } else {
        memcpy(info.tags[info.tag_count++], tag, DIARY_TAG_LEN + 1);
    }
    if (added) *added = idx < 0;

    if (!buf && make_dirs(year, month) != DIARY_OK) return DIARY_ERR_IO;

    char made[HEADING_MAX];
    const char *heading = made;
    size_t heading_len;
    if (buf && lay.heading_len) {
        heading = buf + lay.heading_off;
        heading_len = lay.heading_len;
    } else {
        format_heading(made, sizeof(made), year, month, day);
        heading_len = strlen(made);
    }
    const char *body = buf ? buf + lay.body_off : "";
    size_t body_len = buf ? len - lay.body_off : 0;

    ret = write_entry(path, heading, heading_len, &info, body, body_len);
    free(buf);
    if (ret == DIARY_OK) diary_remove_if_blank(year, month, day);
    return ret;
}

bool diary_remove_if_blank(int year, int month, int day)
{
    diary_info_t info;
    if (diary_read_info(year, month, day, &info) != DIARY_OK) return false;
    if (!info.exists || info.has_text || info.tag_count) return false;

    char path[PATH_MAX_LEN];
    diary_path(path, sizeof(path), year, month, day);
    if (remove(path) != 0) return false;
    meta_path(path, sizeof(path), year, month, day);
    remove(path);
    return true;
}

/* ---- Finding entries ---- */

static int cmp_int(const void *a, const void *b)
{
    int x = *(const int *)a, y = *(const int *)b;
    return (x > y) - (x < y);
}

static bool all_digits(const char *s, size_t n)
{
    for (size_t i = 0; i < n; i++) {
        if (s[i] < '0' || s[i] > '9') return false;
    }
    return true;
}

/* The years under diary/, sorted. */
static int list_years(int *out, int max)
{
    char path[PATH_MAX_LEN];
    snprintf(path, sizeof(path), "%s/diary", s_root);
    DIR *dir = opendir(path);
    if (!dir) return 0;

    int n = 0;
    struct dirent *ent;
    while ((ent = readdir(dir)) != NULL && n < max) {
        if (strlen(ent->d_name) == 4 && all_digits(ent->d_name, 4)) {
            out[n++] = atoi(ent->d_name);
        }
    }
    closedir(dir);
    qsort(out, (size_t)n, sizeof(int), cmp_int);
    return n;
}

/* The days of a month that have a file, sorted. */
static int list_days(int year, int month, int out[31])
{
    char path[PATH_MAX_LEN];
    snprintf(path, sizeof(path), "%s/diary/%04d/%02d", s_root, year, month);
    DIR *dir = opendir(path);
    if (!dir) return 0;

    char prefix[12];
    snprintf(prefix, sizeof(prefix), "%04d-%02d-", year, month);
    int n = 0;
    struct dirent *ent;
    while ((ent = readdir(dir)) != NULL && n < 31) {
        const char *name = ent->d_name;
        /* YYYY-MM-DD.md */
        if (strlen(name) != 13 || strncmp(name, prefix, 8) != 0 ||
            !all_digits(name + 8, 2) || strcmp(name + 10, ".md") != 0) {
            continue;
        }
        int d = atoi(name + 8);
        if (d >= 1 && d <= 31) out[n++] = d;
    }
    closedir(dir);
    qsort(out, (size_t)n, sizeof(int), cmp_int);
    return n;
}

static bool has_entry(int year, int month, int day)
{
    diary_info_t info;
    return diary_read_info(year, month, day, &info) == DIARY_OK &&
           info.exists && (info.has_text || info.tag_count);
}

/* Compare two dates as YYYYMMDD numbers. */
static long date_key(int y, int m, int d)
{
    return (long)y * 10000 + m * 100 + d;
}

int diary_find_entry(int *year, int *month, int *day, int dir)
{
    if (!year || !month || !day || (dir != 1 && dir != -1)) return DIARY_ERR_ARG;

    int years[YEARS_MAX];
    int ny = list_years(years, YEARS_MAX);
    long from = date_key(*year, *month, *day);

    for (int yi = dir > 0 ? 0 : ny - 1; yi >= 0 && yi < ny; yi += dir) {
        int y = years[yi];
        if (dir > 0 ? y < *year : y > *year) continue;
        for (int m = dir > 0 ? 1 : 12; m >= 1 && m <= 12; m += dir) {
            if (dir > 0 ? date_key(y, m, 31) <= from : date_key(y, m, 1) >= from) {
                continue;
            }
            int days[31];
            int nd = list_days(y, m, days);
            for (int di = dir > 0 ? 0 : nd - 1; di >= 0 && di < nd; di += dir) {
                long key = date_key(y, m, days[di]);
                if (dir > 0 ? key <= from : key >= from) continue;
                if (!has_entry(y, m, days[di])) continue;
                *year = y;
                *month = m;
                *day = days[di];
                return DIARY_OK;
            }
        }
    }
    return DIARY_ERR_NONE;
}

int diary_read_month(int year, int month, diary_info_t info[31])
{
    if (!info) return DIARY_ERR_ARG;
    memset(info, 0, 31 * sizeof(info[0]));

    int days[31];
    int nd = list_days(year, month, days);
    int ret = DIARY_OK;
    for (int i = 0; i < nd; i++) {
        int r = diary_read_info(year, month, days[i], &info[days[i] - 1]);
        if (r != DIARY_OK) ret = r;
    }
    return ret;
}
