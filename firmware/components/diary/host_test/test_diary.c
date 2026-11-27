/* Runs diary.c on a computer:
 *   cc -Wall -Wextra -I../include test_diary.c ../diary.c -o /tmp/td
 *   /tmp/td <empty scratch directory>
 */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "diary.h"

static void put_file(const char *path, const char *text)
{
    FILE *f = fopen(path, "w");
    assert(f);
    fputs(text, f);
    fclose(f);
}

static char *read_all(const char *path)
{
    static char buf[4096];
    FILE *f = fopen(path, "r");
    assert(f);
    size_t n = fread(buf, 1, sizeof(buf) - 1, f);
    buf[n] = '\0';
    fclose(f);
    return buf;
}

static bool exists(const char *path)
{
    struct stat st;
    return stat(path, &st) == 0;
}

int main(int argc, char **argv)
{
    assert(argc == 2);
    char path[256];
    diary_info_t info;
    diary_entry_t e = { 0 };
    bool added;
    diary_set_root(argv[1]);

    /* A day with no file is empty, not an error. */
    assert(diary_load(&e, 2026, 11, 21) == DIARY_OK);
    assert(!e.info.exists && !e.body);
    int y = 2026, m = 11, d = 21;
    assert(diary_find_entry(&y, &m, &d, -1) == DIARY_ERR_NONE);

    /* A fresh file holds only its heading, and is blank. */
    assert(diary_create(2026, 11, 21) == DIARY_OK);
    diary_path(path, sizeof(path), 2026, 11, 21);
    assert(strcmp(read_all(path), "# Sat 2026-11-21\n\n") == 0);
    assert(diary_read_info(2026, 11, 21, &info) == DIARY_OK);
    assert(info.exists && !info.has_text && info.tag_count == 0);
    assert(diary_find_entry(&y, &m, &d, 1) == DIARY_ERR_NONE);

    /* Blank files are removed, with the editor's sidecar. */
    char meta[256];
    snprintf(meta, sizeof(meta), "%s/diary/2026/11/.2026-11-21.md.meta", argv[1]);
    put_file(meta, "cursor=0\n");
    assert(diary_remove_if_blank(2026, 11, 21));
    assert(!exists(path) && !exists(meta));

    /* Text, then tags: the text is kept byte for byte. */
    assert(diary_create(2026, 11, 21) == DIARY_OK);
    put_file(path, "# Sat 2026-11-21\n\nDear diary,\n\n  it rained.\n");
    assert(!diary_remove_if_blank(2026, 11, 21));
    assert(diary_toggle_tag(2026, 11, 21, "ab", &added) == DIARY_OK && added);
    assert(diary_toggle_tag(2026, 11, 21, "C3", &added) == DIARY_OK && added);
    assert(strcmp(read_all(path),
                  "# Sat 2026-11-21\nTags: AB C3\n\nDear diary,\n\n  it rained.\n") == 0);
    assert(diary_load(&e, 2026, 11, 21) == DIARY_OK);
    assert(e.info.has_text && e.info.tag_count == 2);
    assert(strcmp(e.info.tags[0], "AB") == 0 && strcmp(e.info.tags[1], "C3") == 0);
    assert(strcmp(e.body, "Dear diary,\n\n  it rained.") == 0);

    /* The same tag again removes it. */
    assert(diary_toggle_tag(2026, 11, 21, "AB", &added) == DIARY_OK && !added);
    assert(strcmp(read_all(path),
                  "# Sat 2026-11-21\nTags: C3\n\nDear diary,\n\n  it rained.\n") == 0);

    /* Bad codes are refused. */
    assert(diary_toggle_tag(2026, 11, 21, "A", &added) == DIARY_ERR_ARG);
    assert(diary_toggle_tag(2026, 11, 21, "A!", &added) == DIARY_ERR_ARG);

    /* A tags line with a word that is not a tag is text. */
    diary_path(path, sizeof(path), 2026, 11, 3);
    assert(diary_create(2026, 11, 3) == DIARY_OK);
    put_file(path, "# Tue 2026-11-03\nTags: work stuff\n");
    assert(diary_read_info(2026, 11, 3, &info) == DIARY_OK);
    assert(info.has_text && info.tag_count == 0);

    /* A tag on a day with no file makes one; removing it removes it. */
    diary_path(path, sizeof(path), 2026, 12, 25);
    assert(diary_toggle_tag(2026, 12, 25, "XM", &added) == DIARY_OK && added);
    assert(strcmp(read_all(path), "# Fri 2026-12-25\nTags: XM\n\n") == 0);
    assert(diary_read_info(2026, 12, 25, &info) == DIARY_OK);
    assert(info.exists && !info.has_text && info.tag_count == 1);

    /* Finding entries: blank files are skipped, months and years crossed. */
    assert(diary_create(2026, 11, 10) == DIARY_OK);       /* blank */
    assert(diary_toggle_tag(2025, 6, 1, "OL", &added) == DIARY_OK);
    y = 2026; m = 11; d = 21;
    assert(diary_find_entry(&y, &m, &d, -1) == DIARY_OK);
    assert(y == 2026 && m == 11 && d == 3);
    assert(diary_find_entry(&y, &m, &d, -1) == DIARY_OK);
    assert(y == 2025 && m == 6 && d == 1);
    assert(diary_find_entry(&y, &m, &d, -1) == DIARY_ERR_NONE);
    assert(y == 2025 && m == 6 && d == 1);
    assert(diary_find_entry(&y, &m, &d, 1) == DIARY_OK && y == 2026 && m == 11 && d == 3);
    assert(diary_find_entry(&y, &m, &d, 1) == DIARY_OK && d == 21);
    assert(diary_find_entry(&y, &m, &d, 1) == DIARY_OK && m == 12 && d == 25);
    assert(diary_find_entry(&y, &m, &d, 1) == DIARY_ERR_NONE);

    /* The month at a glance. */
    diary_info_t month[31];
    assert(diary_read_month(2026, 11, month) == DIARY_OK);
    assert(month[2].has_text && month[20].has_text && month[20].tag_count == 1);
    assert(month[9].exists && !month[9].has_text);
    assert(!month[0].exists);

    /* CRLF files and a heading-less file. */
    diary_path(path, sizeof(path), 2026, 11, 4);
    assert(diary_create(2026, 11, 4) == DIARY_OK);
    put_file(path, "# Wed 2026-11-04\r\nTags: QQ\r\n\r\nHi\r\n");
    assert(diary_load(&e, 2026, 11, 4) == DIARY_OK);
    assert(e.info.tag_count == 1 && strcmp(e.body, "Hi") == 0);
    put_file(path, "Just text\n");
    assert(diary_toggle_tag(2026, 11, 4, "zz", &added) == DIARY_OK);
    assert(strcmp(read_all(path), "# Wed 2026-11-04\nTags: ZZ\n\nJust text\n") == 0);

    diary_entry_free(&e);
    puts("diary: all tests passed");
    return 0;
}
