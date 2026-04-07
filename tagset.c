/*
 * Tag tracking for paging and larger batch work.
 */
#include "tagset.h"
#include "dirlist.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Small local helpers */
static void tagset_set_error(char *error_text, int error_text_size, const char *message)
{
  if ((error_text == NULL) || (error_text_size <= 0) || (message == NULL)) {
    return;
  }

  strncpy(error_text, message, (size_t) error_text_size - 1U);
  error_text[error_text_size - 1] = '\0';
}

static void tagset_extract_filename(const char *line, char *output, size_t output_size)
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

static void tagset_strip_line_end(char *text)
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

static int tagset_line_is_new_file(const char *text)
{
  char ch;

  if (text == NULL) {
    return 0;
  }

  ch = text[0];
  return (ch != '\0') && (ch != ' ') && (ch != '\n') && (ch != '\r');
}

static int tagset_ensure_capacity(struct tagset_data *tagset, int needed_count)
{
  struct tagset_entry *grown_entries;
  int target_capacity;

  if (tagset == NULL) {
    return -1;
  }

  if (needed_count <= tagset->capacity) {
    return 0;
  }

  target_capacity = tagset->capacity;
  if (target_capacity <= 0) {
    target_capacity = TAGSET_GROW_CHUNK;
  }

  while (target_capacity < needed_count) {
    target_capacity += TAGSET_GROW_CHUNK;
  }

  grown_entries = (struct tagset_entry *) realloc(tagset->entries,
                                                  (size_t) target_capacity * sizeof(struct tagset_entry));
  if (grown_entries == NULL) {
    return -1;
  }

  if (target_capacity > tagset->capacity) {
    memset(&grown_entries[tagset->capacity],
           0,
           (size_t) (target_capacity - tagset->capacity) * sizeof(struct tagset_entry));
  }

  tagset->entries = grown_entries;
  tagset->capacity = target_capacity;
  return 0;
}

/* Public tag-set helpers */
void tagset_init(struct tagset_data *tagset)
{
  if (tagset == NULL) {
    return;
  }

  memset(tagset, 0, sizeof(*tagset));
}

void tagset_free(struct tagset_data *tagset)
{
  if (tagset == NULL) {
    return;
  }

  if (tagset->entries != NULL) {
    free(tagset->entries);
    tagset->entries = NULL;
  }
  memset(tagset, 0, sizeof(*tagset));
}

void tagset_clear(struct tagset_data *tagset)
{
  if (tagset == NULL) {
    return;
  }

  tagset->count = 0;
}

void tagset_reset_scope(struct tagset_data *tagset, int source_conf_number, int source_area, const char *listing_path)
{
  if (tagset == NULL) {
    return;
  }

  tagset_clear(tagset);
  tagset->source_conf_number = source_conf_number;
  tagset->source_area = source_area;
  if (listing_path != NULL) {
    strncpy(tagset->listing_path, listing_path, sizeof(tagset->listing_path) - 1U);
    tagset->listing_path[sizeof(tagset->listing_path) - 1U] = '\0';
  } else {
    tagset->listing_path[0] = '\0';
  }
}

int tagset_count(const struct tagset_data *tagset)
{
  return tagset != NULL ? tagset->count : 0;
}

int tagset_contains(const struct tagset_data *tagset, const char *filename)
{
  int index;

  if ((tagset == NULL) || (filename == NULL) || (*filename == '\0')) {
    return 0;
  }

  for (index = 0; index < tagset->count; index++) {
    if (strcmp(tagset->entries[index].filename, filename) == 0) {
      return 1;
    }
  }

  return 0;
}

int tagset_add(struct tagset_data *tagset, const char *filename)
{
  if ((tagset == NULL) || (filename == NULL) || (*filename == '\0')) {
    return -1;
  }

  if (tagset_contains(tagset, filename)) {
    return 0;
  }

  if (tagset_ensure_capacity(tagset, tagset->count + 1) != 0) {
    return -1;
  }

  strncpy(tagset->entries[tagset->count].filename, filename, sizeof(tagset->entries[tagset->count].filename) - 1U);
  tagset->entries[tagset->count].filename[sizeof(tagset->entries[tagset->count].filename) - 1U] = '\0';
  tagset->count++;
  return 1;
}

int tagset_remove(struct tagset_data *tagset, const char *filename)
{
  int index;

  if ((tagset == NULL) || (filename == NULL) || (*filename == '\0')) {
    return 0;
  }

  for (index = 0; index < tagset->count; index++) {
    if (strcmp(tagset->entries[index].filename, filename) == 0) {
      if (index + 1 < tagset->count) {
        memmove(&tagset->entries[index],
                &tagset->entries[index + 1],
                (size_t) (tagset->count - index - 1) * sizeof(struct tagset_entry));
      }
      tagset->count--;
      return 1;
    }
  }

  return 0;
}

int tagset_toggle(struct tagset_data *tagset, const char *filename)
{
  if (tagset_remove(tagset, filename)) {
    return 0;
  }

  if (tagset_add(tagset, filename) < 0) {
    return -1;
  }

  return 1;
}

void tagset_apply_to_dirlist(const struct tagset_data *tagset, struct dirlist_data *dirlist)
{
  int index;

  if (dirlist == NULL) {
    return;
  }

  for (index = 0; index < dirlist->entry_count; index++) {
    dirlist->entries[index].tagged = tagset_contains(tagset, dirlist->entries[index].filename) ? 1 : 0;
  }
}

int tagset_add_all_from_dirlist(struct tagset_data *tagset, const struct dirlist_data *dirlist)
{
  int index;
  int added_count;

  if ((tagset == NULL) || (dirlist == NULL)) {
    return -1;
  }

  added_count = 0;
  for (index = 0; index < dirlist->entry_count; index++) {
    if (tagset_add(tagset, dirlist->entries[index].filename) < 0) {
      return -1;
    }
    added_count++;
  }

  return added_count;
}

int tagset_add_all_from_listing(struct tagset_data *tagset,
                                const char *listing_path,
                                char *error_text,
                                int error_text_size)
{
  FILE *handle;
  char line[1024];
  char filename[32];
  int added_count;

  if ((tagset == NULL) || (listing_path == NULL) || (*listing_path == '\0')) {
    tagset_set_error(error_text, error_text_size, "invalid DIR tag request");
    return -1;
  }

  handle = fopen(listing_path, "r");
  if (handle == NULL) {
    tagset_set_error(error_text, error_text_size, "DIR listing could not be opened for tagging");
    return -1;
  }

  added_count = 0;
  while (fgets(line, sizeof(line), handle) != NULL) {
    tagset_strip_line_end(line);
    if (!tagset_line_is_new_file(line)) {
      continue;
    }

    tagset_extract_filename(line, filename, sizeof(filename));
    if (filename[0] == '\0') {
      continue;
    }

    if (tagset_add(tagset, filename) < 0) {
      fclose(handle);
      tagset_set_error(error_text, error_text_size, "tag list could not grow");
      return -1;
    }
    added_count++;
  }

  fclose(handle);
  if (error_text != NULL) {
    error_text[0] = '\0';
  }
  return added_count;
}
