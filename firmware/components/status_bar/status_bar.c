#include "status_bar.h"

#include <stdio.h>

#define SLOT_CHARS 5    /* "14:05", "+100%" */
#define GAP_CHARS  2    /* between the clock and the battery */

void status_bar_clock_text(char *buf, size_t len, const struct tm *now)
{
    if (!buf || len == 0) return;
    if (!now || now->tm_year + 1900 < STATUS_BAR_MIN_VALID_YEAR) {
        snprintf(buf, len, "--:--");
        return;
    }
    snprintf(buf, len, "%02d:%02d", now->tm_hour, now->tm_min);
}

void status_bar_layout(int bar_w, int char_w, int icon_w, bool has_battery,
                       status_bar_layout_t *out)
{
    int right = bar_w - STATUS_BAR_MARGIN;

    out->slot_w = SLOT_CHARS * char_w;
    if (has_battery) {
        out->batt_x = right - out->slot_w;
        right = out->batt_x - GAP_CHARS * char_w;
    } else {
        out->batt_x = -1;
    }
    out->clock_x = right - out->slot_w;
    right = out->clock_x;
    if (icon_w > 0) {
        out->icon_x = right - char_w - icon_w;
        right = out->icon_x;
    } else {
        out->icon_x = -1;
    }
    /* One character of space before whatever is rightmost of the text. */
    out->text_w = right - char_w - STATUS_BAR_MARGIN;
    if (out->text_w < 0) out->text_w = 0;
}
