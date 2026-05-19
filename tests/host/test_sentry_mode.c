#include <stdio.h>
#include <string.h>

#include "host/sentry_mode.h"

static int failures = 0;

static void expect_mode(const char *name, const char *buf, int fallback, int expected)
{
    int actual = schwung_sentry_mode_parse_buffer(buf, strlen(buf), fallback);
    if (actual != expected) {
        fprintf(stderr, "FAIL: %s expected %d got %d\n", name, expected, actual);
        failures++;
    }
}

int main(void)
{
    expect_mode("session breadcrumb",
                "\x01noise Set MainMode (new state: session)",
                SCHWUNG_SENTRY_MODE_UNKNOWN,
                SCHWUNG_SENTRY_MODE_SESSION);

    expect_mode("latest breadcrumb wins",
                "Set MainMode (new state: session)\n"
                "Set ShiftMode (shift: true, lock: false)\n"
                "Set MainMode (new state: note)",
                SCHWUNG_SENTRY_MODE_UNKNOWN,
                SCHWUNG_SENTRY_MODE_NOTE);

    expect_mode("song overview breadcrumb",
                "prefix Set MainMode (new state: songOverview) suffix",
                SCHWUNG_SENTRY_MODE_NOTE,
                SCHWUNG_SENTRY_MODE_SET_OVERVIEW);

    expect_mode("unknown state keeps fallback",
                "Set MainMode (new state: browser)",
                SCHWUNG_SENTRY_MODE_SESSION,
                SCHWUNG_SENTRY_MODE_SESSION);

    expect_mode("truncated state keeps fallback",
                "Set MainMode (new state:",
                SCHWUNG_SENTRY_MODE_NOTE,
                SCHWUNG_SENTRY_MODE_NOTE);

    if (failures) return 1;
    puts("PASS: sentry mode parser");
    return 0;
}
