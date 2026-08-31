/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DIPIREC_LOG_H
#define DIPIREC_LOG_H

/* timestamped line (ISO UTC, no 'T') to stderr */
void log_line(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

/* same; ANSI stripped unless terminal wants colour */
void log_line_ansi(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

typedef enum { LOG_COLOR_AUTO, LOG_COLOR_ALWAYS, LOG_COLOR_NEVER } log_color_t;

/* auto = tty and not NO_COLOR / TERM=dumb */
void log_set_color(log_color_t mode);

/* "auto"/"always"/"never" -> log_color_t. 0 ok, -1 invalid */
int log_color_from_string(const char *s, log_color_t *out);

/* scans argv for --color/--color=<val>, before full arg parsing (so early
   argerr()s color correctly too). LOG_COLOR_AUTO if absent or unrecognized */
log_color_t log_color_prescan(int argc, char **argv);

int log_stderr_is_tty(void);
int log_colors_enabled(void);

/* for log_line_ansi payloads */
#define LOG_RESET  "\033[0m"
#define LOG_BOLD   "\033[1m"
#define LOG_DIM    "\033[2m"
#define LOG_RED    "\033[31m"
#define LOG_GREEN  "\033[32m"
#define LOG_YELLOW "\033[33m"
#define LOG_BLUE   "\033[34m"
#define LOG_CYAN   "\033[36m"

#endif
