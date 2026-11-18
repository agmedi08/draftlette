#pragma once

/* status_bar: the text and the layout of the clock and battery slots at
 * the right end of the bottom status bar. Shared by the Notes screens
 * (editor_ui) and the Today / Week / Month screens (planner_ui), so both
 * put them in the same place.
 *
 * Plain C with no LVGL, so host_test/ can run it on a computer. */

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stddef.h>
#include <time.h>

/* Room for the clock text, "HH:MM" plus the terminator. */
#define STATUS_BAR_CLOCK_LEN 6

/* Before this year the clock has not been set (no RTC time and no sync
 * since boot), so the time is not shown. */
#define STATUS_BAR_MIN_VALID_YEAR 2025

/* Write the 24-hour time of now ("14:05") into buf, or "--:--" when now
 * is NULL or the clock has not been set. buf always ends in a '\0' when
 * len > 0; a short buf gets as much as fits. */
void status_bar_clock_text(char *buf, size_t len, const struct tm *now);

/* Where the right-hand indicators go in a status bar bar_w pixels wide,
 * drawn in a monospaced font char_w pixels per character. All x values
 * are from the left edge of the bar. */
typedef struct {
    int slot_w;     /* width of the clock and of the battery slot */
    int batt_x;     /* left edge of the battery slot; -1 without one */
    int clock_x;    /* left edge of the clock slot */
    int icon_x;     /* left edge of the WiFi icon; -1 without one */
    int text_w;     /* width left for the text at the left end, which
                     * starts at STATUS_BAR_MARGIN */
} status_bar_layout_t;

/* Left and right margin of the bar, in pixels. */
#define STATUS_BAR_MARGIN 2

/* Lay out, from the right: the battery slot (if has_battery), the clock
 * slot, and a WiFi icon icon_w pixels wide (none when icon_w is 0). Each
 * slot holds 5 characters ("14:05", "+100%"), right-aligned. text_w is
 * never negative. */
void status_bar_layout(int bar_w, int char_w, int icon_w, bool has_battery,
                       status_bar_layout_t *out);

#ifdef __cplusplus
}
#endif
