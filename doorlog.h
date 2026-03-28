/*
 * Tiny append-only log abstraction for arbfiles.
 */
#ifndef DOORLOG_H
#define DOORLOG_H

/* Active log target and enable state. */
struct doorlog {
  int enabled;
  long handle;
  char path[128];
};

/* Log open/write/close helpers. */
int doorlog_open(struct doorlog *log, const char *path, int enabled);
void doorlog_write(struct doorlog *log, const char *text);
void doorlog_writef(struct doorlog *log, const char *format_text, ...);
void doorlog_close(struct doorlog *log);

#endif
