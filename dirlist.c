/*
 * DIRn parsing and loaded-block handling.
 *
 * This follows Ami-Express's `dirLineNewFile()` rule closely enough for the
 * plain text listings we need here: a new entry starts on a line whose first
 * character is not a space and not a newline.
 */
#include "dirlist.h"

#include <stdlib.h>
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
  return (ch != '\0') && (ch != ' ') && (ch != '\n') && (ch != '\r');
}

/* Entry extraction */
static void dirlist_extract_filename(const char *line, char *output, size_t output_size)
{
  size_t index;

  if ((line == NULL) || (output == NULL) || (output_size == 0U)) {
    return;
  }

  index = 0;
  while ((line[index] != '\0') &&
         (line[index] != ' ') &&
         (line[index] != '\n') &&
         (line[index] != '\r') &&
         (index + 1U < output_size)) {
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

static int dirlist_ensure_capacity(struct dirlist_data *dirlist, int needed_count, int working_limit)
{
  struct dirlist_entry *grown_entries;
  int target_capacity;

  if (dirlist == NULL) {
    return -1;
  }

  if (needed_count <= dirlist->entry_capacity) {
    return 0;
  }

  if (working_limit <= 0) {
    working_limit = DIRLIST_DEFAULT_BLOCK_SIZE;
  }
  if (working_limit > DIRLIST_MAX_BLOCK_SIZE) {
    working_limit = DIRLIST_MAX_BLOCK_SIZE;
  }

  if (needed_count > working_limit) {
    return -1;
  }

  target_capacity = dirlist->entry_capacity;
  if (target_capacity <= 0) {
    target_capacity = DIRLIST_GROW_CHUNK;
  }

  while (target_capacity < needed_count) {
    target_capacity += DIRLIST_GROW_CHUNK;
  }

  if (target_capacity > working_limit) {
    target_capacity = working_limit;
  }

  grown_entries = (struct dirlist_entry *) realloc(dirlist->entries,
                                                   (size_t) target_capacity * sizeof(struct dirlist_entry));
  if (grown_entries == NULL) {
    return -1;
  }

  if (target_capacity > dirlist->entry_capacity) {
    memset(&grown_entries[dirlist->entry_capacity],
           0,
           (size_t) (target_capacity - dirlist->entry_capacity) * sizeof(struct dirlist_entry));
  }

  dirlist->entries = grown_entries;
  dirlist->entry_capacity = target_capacity;
  return 0;
}

int dirlist_find_entry_by_filename(const struct dirlist_data *dirlist, const char *filename)
{
  int index;

  if ((dirlist == NULL) || (filename == NULL) || (*filename == '\0')) {
    return -1;
  }

  for (index = 0; index < dirlist->entry_count; index++) {
    if (strcmp(dirlist->entries[index].filename, filename) == 0) {
      return index;
    }
  }

  return -1;
}

int dirlist_add_synthetic_entry(struct dirlist_data *dirlist,
                                const char *filename,
                                const char *header_text,
                                const char *description_text)
{
  struct dirlist_entry *entry;

  if ((dirlist == NULL) || (filename == NULL) || (*filename == '\0')) {
    return -1;
  }

  if (dirlist_ensure_capacity(dirlist, dirlist->entry_count + 1, DIRLIST_MAX_BLOCK_SIZE) != 0) {
    return -1;
  }

  entry = &dirlist->entries[dirlist->entry_count];
  memset(entry, 0, sizeof(*entry));
  entry->has_listing_entry = 0;
  strncpy(entry->filename, filename, sizeof(entry->filename) - 1U);
  entry->filename[sizeof(entry->filename) - 1U] = '\0';

  if ((header_text != NULL) && (*header_text != '\0')) {
    strncpy(entry->header_line, header_text, sizeof(entry->header_line) - 1U);
    entry->header_line[sizeof(entry->header_line) - 1U] = '\0';
  } else {
    strncpy(entry->header_line, filename, sizeof(entry->header_line) - 1U);
    entry->header_line[sizeof(entry->header_line) - 1U] = '\0';
  }

  if ((description_text != NULL) && (*description_text != '\0')) {
    strncpy(entry->description, description_text, sizeof(entry->description) - 1U);
    entry->description[sizeof(entry->description) - 1U] = '\0';
    entry->description_line_count = 1;
  }

  dirlist->entry_count++;
  return dirlist->entry_count - 1;
}

void dirlist_clear_tags(struct dirlist_data *dirlist)
{
  int index;

  if (dirlist == NULL) {
    return;
  }

  for (index = 0; index < dirlist->entry_count; index++) {
    dirlist->entries[index].tagged = 0;
  }
}

int dirlist_count_tags(const struct dirlist_data *dirlist)
{
  int index;
  int count;

  if (dirlist == NULL) {
    return 0;
  }

  count = 0;
  for (index = 0; index < dirlist->entry_count; index++) {
    if (dirlist->entries[index].tagged) {
      count++;
    }
  }

  return count;
}

int dirlist_toggle_tag(struct dirlist_data *dirlist, int entry_index)
{
  if ((dirlist == NULL) || (entry_index < 0) || (entry_index >= dirlist->entry_count)) {
    return 0;
  }

  dirlist->entries[entry_index].tagged = !dirlist->entries[entry_index].tagged;
  return dirlist->entries[entry_index].tagged;
}

void dirlist_set_all_tags(struct dirlist_data *dirlist, int tagged_value)
{
  int index;

  if (dirlist == NULL) {
    return;
  }

  for (index = 0; index < dirlist->entry_count; index++) {
    dirlist->entries[index].tagged = tagged_value ? 1 : 0;
  }
}

void dirlist_init(struct dirlist_data *dirlist)
{
  if (dirlist == NULL) {
    return;
  }

  dirlist_free(dirlist);
  memset(dirlist, 0, sizeof(*dirlist));
}

void dirlist_free(struct dirlist_data *dirlist)
{
  if (dirlist == NULL) {
    return;
  }

  if (dirlist->entries != NULL) {
    free(dirlist->entries);
    dirlist->entries = NULL;
  }
  dirlist->entry_capacity = 0;
  dirlist->entry_count = 0;
}

/* Public load entry points */
int dirlist_load_window_file(const char *listing_path,
                             long start_entry,
                             int max_entries,
                             struct dirlist_data *dirlist,
                             char *error_text,
                             int error_text_size)
{
  FILE *handle;
  char line[1024];
  char status_text[80];
  struct dirlist_entry *current_entry;
  int truncated;
  long current_entry_index;
  int reached_window_end;

  if ((listing_path == NULL) || (dirlist == NULL)) {
    dirlist_set_error(error_text, error_text_size, "invalid DIR list load request");
    return -1;
  }

  if (start_entry < 0L) {
    start_entry = 0L;
  }

  if (max_entries <= 0) {
    max_entries = DIRLIST_DEFAULT_BLOCK_SIZE;
  }
  if (max_entries > DIRLIST_MAX_BLOCK_SIZE) {
    max_entries = DIRLIST_MAX_BLOCK_SIZE;
  }

  dirlist_init(dirlist);
  dirlist->window_start_entry = start_entry;
  dirlist->window_entry_limit = max_entries;
  strncpy(dirlist->listing_path, listing_path, sizeof(dirlist->listing_path) - 1U);
  dirlist->listing_path[sizeof(dirlist->listing_path) - 1U] = '\0';
  dirlist_set_status(dirlist, "listing loaded");

  handle = fopen(listing_path, "r");
  if (handle == NULL) {
    dirlist_set_error(error_text, error_text_size, "DIR listing could not be opened");
    return -1;
  }

  current_entry = NULL;
  truncated = 0;
  current_entry_index = -1;
  reached_window_end = 0;
  while (fgets(line, sizeof(line), handle) != NULL) {
    dirlist_strip_line_end(line);
    if (dirlist_line_is_new_file(line)) {
      current_entry_index++;
      dirlist->total_entries_seen = current_entry_index + 1;

      if (current_entry_index < start_entry) {
        current_entry = NULL;
        continue;
      }

      if (dirlist->entry_count >= max_entries) {
        dirlist->has_next_window = 1;
        reached_window_end = 1;
        break;
      }

      if (dirlist_ensure_capacity(dirlist, dirlist->entry_count + 1, max_entries) != 0) {
        truncated = 1;
        break;
      }
      current_entry = &dirlist->entries[dirlist->entry_count];
      memset(current_entry, 0, sizeof(*current_entry));
      current_entry->has_listing_entry = 1;
      strncpy(current_entry->header_line, line, sizeof(current_entry->header_line) - 1U);
      current_entry->header_line[sizeof(current_entry->header_line) - 1U] = '\0';
      dirlist_extract_filename(line, current_entry->filename, sizeof(current_entry->filename));
      dirlist_append_description(current_entry, line);
      dirlist->entry_count++;
    } else if ((current_entry != NULL) && (line[0] != '\0') && (current_entry_index >= start_entry)) {
      dirlist_append_description(current_entry, line);
    }
  }

  if (start_entry > 0L) {
    dirlist->has_previous_window = 1;
  }

  if (!feof(handle) && !reached_window_end) {
    truncated = 1;
  }

  if (feof(handle) && (dirlist->total_entries_seen < current_entry_index + 1)) {
    dirlist->total_entries_seen = current_entry_index + 1;
  }

  fclose(handle);
  if (truncated) {
    snprintf(status_text, sizeof(status_text), "listing truncated at %d entries", max_entries);
    dirlist_set_status(dirlist, status_text);
  } else if (dirlist->has_next_window) {
    snprintf(status_text,
             sizeof(status_text),
             "showing files %ld-%ld of at least %ld",
             dirlist->window_start_entry + 1L,
             dirlist->window_start_entry + dirlist->entry_count,
             dirlist->window_start_entry + dirlist->entry_count + 1L);
    dirlist_set_status(dirlist, status_text);
  } else if (dirlist->has_previous_window || (dirlist->window_start_entry > 0L)) {
    snprintf(status_text,
             sizeof(status_text),
             "showing files %ld-%ld of %ld",
             dirlist->window_start_entry + 1L,
             dirlist->window_start_entry + dirlist->entry_count,
             dirlist->total_entries_seen);
    dirlist_set_status(dirlist, status_text);
  }
  if (error_text != NULL) {
    error_text[0] = '\0';
  }
  return 0;
}

int dirlist_load_file(const char *listing_path, struct dirlist_data *dirlist, char *error_text, int error_text_size)
{
  return dirlist_load_window_file(listing_path,
                                  0L,
                                  DIRLIST_DEFAULT_BLOCK_SIZE,
                                  dirlist,
                                  error_text,
                                  error_text_size);
}
