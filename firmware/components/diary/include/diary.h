#pragma once

/* diary: one prose entry per day, <root>/.diary/YYYY/MM/YYYY-MM-DD.md
 *
 *   # Sat 2026-11-21
 *   Tags: AB CD
 *
 *   Whatever was written that day, in the Notes editor.
 *
 * The heading and the Tags line are both optional when reading. A Tags
 * line counts only right after the heading (blank lines between are
 * fine) and only when every word on it is a two-character tag, so a
 * hand-written "Tags: work stuff" stays part of the text. Tags are
 * letters A-Z and digits 0-9, stored upper case.
 *
 * Everything after those lines is the entry's text, kept byte for byte
 * when the tags change. An entry "has text" when that part holds more
 * than blanks. A day "has an entry" when its file has text or tags; a
 * file with neither is left over from writing nothing and is removed by
 * diary_remove_if_blank().
 *
 * The folder starts with '.' so the Notes file browser leaves it out. */

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stddef.h>

#define DIARY_DIR       ".diary"
#define DIARY_TAG_LEN   2
#define DIARY_MAX_TAGS  8
#define DIARY_BODY_MAX  (32 * 1024)     /* text loaded for reading */

#define DIARY_OK        0
#define DIARY_ERR_IO   -1
#define DIARY_ERR_ARG  -2
#define DIARY_ERR_FULL -3   /* the entry already has DIARY_MAX_TAGS tags */
#define DIARY_ERR_NONE -4   /* no entry in that direction */
#define DIARY_ERR_MEM  -5

typedef struct {
    bool exists;            /* the day has a file */
    bool has_text;
    int tag_count;
    char tags[DIARY_MAX_TAGS][DIARY_TAG_LEN + 1];
} diary_info_t;

typedef struct {
    int year, month, day;
    diary_info_t info;
    char *body;             /* the text, NUL-terminated; NULL when none */
    size_t body_len;
    bool truncated;         /* the text runs past DIARY_BODY_MAX */
} diary_entry_t;

/* Directory holding .diary/, e.g. the SD card mount point. The first
 * call for a root renames a diary/ folder left there by earlier
 * firmware to .diary/, when there is no .diary/ yet. */
void diary_set_root(const char *root);

/* The day's file path. Returns DIARY_ERR_ARG when it does not fit. */
int diary_path(char *out, size_t out_sz, int year, int month, int day);

/* What a day holds, without loading its text. A day with no file reads
 * as an empty info, not as an error. */
int diary_read_info(int year, int month, int day, diary_info_t *info);

/* Load a day for reading. A day with no file loads as empty. Free with
 * diary_entry_free(), also after an error. */
int diary_load(diary_entry_t *e, int year, int month, int day);
void diary_entry_free(diary_entry_t *e);

/* Make the day's file with just its heading, ready to write in. A file
 * that already exists is left alone. */
int diary_create(int year, int month, int day);

/* Upper-case code in place and check it is a valid tag. */
bool diary_tag_normalize(char *code);

/* Add code to the day's tags, or remove it when the day already has it.
 * *added says which. A day with no file gets one. Removing the last tag
 * of an entry with no text removes the file. */
int diary_toggle_tag(int year, int month, int day, const char *code,
                     bool *added);

/* Remove the day's file, and the Notes editor's hidden .meta file
 * beside it, when the entry has neither text nor tags. True when it
 * was removed. */
bool diary_remove_if_blank(int year, int month, int day);

/* Move the date to the nearest day after it (dir 1) or before it
 * (dir -1) that has an entry. DIARY_ERR_NONE leaves the date alone. */
int diary_find_entry(int *year, int *month, int *day, int dir);

/* What every day of a month holds: info[0] is the 1st. */
int diary_read_month(int year, int month, diary_info_t info[31]);

#ifdef __cplusplus
}
#endif
