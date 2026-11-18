/* Runs status_bar.c on a computer:
 *   cc -Wall -Wextra -I../include test_status_bar.c ../status_bar.c -o /tmp/tsb
 *   /tmp/tsb
 */
#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "status_bar.h"

static struct tm at(int year, int hour, int min)
{
    struct tm t;
    memset(&t, 0, sizeof(t));
    t.tm_year = year - 1900;
    t.tm_mon = 10;
    t.tm_mday = 18;
    t.tm_hour = hour;
    t.tm_min = min;
    return t;
}

static void expect_clock(int year, int hour, int min, const char *want)
{
    char buf[STATUS_BAR_CLOCK_LEN];
    struct tm t = at(year, hour, min);
    status_bar_clock_text(buf, sizeof(buf), &t);
    if (strcmp(buf, want) != 0) {
        printf("clock %d %02d:%02d: got \"%s\", want \"%s\"\n",
               year, hour, min, buf, want);
        assert(0);
    }
}

static void test_clock(void)
{
    expect_clock(2026, 9, 5, "09:05");
    expect_clock(2026, 0, 0, "00:00");
    expect_clock(2026, 23, 59, "23:59");
    expect_clock(2026, 14, 5, "14:05");
    expect_clock(STATUS_BAR_MIN_VALID_YEAR, 12, 0, "12:00");

    /* Not set yet: the year the clock starts at after power loss. */
    expect_clock(1970, 0, 3, "--:--");
    expect_clock(STATUS_BAR_MIN_VALID_YEAR - 1, 23, 59, "--:--");

    char buf[STATUS_BAR_CLOCK_LEN];
    status_bar_clock_text(buf, sizeof(buf), NULL);
    assert(strcmp(buf, "--:--") == 0);

    /* A short buffer is cut and still terminated. */
    char small[3] = { 'x', 'x', 'x' };
    struct tm t = at(2026, 14, 5);
    status_bar_clock_text(small, sizeof(small), &t);
    assert(strcmp(small, "14") == 0);

    /* No room at all: nothing is written. */
    char none[1] = { 'x' };
    status_bar_clock_text(none, 0, &t);
    assert(none[0] == 'x');
}

/* Checks that hold for every layout. */
static void check_layout(int bar_w, int char_w, int icon_w, bool batt,
                         const status_bar_layout_t *l)
{
    assert(l->slot_w == 5 * char_w);
    assert(l->text_w >= 0);
    if (batt) {
        /* Battery at the right edge, a 2-character gap, then the clock. */
        assert(l->batt_x + l->slot_w == bar_w - STATUS_BAR_MARGIN);
        assert(l->clock_x + l->slot_w + 2 * char_w == l->batt_x);
    } else {
        assert(l->batt_x == -1);
        assert(l->clock_x + l->slot_w == bar_w - STATUS_BAR_MARGIN);
    }
    int leftmost = l->clock_x;
    if (icon_w > 0) {
        assert(l->icon_x + icon_w + char_w == l->clock_x);
        leftmost = l->icon_x;
    } else {
        assert(l->icon_x == -1);
    }
    /* The text never reaches the indicators. */
    assert(STATUS_BAR_MARGIN + l->text_w < leftmost);
}

static void test_layout(void)
{
    status_bar_layout_t l;

    /* Waveshare RLCD-4.2: 400 px wide, Greybeard 11 (6 px), 11 px icon. */
    status_bar_layout(400, 6, 11, true, &l);
    check_layout(400, 6, 11, true, &l);
    assert(l.batt_x == 368);
    assert(l.clock_x == 326);
    assert(l.icon_x == 309);
    /* The Notes status text keeps the width it had before the clock
     * (400 - 2 - 97). */
    assert(l.text_w == 301);

    /* The planner draws no WiFi icon: its hints get a little more. */
    status_bar_layout(400, 6, 0, true, &l);
    check_layout(400, 6, 0, true, &l);
    assert(l.text_w == 318);
    /* The longest planner hint is 47 characters. */
    assert(l.text_w >= 47 * 6);

    /* A board without a battery monitor. */
    status_bar_layout(400, 6, 11, false, &l);
    check_layout(400, 6, 11, false, &l);
    assert(l.clock_x == 368);

    /* High-density boards: Hack 11 is 11 px per character. */
    status_bar_layout(800, 11, 11, true, &l);
    check_layout(800, 11, 11, true, &l);
    status_bar_layout(800, 11, 0, false, &l);
    check_layout(800, 11, 0, false, &l);

    /* Too narrow for any text: text_w stops at 0. */
    status_bar_layout(60, 6, 11, true, &l);
    assert(l.text_w == 0);
}

int main(void)
{
    test_clock();
    test_layout();
    printf("status_bar: all tests passed\n");
    return 0;
}
