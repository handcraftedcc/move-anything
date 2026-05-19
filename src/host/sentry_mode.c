#include "sentry_mode.h"

#include <string.h>

static const char k_main_mode_marker[] = "Set MainMode (new state:";

int schwung_sentry_mode_parse_main_mode_token(const char *state)
{
    if (!state) return SCHWUNG_SENTRY_MODE_UNKNOWN;
    if (strcmp(state, "session") == 0) return SCHWUNG_SENTRY_MODE_SESSION;
    if (strcmp(state, "note") == 0) return SCHWUNG_SENTRY_MODE_NOTE;
    if (strcmp(state, "songOverview") == 0) return SCHWUNG_SENTRY_MODE_SET_OVERVIEW;
    return SCHWUNG_SENTRY_MODE_UNKNOWN;
}

int schwung_sentry_mode_parse_buffer(const char *buf, size_t len, int fallback)
{
    if (!buf || len == 0) return fallback;

    int mode = fallback;
    size_t marker_len = strlen(k_main_mode_marker);
    for (size_t i = 0; i + marker_len < len; i++) {
        if (memcmp(buf + i, k_main_mode_marker, marker_len) != 0) continue;

        size_t pos = i + marker_len;
        while (pos < len && (buf[pos] == ' ' || buf[pos] == '\t')) pos++;

        char token[32];
        size_t n = 0;
        while (pos < len && n + 1 < sizeof(token)) {
            char c = buf[pos];
            if ((c >= 'a' && c <= 'z') ||
                (c >= 'A' && c <= 'Z') ||
                (c >= '0' && c <= '9') ||
                c == '_' || c == '-') {
                token[n++] = c;
                pos++;
                continue;
            }
            break;
        }
        token[n] = '\0';

        int parsed = schwung_sentry_mode_parse_main_mode_token(token);
        if (parsed != SCHWUNG_SENTRY_MODE_UNKNOWN) {
            mode = parsed;
        }
    }

    return mode;
}

const char *schwung_sentry_mode_name(int mode)
{
    switch (mode) {
    case SCHWUNG_SENTRY_MODE_SESSION:
        return "session";
    case SCHWUNG_SENTRY_MODE_NOTE:
        return "note";
    case SCHWUNG_SENTRY_MODE_SET_OVERVIEW:
        return "songOverview";
    default:
        return "unknown";
    }
}
