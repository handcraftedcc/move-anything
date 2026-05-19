#ifndef SCHWUNG_SENTRY_MODE_H
#define SCHWUNG_SENTRY_MODE_H

#include <stddef.h>

enum {
    SCHWUNG_SENTRY_MODE_UNKNOWN = 0,
    SCHWUNG_SENTRY_MODE_SESSION = 1,
    SCHWUNG_SENTRY_MODE_NOTE = 2,
    SCHWUNG_SENTRY_MODE_SET_OVERVIEW = 3
};

int schwung_sentry_mode_parse_main_mode_token(const char *state);
int schwung_sentry_mode_parse_buffer(const char *buf, size_t len, int fallback);
const char *schwung_sentry_mode_name(int mode);

#endif
