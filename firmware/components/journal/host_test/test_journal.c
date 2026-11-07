/* Runs journal.c on a computer:
 *   cc -Wall -Wextra -I../include test_journal.c ../journal.c -o /tmp/tj
 *   /tmp/tj <empty scratch directory>
 */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "journal.h"

static journal_day_t a, b;

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

int main(int argc, char **argv)
{
    assert(argc == 2);
    char path[256], cmd[320];
    journal_set_root(argv[1]);

    /* An empty day leaves no file behind. */
    assert(journal_load(&a, 2026, 11, 6) == JOURNAL_OK && a.count == 0);
    assert(journal_save(&a) == JOURNAL_OK);
    snprintf(path, sizeof(path), "%s/journal/2026/11/2026-11-06.md", argv[1]);
    assert(!fopen(path, "r"));

    /* Every kind round-trips, with a pre-formatted header. */
    static const char *const text =
        "# Fri 2026-11-06\n\n"
        "- [ ] open\n- [x] finished\n- [-] dropped\n"
        "- [>] moved on -> 2026-11-07\n- o dentist\n- ! headache\n"
        "- plain note\nsome hand-written line\n## heading\n";
    snprintf(cmd, sizeof(cmd), "mkdir -p %s/journal/2026/11", argv[1]);
    assert(system(cmd) == 0);
    put_file(path, text);
    assert(journal_load(&a, 2026, 11, 6) == JOURNAL_OK);
    assert(a.count == 9);
    assert(a.bullets[0].kind == JOURNAL_TASK);
    assert(a.bullets[1].kind == JOURNAL_DONE);
    assert(a.bullets[2].kind == JOURNAL_CANCELLED);
    assert(a.bullets[3].kind == JOURNAL_MIGRATED);
    assert(!strcmp(a.bullets[3].text, "moved on"));
    assert(!strcmp(a.bullets[3].moved_to, "2026-11-07"));
    assert(a.bullets[4].kind == JOURNAL_EVENT);
    assert(a.bullets[5].kind == JOURNAL_TRACKER);
    assert(a.bullets[6].kind == JOURNAL_NOTE);
    assert(a.bullets[7].kind == JOURNAL_RAW);
    assert(journal_save(&a) == JOURNAL_OK);
    assert(!strcmp(read_all(path), text));

    /* Migration copies, marks, and crosses a month end. */
    int y = 2026, m = 11, d = 30;
    journal_date_add(&y, &m, &d, 1);
    assert(y == 2026 && m == 12 && d == 1);
    assert(journal_migrate(&a, 0, y, m, d) == JOURNAL_OK);
    assert(a.bullets[0].kind == JOURNAL_MIGRATED);
    assert(journal_load(&b, 2026, 12, 1) == JOURNAL_OK);
    assert(b.count == 1 && b.bullets[0].kind == JOURNAL_TASK);
    assert(!strcmp(b.bullets[0].text, "open"));
    assert(journal_load(&a, 2026, 11, 6) == JOURNAL_OK);
    assert(!strcmp(a.bullets[0].moved_to, "2026-12-01"));

    /* Migrating to the same day is refused. */
    assert(journal_migrate(&a, 1, 2026, 11, 6) == JOURNAL_ERR_ARG);

    /* A file too big to hold is refused, not truncated. */
    char big[256];
    snprintf(big, sizeof(big), "%s/journal/2026/11/2026-11-07.md", argv[1]);
    FILE *f = fopen(big, "w");
    assert(f);
    for (int i = 0; i < JOURNAL_MAX_BULLETS + 1; i++) fprintf(f, "- n%d\n", i);
    fclose(f);
    assert(journal_load(&b, 2026, 11, 7) == JOURNAL_ERR_TOO_BIG);
    f = fopen(big, "w");
    assert(f);
    for (int i = 0; i < 400; i++) fputc('x', f);
    fputc('\n', f);
    fclose(f);
    assert(journal_load(&b, 2026, 11, 7) == JOURNAL_ERR_TOO_BIG);

    /* A save cut off before its rename is recovered. */
    char tmp[300];
    snprintf(tmp, sizeof(tmp), "%s.tmp", big);
    remove(big);
    put_file(tmp, "# Sat 2026-11-07\n\n- [ ] saved\n");
    assert(journal_load(&b, 2026, 11, 7) == JOURNAL_OK && b.count == 1);

    /* Windows line endings from editing the card elsewhere. */
    put_file(big, "# Sat 2026-11-07\r\n\r\n- [ ] crlf\r\n");
    assert(journal_load(&b, 2026, 11, 7) == JOURNAL_OK);
    assert(b.count == 1 && !strcmp(b.bullets[0].text, "crlf"));

    puts("journal tests passed");
    return 0;
}
