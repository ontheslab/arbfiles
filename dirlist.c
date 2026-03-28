/*
 * Minimal DIRn parser for the first browseable file-list pass.
 *
 * This follows Ami-Express's `dirLineNewFile()` rule closely enough for the
 * plain text listings we need here: a new entry starts on a line whose first
 * character is not a space and not a newline.
 */
#include "dirlist.h"

#include <stdio.h>
#include <string.h>

/* Small DIR-list helpers */
void dirlist_set_error(char *error_text, int error_text_size, const char *message)
{
  if ((error_text == NULL) || (error_text_size <= 0) || (message == NULL)) {
    return;
  }

  strncpy(error_text, message, (size_t) error_text_size - 1U);
  error_text[error_text_size - 1] = '\0';
}

void dirlist_set_source_path(struct dirlist_data *dirlist, const char *source_path)
{
  if ((dirlist == NULL) || (source_path == NULL)) {
    return;
  }

  strncpy(dirlist->source_path, source_path, sizeof(dirlist->source_path) - 1U);
  dirlist->source_path[sizeof(dirlist->source_path) - 1U] = '\0';
}

void dirlist_set_status(struct dirlist_data *dirlist, const char *status_text)
{
  if ((dirlist == NULL) || (status_text == NULL)) {
    return;
  }

  strncpy(dirlist->status_text, status_text, sizeof(dirlist->status_text) - 1U);
  dirlist->status_text[sizeof(dirlist->status_text) - 1U] = '\0';
}

static void dirlist_strip_line_end(char *text)
{
  size_t length;

  if (text == NULL) {
    return;
  }

  length = strlen(text);
  while ((length > 0U) && ((text[length - 1U] == '\n') || (text[length - 1U] == '\r'))) {
    text[length - 1U] = '\0';
    length--;
  }
}

static int dirlist_line_is_new_file(const char *text)
{
  char ch;

  if (text == NULL) {
    return 0;
  }

  ch = text[0];
  return (ch != '\0') && (ch != ' ') && (ch != '\n');
}

/* Entry extraction */
static void dirlist_extract_filename(const char *line, char *output, size_t output_size)
{
  size_t index;

  if ((line == NULL) || (output == NULL) || (output_size == 0U)) {
    return;
  }

  index = 0;
  while ((line[index] != '\0') && (line[index] != ' ') && (index + 1U < output_size)) {
    output[index] = line[index];
    index++;
  }
  output[index] = '\0';
}

static void dirlist_append_description(struct dirlist_entry *entry, const char *line)
{
  size_t used_length;
  size_t remaining;

  if ((entry == NULL) || (line == NULL)) {
    return;
  }

  used_length = strlen(entry->description);
  remaining = sizeof(entry->description) - used_length;
  if (remaining <= 1U) {
    return;
  }

  if (entry->description_line_count > 0) {
    strncat(entry->description, "\n", remaining - 1U);
  }
  used_length = strlen(entry->description);
  remaining = sizeof(entry->description) - used_length;
  if (remaining <= 1U) {
    return;
  }

  strncat(entry->description, line, remaining - 1U);
  entry->description_line_count++;
}

void dirlist_init(struct dirlist_data *dirlist)
{
  if (dirlist == NULL) {
    return;
  }

  memset(dirlist, 0, sizeof(*dirlist));
}

/* Public load path */
int dirlist_load_file(const char *listing_path, struct dirlist_data *dirlist, char *error_text, int error_text_size)
{
  FILE *handle;
  char line[1024];
  struct dirlist_entry *current_entry;

  if ((listing_path == NULL) || (dirlist == NULL)) {
    dirlist_set_error(error_text, error_text_size, "invalid DIR list load request");
    return -1;
  }

  dirlist_init(dirlist);
  strncpy(dirlist->listing_path, listing_path, sizeof(dirlist->listing_path) - 1U);
  dirlist->listing_path[sizeof(dirlist->listing_path) - 1U] = '\0';
  dirlist_set_status(dirlist, "listing loaded");

  handle = fopen(listing_path, "r");
  if (handle == NULL) {
    dirlist_set_error(error_text, error_text_size, "DIR listing could not be opened");
    return -1;
  }

  current_entry = NULL;
  while ((fgets(line, sizeof(line), handle) != NULL) && (dirlist->entry_count < DIRLIST_MAX_ENTRIES)) {
    dirlist_strip_line_end(line);
    if (dirlist_line_is_new_file(line)) {
      current_entry = &dirlist->entries[dirlist->entry_count];
      memset(current_entry, 0, sizeof(*current_entry));
      strncpy(current_entry->header_line, line, sizeof(current_entry->header_line) - 1U);
      current_entry->header_line[sizeof(current_entry->header_line) - 1U] = '\0';
      dirlist_extract_filename(line, current_entry->filename, sizeof(current_entry->filename));
      dirlist_append_description(current_entry, line);
      dirlist->entry_count++;
    } else if ((current_entry != NULL) && (line[0] != '\0')) {
      dirlist_append_description(current_entry, line);
    }
  }

  fclose(handle);
  if (error_text != NULL) {
    error_text[0] = '\0';
  }
  return 0;
}
