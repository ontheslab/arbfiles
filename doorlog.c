/*
 * Lightweight Amiga-side logger.
 *
 * Native DOS file I/O is used so the log remains usable in normal door launch
 * environments.
 */
#include "doorlog.h"

#include <dos/dos.h>
#include <proto/dos.h>

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

/* Handle and path helpers */
static BPTR doorlog_file(const struct doorlog *log)
{
  if ((log == NULL) || !log->enabled || (log->handle == 0)) {
    return 0;
  }

  return (BPTR) log->handle;
}

static BPTR doorlog_open_path(const char *path)
{
  BPTR handle;

  if ((path == NULL) || (*path == '\0')) {
    return 0;
  }

  handle = Open(path, MODE_READWRITE);
  if (handle == 0) {
    handle = Open(path, MODE_NEWFILE);
  }

  return handle;
}

static void doorlog_write_line(BPTR handle, const char *text)
{
  if ((handle == 0) || (text == NULL)) {
    return;
  }

  Write(handle, text, (LONG) strlen(text));
  Write(handle, "\n", 1);
}

/* Public logging API */
int doorlog_open(struct doorlog *log, const char *path, int enabled)
{
  BPTR handle;
  const char *fallback_path;

  if (log == NULL) {
    return -1;
  }

  memset(log, 0, sizeof(*log));
  if (!enabled || (path == NULL) || (*path == '\0')) {
    return 0;
  }

  handle = doorlog_open_path(path);
  if (handle != 0) {
    strncpy(log->path, path, sizeof(log->path) - 1U);
    log->path[sizeof(log->path) - 1U] = '\0';
  }

  if (handle == 0) {
    fallback_path = "T:arbfiles.log";
    handle = doorlog_open_path(fallback_path);
    if (handle != 0) {
      strncpy(log->path, fallback_path, sizeof(log->path) - 1U);
      log->path[sizeof(log->path) - 1U] = '\0';
    }
  }

  if (handle == 0) {
    fallback_path = "arbfiles.log";
    handle = doorlog_open_path(fallback_path);
    if (handle != 0) {
      strncpy(log->path, fallback_path, sizeof(log->path) - 1U);
      log->path[sizeof(log->path) - 1U] = '\0';
    }
  }

  if (handle == 0) {
    return -1;
  }

  Seek(handle, 0, OFFSET_END);
  log->enabled = 1;
  log->handle = (long) handle;
  doorlog_write_line(handle, "---- door log opened ----");
  doorlog_writef(log, "Log path %s", log->path);
  return 0;
}

void doorlog_write(struct doorlog *log, const char *text)
{
  BPTR handle;

  handle = doorlog_file(log);
  if (handle == 0) {
    return;
  }

  doorlog_write_line(handle, text);
}

void doorlog_writef(struct doorlog *log, const char *format_text, ...)
{
  BPTR handle;
  char buffer[256];
  va_list argument_list;

  handle = doorlog_file(log);
  if ((handle == 0) || (format_text == NULL)) {
    return;
  }

  va_start(argument_list, format_text);
  vsnprintf(buffer, sizeof(buffer), format_text, argument_list);
  va_end(argument_list);
  doorlog_write_line(handle, buffer);
}

void doorlog_close(struct doorlog *log)
{
  BPTR handle;

  handle = doorlog_file(log);
  if (handle != 0) {
    doorlog_write_line(handle, "---- door log closed ----");
    Close(handle);
  }

  if (log != NULL) {
    log->enabled = 0;
    log->handle = 0;
  }
}
