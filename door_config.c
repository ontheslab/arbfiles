/*
 * Flat key=value config loader for the Ami file manager door.
 */
#include "door_config.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Local parsing helpers */
static void config_set_error(char *error_text, int error_text_size, const char *message)
{
  if ((error_text == NULL) || (error_text_size <= 0) || (message == NULL)) {
    return;
  }

  strncpy(error_text, message, (size_t) error_text_size - 1U);
  error_text[error_text_size - 1] = '\0';
}

static char *config_trim(char *text)
{
  char *end;

  if (text == NULL) {
    return NULL;
  }

  while ((*text != '\0') && isspace((unsigned char) *text)) {
    text++;
  }

  end = text + strlen(text);
  while ((end > text) && isspace((unsigned char) end[-1])) {
    end--;
  }
  *end = '\0';
  return text;
}

static int config_text_equals(const char *left, const char *right)
{
  unsigned char lch;
  unsigned char rch;

  if ((left == NULL) || (right == NULL)) {
    return 0;
  }

  while ((*left != '\0') && (*right != '\0')) {
    lch = (unsigned char) toupper((unsigned char) *left);
    rch = (unsigned char) toupper((unsigned char) *right);
    if (lch != rch) {
      return 0;
    }
    left++;
    right++;
  }

  return (*left == '\0') && (*right == '\0');
}

static int config_parse_bool(const char *text)
{
  if (text == NULL) {
    return 0;
  }

  if (config_text_equals(text, "1") || config_text_equals(text, "YES") || config_text_equals(text, "TRUE")) {
    return 1;
  }

  return 0;
}

/* Setting application and defaults */
static void config_apply_setting(struct door_config *config, const char *key, const char *value)
{
  if ((config == NULL) || (key == NULL) || (value == NULL)) {
    return;
  }

  if (config_text_equals(key, "bbs_location")) {
    strncpy(config->bbs_location, value, sizeof(config->bbs_location) - 1U);
    config->bbs_location[sizeof(config->bbs_location) - 1U] = '\0';
  } else if (config_text_equals(key, "debug_log")) {
    strncpy(config->debug_log, value, sizeof(config->debug_log) - 1U);
    config->debug_log[sizeof(config->debug_log) - 1U] = '\0';
  } else if (config_text_equals(key, "debug_enabled")) {
    config->debug_enabled = config_parse_bool(value);
  } else if (config_text_equals(key, "disable_paging")) {
    config->disable_paging = config_parse_bool(value);
  } else if (config_text_equals(key, "allow_hold_area")) {
    config->allow_hold_area = config_parse_bool(value);
  } else if (config_text_equals(key, "start_in_current_conf")) {
    config->start_in_current_conf = config_parse_bool(value);
  }
}

void config_set_defaults(struct door_config *config)
{
  if (config == NULL) {
    return;
  }

  memset(config, 0, sizeof(*config));
  strncpy(config->bbs_location, "BBS:", sizeof(config->bbs_location) - 1U);
  config->bbs_location[sizeof(config->bbs_location) - 1U] = '\0';
  strncpy(config->debug_log, "T:arbfiles.log", sizeof(config->debug_log) - 1U);
  config->debug_log[sizeof(config->debug_log) - 1U] = '\0';
  config->debug_enabled = 1;
  config->disable_paging = 1;
  config->allow_hold_area = 1;
  config->start_in_current_conf = 1;
}

/* Public config-file loader */
int config_load_file(const char *path, struct door_config *config, char *error_text, int error_text_size)
{
  FILE *handle;
  char line[512];
  char *trimmed;
  char *separator;
  char *key;
  char *value;

  if ((path == NULL) || (config == NULL)) {
    config_set_error(error_text, error_text_size, "invalid config load request");
    return -1;
  }

  config_set_defaults(config);
  handle = fopen(path, "r");
  if (handle == NULL) {
    config_set_error(error_text, error_text_size, "config file could not be opened");
    return -1;
  }

  while (fgets(line, sizeof(line), handle) != NULL) {
    trimmed = config_trim(line);
    if ((*trimmed == '\0') || (*trimmed == '#') || (*trimmed == ';')) {
      continue;
    }

    separator = strchr(trimmed, '=');
    if (separator == NULL) {
      continue;
    }

    *separator = '\0';
    key = config_trim(trimmed);
    value = config_trim(separator + 1);
    config_apply_setting(config, key, value);
  }

  fclose(handle);
  if (error_text != NULL) {
    error_text[0] = '\0';
  }
  return 0;
}
