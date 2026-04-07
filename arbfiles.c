/*
 * arbfiles main control flow.
 *
 * This module owns the top-level door run, conference loading, browse state,
 * and move/delete work.
 *
 * Ref: the overall door shape follows the working arblink pattern, while the
 * conference and area handling here is matched against real Ami-Express data.
 */
#include "aedoor_bridge.h"
#include "ae_config_scan.h"
#include "dirlist.h"
#include "door_config.h"
#include "door_version.h"
#include "doorlog.h"
#include "file_ops.h"
#include "tagset.h"
#include "ui.h"

#include <dos/dos.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <proto/dos.h>

/*
 * Keep the bigger working data out of the 68k stack frame. vbcc quickly runs
 * out of room once DIR listings and conference data are both local to main().
 */
static struct ae_system_config g_system_config;
static struct ae_current_conference_info g_current_conference;
static struct dirlist_data g_dirlist;
static struct ae_current_conference_info g_destination_conference;
static struct dirlist_data g_destination_dirlist;
static struct tagset_data g_tagset;

/* Conference and storage helpers */

static int conference_has_hold_area(const struct door_config *config,
                                    const struct ae_current_conference_info *conference)
{
  return (config != NULL) &&
         (config->allow_hold_area != 0) &&
         (conference != NULL) &&
         (conference->base.location[0] != '\0');
}

static int conference_has_trash_area(const struct door_config *config)
{
  return (config != NULL) && (config->trash_path[0] != '\0');
}

static int conference_get_trash_area_index(const struct door_config *config,
                                           const struct ae_current_conference_info *conference)
{
  if ((conference == NULL) || !conference_has_trash_area(config)) {
    return 0;
  }

  return conference->base.dir_count + (conference_has_hold_area(config, conference) ? 2 : 1);
}

static int conference_get_max_area(const struct door_config *config,
                                   const struct ae_current_conference_info *conference)
{
  int max_area;

  if (conference == NULL) {
    return 0;
  }

  max_area = conference->base.dir_count;
  if (conference_has_hold_area(config, conference)) {
    max_area++;
  }
  if (conference_has_trash_area(config)) {
    max_area++;
  }
  return max_area;
}

static int conference_get_download_folder_count(const struct ae_current_conference_info *conference)
{
  if (conference == NULL) {
    return 0;
  }

  return conference->base.download_path_count;
}

static const char *conference_get_download_folder_path(const struct ae_current_conference_info *conference,
                                                       int folder_index)
{
  if ((conference == NULL) ||
      (folder_index < 1) ||
      (folder_index > conference->base.download_path_count) ||
      (conference->download_paths[folder_index - 1][0] == '\0')) {
    return NULL;
  }

  return conference->download_paths[folder_index - 1];
}

static int conference_get_populated_download_folder_count(const struct ae_current_conference_info *conference)
{
  int index;
  int count;

  if (conference == NULL) {
    return 0;
  }

  count = 0;
  for (index = 0; index < conference->base.download_path_count; index++) {
    if (conference->download_paths[index][0] != '\0') {
      count++;
    }
  }

  return count;
}

static int conference_get_download_folder_slot_for_order(const struct ae_current_conference_info *conference,
                                                         int folder_order)
{
  int index;
  int count;

  if ((conference == NULL) || (folder_order < 1)) {
    return 0;
  }

  count = 0;
  for (index = 0; index < conference->base.download_path_count; index++) {
    if (conference->download_paths[index][0] == '\0') {
      continue;
    }

    count++;
    if (count == folder_order) {
      return index + 1;
    }
  }

  return 0;
}

static int conference_paths_match(const char *left, const char *right)
{
  unsigned char left_char;
  unsigned char right_char;

  if ((left == NULL) || (right == NULL)) {
    return 0;
  }

  while ((*left != '\0') && (*right != '\0')) {
    left_char = (unsigned char) *left++;
    right_char = (unsigned char) *right++;
    if (toupper(left_char) != toupper(right_char)) {
      return 0;
    }
  }

  return (*left == '\0') && (*right == '\0');
}

static int conference_uses_rotated_area_folder_map(const struct ae_current_conference_info *conference)
{
  if (conference == NULL) {
    return 0;
  }

  if ((conference->base.dir_count <= 1) ||
      (conference_get_populated_download_folder_count(conference) < conference->base.dir_count) ||
      (conference->base.upload_path_count <= 0)) {
    return 0;
  }

  if ((conference_get_download_folder_slot_for_order(conference, 1) <= 0) ||
      (conference->upload_paths[0][0] == '\0')) {
    return 0;
  }

  return conference_paths_match(conference->download_paths[conference_get_download_folder_slot_for_order(conference, 1) - 1],
                                conference->upload_paths[0]);
}

static int conference_get_preferred_folder_for_area(const struct ae_current_conference_info *conference,
                                                    int active_area)
{
  int rotated_index;

  if (conference == NULL) {
    return 1;
  }

  if ((active_area < 1) || (active_area > conference->base.dir_count)) {
    return 1;
  }

  if (conference_uses_rotated_area_folder_map(conference)) {
    rotated_index = active_area + 1;
    if (rotated_index > conference->base.dir_count) {
      rotated_index = 1;
    }
    rotated_index = conference_get_download_folder_slot_for_order(conference, rotated_index);
    if ((rotated_index >= 1) &&
        (rotated_index <= conference->base.download_path_count) &&
        (conference->download_paths[rotated_index - 1][0] != '\0')) {
      return rotated_index;
    }
  }

  rotated_index = conference_get_download_folder_slot_for_order(conference, active_area);
  if ((rotated_index >= 1) &&
      (rotated_index <= conference->base.download_path_count) &&
      (conference->download_paths[rotated_index - 1][0] != '\0')) {
    return rotated_index;
  }

  if (conference->base.download_path_count > 0) {
    for (rotated_index = 1; rotated_index <= conference->base.download_path_count; rotated_index++) {
      if (conference->download_paths[rotated_index - 1][0] != '\0') {
        return rotated_index;
      }
    }
  }

  return 1;
}

static void join_path(char *output, size_t output_size, const char *base_path, const char *leaf_name);

static int file_exists_at_path(const char *path)
{
  FILE *handle;

  if ((path == NULL) || (*path == '\0')) {
    return 0;
  }

  handle = fopen(path, "rb");
  if (handle == NULL) {
    return 0;
  }

  fclose(handle);
  return 1;
}

static int name_equals_ignore_case(const char *left, const char *right)
{
  unsigned char left_char;
  unsigned char right_char;

  if ((left == NULL) || (right == NULL)) {
    return 0;
  }

  while ((*left != '\0') && (*right != '\0')) {
    left_char = (unsigned char) *left++;
    right_char = (unsigned char) *right++;
    if (toupper(left_char) != toupper(right_char)) {
      return 0;
    }
  }

  return (*left == '\0') && (*right == '\0');
}

static void record_source_match(const char *folder_path,
                                char *matched_folder,
                                size_t matched_folder_size,
                                int *match_count)
{
  if ((folder_path == NULL) || (*folder_path == '\0') ||
      (matched_folder == NULL) || (matched_folder_size == 0U) ||
      (match_count == NULL)) {
    return;
  }

  if (*match_count <= 0) {
    strncpy(matched_folder, folder_path, matched_folder_size - 1U);
    matched_folder[matched_folder_size - 1U] = '\0';
    *match_count = 1;
    return;
  }

  if (conference_paths_match(matched_folder, folder_path)) {
    return;
  }

  *match_count = *match_count + 1;
}

static void search_source_file_recursive(const char *folder_path,
                                         const char *filename,
                                         char *matched_folder,
                                         size_t matched_folder_size,
                                         int *match_count,
                                         int depth)
{
  char candidate_path[256];
  BPTR lock;
  struct FileInfoBlock *fib;

  if ((folder_path == NULL) || (*folder_path == '\0') ||
      (filename == NULL) || (*filename == '\0') ||
      (matched_folder == NULL) || (matched_folder_size == 0U) ||
      (match_count == NULL) || (depth > 8) || (*match_count > 1)) {
    return;
  }

  join_path(candidate_path, sizeof(candidate_path), folder_path, filename);
  if (file_exists_at_path(candidate_path)) {
    record_source_match(folder_path, matched_folder, matched_folder_size, match_count);
    return;
  }

  lock = Lock((STRPTR) folder_path, ACCESS_READ);
  if (lock == 0) {
    return;
  }

  fib = AllocDosObject(DOS_FIB, NULL);
  if (fib != NULL) {
    if (Examine(lock, fib) != DOSFALSE) {
      while ((*match_count <= 1) && (ExNext(lock, fib) != DOSFALSE)) {
        if (fib->fib_DirEntryType > 0) {
          const char *entry_name;

          entry_name = (const char *) fib->fib_FileName;
          if ((entry_name == NULL) || (*entry_name == '\0') ||
              (strcmp(entry_name, ".") == 0) || (strcmp(entry_name, "..") == 0)) {
            continue;
          }

          join_path(candidate_path, sizeof(candidate_path), folder_path, entry_name);
          search_source_file_recursive(candidate_path,
                                       filename,
                                       matched_folder,
                                       matched_folder_size,
                                       match_count,
                                       depth + 1);
        } else if (fib->fib_DirEntryType < 0) {
          if (name_equals_ignore_case((const char *) fib->fib_FileName, filename)) {
            record_source_match(folder_path, matched_folder, matched_folder_size, match_count);
          }
        }
      }
    }
    FreeDosObject(DOS_FIB, fib);
  }

  UnLock(lock);
}

/* Config file handling */

static int load_config_with_fallbacks(const char **used_path,
                                      const char *requested_path,
                                      struct door_config *config,
                                      char *error_text,
                                      int error_text_size)
{
  static const char *default_paths[] = {
    "PROGDIR:arbfiles.cfg",
    "arbfiles.cfg"
  };
  int status;
  int index;

  if ((used_path == NULL) || (config == NULL)) {
    return -1;
  }

  if ((requested_path != NULL) && (*requested_path != '\0')) {
    status = config_load_file(requested_path, config, error_text, error_text_size);
    if (status == 0) {
      *used_path = requested_path;
      return 0;
    }
  }

  for (index = 0; index < (int) (sizeof(default_paths) / sizeof(default_paths[0])); index++) {
    status = config_load_file(default_paths[index], config, error_text, error_text_size);
    if (status == 0) {
      *used_path = default_paths[index];
      return 0;
    }
  }

  *used_path = requested_path != NULL ? requested_path : default_paths[0];
  config_set_defaults(config);
  return -1;
}

/* General path helpers */
static int path_looks_like_config(const char *text)
{
  size_t text_length;

  if ((text == NULL) || (*text == '\0')) {
    return 0;
  }

  if ((strchr(text, ':') != NULL) || (strchr(text, '/') != NULL) || (strchr(text, '\\') != NULL)) {
    return 1;
  }

  text_length = strlen(text);
  if ((text_length >= 4U) && (strcmp(text + text_length - 4U, ".cfg") == 0)) {
    return 1;
  }

  return 0;
}

static void join_path(char *output, size_t output_size, const char *base_path, const char *leaf_name)
{
  size_t text_length;

  if ((output == NULL) || (output_size == 0U)) {
    return;
  }

  output[0] = '\0';
  if ((base_path == NULL) || (*base_path == '\0') || (leaf_name == NULL)) {
    return;
  }

  text_length = strlen(base_path);
  if ((text_length > 0U) && ((base_path[text_length - 1U] == '/') || (base_path[text_length - 1U] == ':'))) {
    snprintf(output, output_size, "%s%s", base_path, leaf_name);
  } else {
    snprintf(output, output_size, "%s/%s", base_path, leaf_name);
  }
}

static int try_load_listing_path(const char *base_path,
                                 const char *leaf_name,
                                 struct dirlist_data *dirlist,
                                 char *error_text,
                                 int error_text_size)
{
  char listing_path[256];

  join_path(listing_path, sizeof(listing_path), base_path, leaf_name);
  return dirlist_load_file(listing_path, dirlist, error_text, error_text_size);
}

static int trash_name_equals(const char *left, const char *right)
{
  unsigned char left_char;
  unsigned char right_char;

  if ((left == NULL) || (right == NULL)) {
    return 0;
  }

  while ((*left != '\0') && (*right != '\0')) {
    left_char = (unsigned char) *left++;
    right_char = (unsigned char) *right++;
    if (toupper(left_char) != toupper(right_char)) {
      return 0;
    }
  }

  return (*left == '\0') && (*right == '\0');
}

static int trash_name_ends_with(const char *text, const char *suffix)
{
  size_t text_length;
  size_t suffix_length;

  if ((text == NULL) || (suffix == NULL)) {
    return 0;
  }

  text_length = strlen(text);
  suffix_length = strlen(suffix);
  if ((suffix_length == 0U) || (text_length < suffix_length)) {
    return 0;
  }

  return trash_name_equals(text + text_length - suffix_length, suffix);
}

static int trash_name_is_internal(const char *name)
{
  if ((name == NULL) || (*name == '\0')) {
    return 1;
  }

  if (trash_name_equals(name, "DIR1")) {
    return 1;
  }

  if (trash_name_ends_with(name, ".info") ||
      trash_name_ends_with(name, ".afdtmp") ||
      trash_name_ends_with(name, ".afdbak") ||
      trash_name_ends_with(name, ".afddel") ||
      trash_name_ends_with(name, ".afdrst")) {
    return 1;
  }

  return 0;
}

/* Conference data and listing loading */

static int load_numbered_area_listing(const struct ae_current_conference_info *conference,
                                      int active_area,
                                      struct dirlist_data *dirlist,
                                      char *error_text,
                                      int error_text_size)
{
  char listing_name[32];
  static const char *prefixes[] = {
    "DIR",
    "Dir",
    "dir"
  };
  int index;

  for (index = 0; index < (int) (sizeof(prefixes) / sizeof(prefixes[0])); index++) {
    snprintf(listing_name, sizeof(listing_name), "%s%d", prefixes[index], active_area);
    if (try_load_listing_path(conference->base.location,
                              listing_name,
                              dirlist,
                              error_text,
                              error_text_size) == 0) {
      return 0;
    }
  }

  dirlist_init(dirlist);
  dirlist_set_status(dirlist, "listing not found");
  dirlist_set_source_path(dirlist, conference->base.location);
  dirlist_set_error(error_text, error_text_size, "DIR listing could not be opened");
  return -1;
}

static int load_hold_area_listing(const struct ae_current_conference_info *conference,
                                  struct dirlist_data *dirlist,
                                  char *error_text,
                                  int error_text_size)
{
  static const char *hold_dirs[] = {
    "Hold",
    "HOLD",
    "hold"
  };
  static const char *held_files[] = {
    "Held",
    "HELD",
    "held"
  };
  char hold_path[256];
  int hold_index;
  int held_index;

  for (hold_index = 0; hold_index < (int) (sizeof(hold_dirs) / sizeof(hold_dirs[0])); hold_index++) {
    join_path(hold_path, sizeof(hold_path), conference->base.location, hold_dirs[hold_index]);
    for (held_index = 0; held_index < (int) (sizeof(held_files) / sizeof(held_files[0])); held_index++) {
      if (try_load_listing_path(hold_path,
                                held_files[held_index],
                                dirlist,
                                error_text,
                                error_text_size) == 0) {
        dirlist_set_source_path(dirlist, hold_path);
        dirlist_set_status(dirlist, "hold area");
        return 0;
      }
    }
  }

  dirlist_init(dirlist);
  dirlist_set_status(dirlist, "hold area not found");
  dirlist_set_error(error_text, error_text_size, "Hold/Held listing could not be opened");
  return -1;
}

static int load_trash_area_listing(const struct door_config *config,
                                   struct dirlist_data *dirlist,
                                   char *error_text,
                                   int error_text_size)
{
  static const char *prefixes[] = {
    "DIR1",
    "Dir1",
    "dir1"
  };
  char listing_path[256];
  int index;
  int listing_found;
  int stray_added;
  BPTR lock;
  struct FileInfoBlock *fib;

  if ((config == NULL) || (config->trash_path[0] == '\0')) {
    dirlist_init(dirlist);
    dirlist_set_status(dirlist, "trash listing not available");
    dirlist_set_error(error_text, error_text_size, "trash listing could not be opened");
    return -1;
  }

  listing_found = 0;
  for (index = 0; index < (int) (sizeof(prefixes) / sizeof(prefixes[0])); index++) {
    if (try_load_listing_path(config->trash_path,
                              prefixes[index],
                              dirlist,
                              error_text,
                              error_text_size) == 0) {
      join_path(listing_path, sizeof(listing_path), config->trash_path, prefixes[index]);
      strncpy(dirlist->listing_path, listing_path, sizeof(dirlist->listing_path) - 1U);
      dirlist->listing_path[sizeof(dirlist->listing_path) - 1U] = '\0';
      dirlist_set_source_path(dirlist, config->trash_path);
      dirlist_set_status(dirlist, "trash area");
      listing_found = 1;
      break;
    }
  }

  if (!listing_found) {
    dirlist_init(dirlist);
    join_path(listing_path, sizeof(listing_path), config->trash_path, "DIR1");
    strncpy(dirlist->listing_path, listing_path, sizeof(dirlist->listing_path) - 1U);
    dirlist->listing_path[sizeof(dirlist->listing_path) - 1U] = '\0';
  }

  dirlist_set_source_path(dirlist, config->trash_path);
  stray_added = 0;

  lock = Lock((STRPTR) config->trash_path, ACCESS_READ);
  if (lock != 0) {
    fib = AllocDosObject(DOS_FIB, NULL);
    if (fib != NULL) {
      if (Examine(lock, fib) != DOSFALSE) {
        while ((dirlist->entry_count < config->list_block_size) && (ExNext(lock, fib) != DOSFALSE)) {
          char header_text[256];
          char description_text[256];
          const char *filename;

          if (fib->fib_DirEntryType >= 0) {
            continue;
          }

          filename = (const char *) fib->fib_FileName;
          if (trash_name_is_internal(filename)) {
            continue;
          }

          if (dirlist_find_entry_by_filename(dirlist, filename) >= 0) {
            continue;
          }

          snprintf(header_text, sizeof(header_text), "%s  (description not found)", filename);
          snprintf(description_text, sizeof(description_text), "%s\n (description not found)", filename);
          if (dirlist_add_synthetic_entry(dirlist, filename, header_text, description_text) >= 0) {
            stray_added++;
          }
        }
      }
      FreeDosObject(DOS_FIB, fib);
    }
    UnLock(lock);
  }

  if (listing_found || (stray_added > 0)) {
    if (!listing_found && (stray_added > 0)) {
      dirlist_set_status(dirlist, "trash area");
    }
    if (error_text != NULL) {
      error_text[0] = '\0';
    }
    return 0;
  }

  dirlist_set_status(dirlist, "trash listing not found");
  dirlist_set_error(error_text, error_text_size, "trash listing could not be opened");
  return -1;
}

static int load_current_conference_info(const struct aedoor_context *door,
                                        const struct ae_system_config *system_config,
                                        struct ae_current_conference_info *current_conference,
                                        char *error_text,
                                        int error_text_size)
{
  const struct ae_conference_info *conference;
  const char *preferred_name;
  int display_conf_number;

  if ((door == NULL) || (system_config == NULL) || (current_conference == NULL)) {
    return -1;
  }

  memset(current_conference, 0, sizeof(*current_conference));
  display_conf_number = door->current_conf + 1;
  conference = ae_config_find_conference(system_config, display_conf_number);
  if (conference != NULL) {
    preferred_name = conference->name[0] != '\0' ? conference->name : door->conf_name;
    return ae_config_scan_load_current_conference(preferred_name,
                                                  conference->location,
                                                  conference->number,
                                                  current_conference,
                                                  error_text,
                                                  error_text_size);
  }

  return ae_config_scan_load_current_conference(door->conf_name,
                                                door->conf_location,
                                                display_conf_number,
                                                current_conference,
                                                error_text,
                                                error_text_size);
}

static int load_browsed_conference_info(const struct aedoor_context *door,
                                        struct ae_system_config *system_config,
                                        int active_conference_index,
                                        struct ae_current_conference_info *current_conference,
                                        char *error_text,
                                        int error_text_size)
{
  struct ae_conference_info *conference;
  const char *preferred_name;
  int status;

  if ((door == NULL) || (system_config == NULL) || (current_conference == NULL)) {
    return -1;
  }

  if ((active_conference_index < 0) || (active_conference_index >= system_config->discovered_count)) {
    return load_current_conference_info(door, system_config, current_conference, error_text, error_text_size);
  }

  conference = &system_config->conferences[active_conference_index];
  preferred_name = conference->name;
  if ((conference->number == (door->current_conf + 1)) && (preferred_name[0] == '\0')) {
    preferred_name = door->conf_name;
  }

  status = ae_config_scan_load_current_conference(preferred_name,
                                                  conference->location,
                                                  conference->number,
                                                  current_conference,
                                                  error_text,
                                                  error_text_size);
  if (status == 0) {
    if ((current_conference->base.name[0] == '\0') && (conference->name[0] != '\0')) {
      strncpy(current_conference->base.name,
              conference->name,
              sizeof(current_conference->base.name) - 1U);
      current_conference->base.name[sizeof(current_conference->base.name) - 1U] = '\0';
    }

    if ((conference->name[0] == '\0') && (current_conference->base.name[0] != '\0')) {
      strncpy(conference->name, current_conference->base.name, sizeof(conference->name) - 1U);
      conference->name[sizeof(conference->name) - 1U] = '\0';
    }
    conference->dir_count = current_conference->base.dir_count;
    conference->download_path_count = current_conference->base.download_path_count;
    conference->upload_path_count = current_conference->base.upload_path_count;
    conference->loaded = current_conference->base.loaded;
  }

  return status;
}

static int get_default_area_for_conference(const struct door_config *config,
                                           const struct ae_current_conference_info *conference)
{
  if (conference == NULL) {
    return 1;
  }

  if (conference->base.dir_count > 0) {
    return 1;
  }

  if (conference_has_hold_area(config, conference)) {
    return 1;
  }

  return 1;
}

static int get_default_destination_folder_for_area(const struct ae_current_conference_info *conference,
                                                   int active_area)
{
  if (conference == NULL) {
    return 1;
  }

  if ((active_area >= 1) && (active_area <= conference->base.dir_count)) {
    return conference_get_preferred_folder_for_area(conference, active_area);
  }

  return 1;
}

static int load_current_area_listing(const struct ae_current_conference_info *conference,
                                     const struct door_config *config,
                                     int active_area,
                                     long block_start_entry,
                                     struct dirlist_data *dirlist,
                                     char *error_text,
                                     int error_text_size)
{
  int max_area;
  int trash_area_index;

  if ((conference == NULL) || (dirlist == NULL)) {
    return -1;
  }

  max_area = conference_get_max_area(config, conference);
  trash_area_index = conference_get_trash_area_index(config, conference);
  if ((active_area < 1) || (active_area > max_area)) {
    dirlist_init(dirlist);
    if (error_text != NULL) {
      error_text[0] = '\0';
    }
    return -1;
  }

  if (active_area <= conference->base.dir_count) {
    char listing_path[256];
    static const char *prefixes[] = {
      "DIR",
      "Dir",
      "dir"
    };
    int index;

    for (index = 0; index < (int) (sizeof(prefixes) / sizeof(prefixes[0])); index++) {
      snprintf(listing_path, sizeof(listing_path), "%s%s%d",
               conference->base.location, prefixes[index], active_area);
      if (dirlist_load_window_file(listing_path,
                                   block_start_entry,
                                   config != NULL ? config->list_block_size : DIRLIST_DEFAULT_BLOCK_SIZE,
                                   dirlist,
                                   error_text,
                                   error_text_size) == 0) {
        dirlist_set_source_path(dirlist,
                                conference_get_download_folder_path(conference,
                                                                    conference_get_preferred_folder_for_area(conference, active_area)));
        return 0;
      }
    }

    dirlist_init(dirlist);
    dirlist_set_error(error_text, error_text_size, "DIR listing could not be opened");
    return -1;
  }

  if (conference_has_hold_area(config, conference) &&
      (active_area == conference->base.dir_count + 1)) {
    return load_hold_area_listing(conference, dirlist, error_text, error_text_size);
  }

  if ((trash_area_index > 0) && (active_area == trash_area_index)) {
    return load_trash_area_listing(config,
                                   dirlist,
                                   error_text,
                                   error_text_size);
  }

  dirlist_init(dirlist);
  dirlist_set_error(error_text, error_text_size, "unsupported area");
  return -1;
}

/* Tag and batch-move helpers */
static void reset_tag_scope_for_source(const struct ae_current_conference_info *conference,
                                       int active_area,
                                       const struct dirlist_data *dirlist)
{
  tagset_reset_scope(&g_tagset,
                     conference != NULL ? conference->base.number : 0,
                     active_area,
                     dirlist != NULL ? dirlist->listing_path : NULL);
}

static const char *resolve_source_folder_path(const struct door_config *config,
                                              const struct ae_current_conference_info *conference,
                                              const struct dirlist_data *dirlist,
                                              int active_area,
                                              int selected_entry,
                                              char *error_text,
                                              int error_text_size);

static int batch_move_tagged_set(const struct door_config *config,
                                 struct aedoor_context *door,
                                 const struct ae_current_conference_info *source_conference,
                                 const struct ae_current_conference_info *destination_conference,
                                 int active_area,
                                 const char *destination_folder,
                                 const char *destination_listing_path,
                                 int list_block_size,
                                 char *status_message,
                                 int status_message_size,
                                 char *error_text,
                                 int error_text_size)
{
  struct dirlist_data batch_dirlist;
  struct tagset_data pending_tags;
  long block_start_entry;
  long last_block_start_entry;
  int block_step;
  int moved_count;
  int failed_count;
  int batch_index;
  int batch_total;
  int tag_index;
  char first_error[160];

  if ((config == NULL) || (door == NULL) || (source_conference == NULL) ||
      (destination_folder == NULL) || (*destination_folder == '\0') ||
      (destination_listing_path == NULL) || (*destination_listing_path == '\0')) {
    strncpy(error_text, "invalid batch move request", (size_t) error_text_size - 1U);
    error_text[error_text_size - 1] = '\0';
    return -1;
  }

  memset(&batch_dirlist, 0, sizeof(batch_dirlist));
  dirlist_init(&batch_dirlist);
  tagset_init(&pending_tags);
  moved_count = 0;
  failed_count = 0;
  batch_index = 0;
  batch_total = tagset_count(&g_tagset);
  first_error[0] = '\0';
  block_step = list_block_size > 0 ? list_block_size : DIRLIST_DEFAULT_BLOCK_SIZE;

  if (batch_total <= 0) {
    strncpy(error_text, "No tagged files could be moved.", (size_t) error_text_size - 1U);
    error_text[error_text_size - 1] = '\0';
    return -1;
  }

  pending_tags.source_conf_number = g_tagset.source_conf_number;
  pending_tags.source_area = g_tagset.source_area;
  strncpy(pending_tags.listing_path, g_tagset.listing_path, sizeof(pending_tags.listing_path) - 1U);
  pending_tags.listing_path[sizeof(pending_tags.listing_path) - 1U] = '\0';
  for (tag_index = 0; tag_index < g_tagset.count; tag_index++) {
    if (tagset_add(&pending_tags, g_tagset.entries[tag_index].filename) < 0) {
      tagset_free(&pending_tags);
      dirlist_free(&batch_dirlist);
      strncpy(error_text, "tag list could not grow for batch move", (size_t) error_text_size - 1U);
      error_text[error_text_size - 1] = '\0';
      return -1;
    }
  }

  last_block_start_entry = 0L;
  block_start_entry = 0L;
  for (;;) {
    if (load_current_area_listing(source_conference,
                                  config,
                                  active_area,
                                  block_start_entry,
                                  &batch_dirlist,
                                  error_text,
                                  error_text_size) != 0) {
      tagset_free(&pending_tags);
      dirlist_free(&batch_dirlist);
      return -1;
    }
    last_block_start_entry = batch_dirlist.window_start_entry;
    if (!batch_dirlist.has_next_window) {
      break;
    }
    block_start_entry += block_step;
  }

  for (block_start_entry = last_block_start_entry; block_start_entry >= 0L; block_start_entry -= block_step) {
    if (load_current_area_listing(source_conference,
                                  config,
                                  active_area,
                                  block_start_entry,
                                  &batch_dirlist,
                                  error_text,
                                  error_text_size) != 0) {
      tagset_free(&pending_tags);
      dirlist_free(&batch_dirlist);
      return -1;
    }
    tagset_apply_to_dirlist(&g_tagset, &batch_dirlist);

    for (tag_index = batch_dirlist.entry_count - 1; tag_index >= 0; tag_index--) {
      const char *selected_filename;
      const char *source_folder;
      int move_status;

      if (!batch_dirlist.entries[tag_index].tagged) {
        continue;
      }
      if (!tagset_contains(&pending_tags, batch_dirlist.entries[tag_index].filename)) {
        continue;
      }

      batch_index++;
      selected_filename = batch_dirlist.entries[tag_index].filename[0] != '\0'
        ? batch_dirlist.entries[tag_index].filename
        : "(unknown)";
      source_folder = resolve_source_folder_path(config,
                                                 source_conference,
                                                 &batch_dirlist,
                                                 active_area,
                                                 tag_index,
                                                 error_text,
                                                 error_text_size);
      if (source_folder == NULL) {
        failed_count++;
        tagset_remove(&pending_tags, selected_filename);
        if (first_error[0] == '\0') {
          strncpy(first_error, error_text, sizeof(first_error) - 1U);
          first_error[sizeof(first_error) - 1U] = '\0';
        }
        continue;
      }

      ui_show_batch_move_progress(door,
                                  selected_filename,
                                  source_folder,
                                  destination_folder,
                                  batch_index,
                                  batch_total);
      move_status = file_ops_move_selected(&batch_dirlist,
                                           tag_index,
                                           source_folder,
                                           destination_folder,
                                           destination_listing_path,
                                           error_text,
                                           error_text_size);
      if (move_status != 0) {
        failed_count++;
        tagset_remove(&pending_tags, selected_filename);
        if (first_error[0] == '\0') {
          strncpy(first_error, error_text, sizeof(first_error) - 1U);
          first_error[sizeof(first_error) - 1U] = '\0';
        }
      } else {
        moved_count++;
        tagset_remove(&pending_tags, selected_filename);
        tagset_remove(&g_tagset, selected_filename);
      }
    }
  }

  tagset_free(&pending_tags);
  dirlist_free(&batch_dirlist);
  if ((moved_count > 0) && (failed_count == 0)) {
    snprintf(status_message, (size_t) status_message_size, "Batch move completed: %d files moved.", moved_count);
    return 0;
  }
  if (moved_count > 0) {
    snprintf(status_message,
             (size_t) status_message_size,
             "Batch move finished: %d moved, %d failed.",
             moved_count,
             failed_count);
    return 1;
  }

  if (first_error[0] != '\0') {
    strncpy(error_text, first_error, (size_t) error_text_size - 1U);
    error_text[error_text_size - 1] = '\0';
  } else {
    strncpy(error_text, "No tagged files could be moved.", (size_t) error_text_size - 1U);
    error_text[error_text_size - 1] = '\0';
  }
  return -1;
}

/* Source and destination folder mapping */
static const char *get_destination_folder_path(const struct door_config *config,
                                               const struct ae_current_conference_info *conference,
                                               const struct dirlist_data *dirlist,
                                               int active_area,
                                               int destination_folder_index)
{
  if (conference == NULL) {
    return NULL;
  }

  if ((active_area >= 1) && (active_area <= conference->base.dir_count)) {
    return conference_get_download_folder_path(conference, destination_folder_index);
  }

  if (conference_has_hold_area(config, conference) &&
      (active_area == conference->base.dir_count + 1) &&
      (dirlist != NULL) &&
      (dirlist->source_path[0] != '\0')) {
    return dirlist->source_path;
  }

  return NULL;
}

static const char *resolve_source_folder_path(const struct door_config *config,
                                              const struct ae_current_conference_info *conference,
                                              const struct dirlist_data *dirlist,
                                              int active_area,
                                              int selected_entry,
                                              char *error_text,
                                              int error_text_size)
{
  char candidate_path[256];
  static char matched_folder_path[256];
  const char *filename;
  const char *folder_path;
  int folder_index;
  int match_count;

  if ((conference == NULL) || (dirlist == NULL) ||
      (selected_entry < 0) || (selected_entry >= dirlist->entry_count)) {
    if ((error_text != NULL) && (error_text_size > 0)) {
      strncpy(error_text, "invalid source selection", (size_t) error_text_size - 1U);
      error_text[error_text_size - 1] = '\0';
    }
    return NULL;
  }

  filename = dirlist->entries[selected_entry].filename;
  if ((filename == NULL) || (*filename == '\0')) {
    if ((error_text != NULL) && (error_text_size > 0)) {
      strncpy(error_text, "selected entry has no filename", (size_t) error_text_size - 1U);
      error_text[error_text_size - 1] = '\0';
    }
    return NULL;
  }

  if (conference_has_hold_area(config, conference) &&
      (active_area == conference->base.dir_count + 1) &&
      (dirlist->source_path[0] != '\0')) {
    return dirlist->source_path;
  }

  if ((conference_get_trash_area_index(config, conference) > 0) &&
      (active_area == conference_get_trash_area_index(config, conference)) &&
      (config != NULL) &&
      (config->trash_path[0] != '\0')) {
    return config->trash_path;
  }

  if ((active_area >= 1) &&
      (active_area <= conference->base.dir_count) &&
      (dirlist->source_path[0] != '\0')) {
    join_path(candidate_path, sizeof(candidate_path), dirlist->source_path, filename);
    if (file_exists_at_path(candidate_path)) {
      return dirlist->source_path;
    }

    matched_folder_path[0] = '\0';
    match_count = 0;
    search_source_file_recursive(dirlist->source_path,
                                 filename,
                                 matched_folder_path,
                                 sizeof(matched_folder_path),
                                 &match_count,
                                 0);
    if (match_count == 1) {
      return matched_folder_path;
    }
    if (match_count > 1) {
      if ((error_text != NULL) && (error_text_size > 0)) {
        strncpy(error_text, "source file exists multiple times in this area's storage path",
                (size_t) error_text_size - 1U);
        error_text[error_text_size - 1] = '\0';
      }
      return NULL;
    }
  }

  matched_folder_path[0] = '\0';
  match_count = 0;
  for (folder_index = 1; folder_index <= conference_get_download_folder_count(conference); folder_index++) {
    folder_path = conference_get_download_folder_path(conference, folder_index);
    if ((folder_path == NULL) || (*folder_path == '\0')) {
      continue;
    }

    join_path(candidate_path, sizeof(candidate_path), folder_path, filename);
    if (file_exists_at_path(candidate_path)) {
      record_source_match(folder_path,
                          matched_folder_path,
                          sizeof(matched_folder_path),
                          &match_count);
      continue;
    }

    search_source_file_recursive(folder_path,
                                 filename,
                                 matched_folder_path,
                                 sizeof(matched_folder_path),
                                 &match_count,
                                 0);
  }

  if (match_count == 1) {
    return matched_folder_path;
  }

  if ((error_text != NULL) && (error_text_size > 0)) {
    if (match_count <= 0) {
      strncpy(error_text, "source file was not found in this conference's download folders",
              (size_t) error_text_size - 1U);
    } else {
      strncpy(error_text, "source file exists in multiple download folders; move is ambiguous",
              (size_t) error_text_size - 1U);
    }
    error_text[error_text_size - 1] = '\0';
  }

  return NULL;
}

/* Door entry point */

int main(int argc, char **argv)
{
  const char *config_path;
  const char *requested_config_path;
  struct door_config config;
  struct aedoor_context door;
  struct doorlog log;
  int active_conference_index;
  int active_area;
  long source_block_start_entry;
  int selected_entry;
  int destination_conference_index;
  int destination_area;
  long destination_block_start_entry;
  int destination_folder_index;
  int trash_reference_area;
  int delete_mode;
  int ui_mode;
  int conference_status;
  int destination_status;
  int ui_status;
  char error_text[160];
  char status_message[160];
  int status;
  int previous_ui_mode;
  int previous_destination_area;

  requested_config_path = NULL;
  if ((argc > 2) && (argv[2] != NULL) && (argv[2][0] != '\0')) {
    requested_config_path = argv[2];
  } else if ((argc > 1) && path_looks_like_config(argv[1])) {
    requested_config_path = argv[1];
  }

  memset(&door, 0, sizeof(door));
  memset(&g_system_config, 0, sizeof(g_system_config));
  memset(&g_current_conference, 0, sizeof(g_current_conference));
  memset(&g_dirlist, 0, sizeof(g_dirlist));
  memset(&g_destination_conference, 0, sizeof(g_destination_conference));
  memset(&g_destination_dirlist, 0, sizeof(g_destination_dirlist));
  tagset_init(&g_tagset);
  memset(&log, 0, sizeof(log));
  error_text[0] = '\0';
  status_message[0] = '\0';

  config_path = requested_config_path != NULL ? requested_config_path : "PROGDIR:arbfiles.cfg";
  status = load_config_with_fallbacks(&config_path, requested_config_path, &config, error_text, (int) sizeof(error_text));

  if (doorlog_open(&log, config.debug_log, config.debug_enabled) != 0) {
    printf("Log warning: could not open %s\n", config.debug_log);
  } else if (config.debug_enabled) {
    doorlog_writef(&log, "Door version %s", ARBFILES_VERSION);
    doorlog_writef(&log, "Door start with config %s", config_path);
    if (status != 0) {
      doorlog_writef(&log, "Config load warning: %s", error_text);
      doorlog_write(&log, "Continuing with default configuration values.");
    }
  }

  if (status != 0) {
    printf("Config warning: %s (%s)\n", error_text, config_path);
  }

  status = aedoor_open(&door, argc, argv, error_text, (int) sizeof(error_text));
  if (status != 0) {
    doorlog_writef(&log, "AEDoor open failed: %s", error_text);
    printf("AEDoor open failed: %s\n", error_text);
    doorlog_close(&log);
    return 10;
  }

  doorlog_write(&log, "AEDoor opened.");

  status = aedoor_fetch_context(&door, error_text, (int) sizeof(error_text));
  if (status != 0) {
    doorlog_writef(&log, "AEDoor context fetch failed: %s", error_text);
    aedoor_write_line(&door, "Door start failed while fetching user context.");
    aedoor_write_line(&door, error_text);
    aedoor_close(&door);
    doorlog_close(&log);
    return 20;
  }
  doorlog_writef(&log, "User %s in conference %d", door.username, door.current_conf);

  status = aedoor_prepare_session(&door, &config, error_text, (int) sizeof(error_text));
  if (status != 0) {
    doorlog_writef(&log, "AEDoor session preparation failed: %s", error_text);
    aedoor_write_line(&door, "Door start failed while preparing the session.");
    aedoor_write_line(&door, error_text);
    aedoor_close(&door);
    doorlog_close(&log);
    return 30;
  }
  doorlog_write(&log, "AEDoor session prepared.");

  status = ae_config_scan_load(&config, &g_system_config, error_text, (int) sizeof(error_text));
  if (status != 0) {
    doorlog_writef(&log, "AmiExpress config discovery warning: %s", error_text);
  } else {
    doorlog_writef(&log, "AmiExpress discovery status: %s", g_system_config.status_text);
  }

  active_conference_index = ae_config_find_conference_index(&g_system_config, door.current_conf + 1);
  conference_status = load_current_conference_info(&door,
                                                   &g_system_config,
                                                   &g_current_conference,
                                                   error_text,
                                                   (int) sizeof(error_text));
  if (conference_status == 0) {
    doorlog_writef(&log,
                   "Current conference %d resolved as %s with %d dirs",
                   g_current_conference.base.number,
                   g_current_conference.base.location,
                   g_current_conference.base.dir_count);
    if ((active_conference_index >= 0) && (active_conference_index < g_system_config.discovered_count)) {
      if ((g_system_config.conferences[active_conference_index].name[0] == '\0') &&
          (g_current_conference.base.name[0] != '\0')) {
        strncpy(g_system_config.conferences[active_conference_index].name,
                g_current_conference.base.name,
                sizeof(g_system_config.conferences[active_conference_index].name) - 1U);
        g_system_config.conferences[active_conference_index].name[sizeof(g_system_config.conferences[active_conference_index].name) - 1U] = '\0';
      }
      g_system_config.conferences[active_conference_index].dir_count = g_current_conference.base.dir_count;
      g_system_config.conferences[active_conference_index].download_path_count = g_current_conference.base.download_path_count;
      g_system_config.conferences[active_conference_index].upload_path_count = g_current_conference.base.upload_path_count;
      g_system_config.conferences[active_conference_index].loaded = g_current_conference.base.loaded;
    }
  } else {
    doorlog_writef(&log, "Current conference fallback failed: %s", error_text);
  }

  active_area = 1;
  source_block_start_entry = 0L;
  selected_entry = 0;
  destination_conference_index = active_conference_index;
  destination_area = 1;
  destination_block_start_entry = 0L;
  destination_folder_index = 1;
  trash_reference_area = 1;
  delete_mode = UI_DELETE_NONE;
  ui_mode = UI_MODE_SOURCE;
  if (conference_status == 0) {
    active_area = get_default_area_for_conference(&config, &g_current_conference);
    reset_tag_scope_for_source(&g_current_conference, active_area, &g_dirlist);
  }
  destination_status = load_browsed_conference_info(&door,
                                                    &g_system_config,
                                                    destination_conference_index,
                                                    &g_destination_conference,
                                                    error_text,
                                                    (int) sizeof(error_text));
  if (destination_status == 0) {
    destination_area = get_default_area_for_conference(&config, &g_destination_conference);
    destination_folder_index = get_default_destination_folder_for_area(&g_destination_conference, destination_area);
  }

  ui_status = 0;
  for (;;) {
    previous_ui_mode = ui_mode;
    previous_destination_area = destination_area;

    load_current_area_listing(conference_status == 0 ? &g_current_conference : NULL,
                              &config,
                              active_area,
                              source_block_start_entry,
                              &g_dirlist,
                              error_text,
                              (int) sizeof(error_text));
    tagset_apply_to_dirlist(&g_tagset, &g_dirlist);
    load_current_area_listing(destination_status == 0 ? &g_destination_conference : NULL,
                              &config,
                              destination_area,
                              destination_block_start_entry,
                              &g_destination_dirlist,
                              error_text,
                              (int) sizeof(error_text));
    ui_status = ui_run(&config,
                       &door,
                       &g_system_config,
                       conference_status == 0 ? &g_current_conference : NULL,
                       &g_dirlist,
                       &g_tagset,
                       destination_status == 0 ? &g_destination_conference : NULL,
                       &g_destination_dirlist,
                       status_message,
                       tagset_count(&g_tagset),
                       &ui_mode,
                       &active_conference_index,
                       &active_area,
                       &source_block_start_entry,
                       &selected_entry,
                       &destination_conference_index,
                       &destination_area,
                       &destination_block_start_entry,
                       &destination_folder_index,
                       &trash_reference_area,
                       &delete_mode,
                       config.list_block_size,
                       &log,
                       error_text,
                       (int) sizeof(error_text));
    if (ui_status == UI_RESULT_CONFERENCE_CHANGE) {
      if (ui_mode == UI_MODE_DESTINATION) {
        destination_status = load_browsed_conference_info(&door,
                                                          &g_system_config,
                                                          destination_conference_index,
                                                          &g_destination_conference,
                                                          error_text,
                                                          (int) sizeof(error_text));
        if (destination_status == 0) {
          destination_area = get_default_area_for_conference(&config, &g_destination_conference);
          destination_block_start_entry = 0L;
          destination_folder_index = get_default_destination_folder_for_area(&g_destination_conference, destination_area);
        } else {
          destination_area = 1;
          destination_block_start_entry = 0L;
          destination_folder_index = 1;
        }
        status_message[0] = '\0';
      } else {
        if (tagset_count(&g_tagset) > 0) {
          tagset_reset_scope(&g_tagset, 0, 0, NULL);
          dirlist_clear_tags(&g_dirlist);
          strncpy(status_message, "Tags cleared after source conference change.", sizeof(status_message) - 1U);
          status_message[sizeof(status_message) - 1U] = '\0';
        }
        conference_status = load_browsed_conference_info(&door,
                                                         &g_system_config,
                                                         active_conference_index,
                                                         &g_current_conference,
                                                         error_text,
                                                         (int) sizeof(error_text));
        if (conference_status == 0) {
          active_area = get_default_area_for_conference(&config, &g_current_conference);
          reset_tag_scope_for_source(&g_current_conference, active_area, &g_dirlist);
        } else {
          active_area = 1;
          tagset_reset_scope(&g_tagset, 0, 0, NULL);
        }
        source_block_start_entry = 0L;
        trash_reference_area = active_area;
        selected_entry = 0;
        if (status_message[0] == '\0') {
          status_message[0] = '\0';
        }
      }
      continue;
    }
    if (ui_status == UI_RESULT_BLOCK_CHANGE) {
      if (ui_mode == UI_MODE_SOURCE) {
        status_message[0] = '\0';
        selected_entry = 0;
      } else {
        status_message[0] = '\0';
      }
      continue;
    }
    if (ui_status == UI_RESULT_MOVE) {
      const char *source_folder;
      const char *destination_folder;
      const char *selected_filename;
      int move_status;
      int tagged_count;

      tagged_count = tagset_count(&g_tagset);
      selected_filename = (selected_entry >= 0) && (selected_entry < g_dirlist.entry_count)
        ? g_dirlist.entries[selected_entry].filename
        : "(unknown)";
      destination_folder = get_destination_folder_path(&config,
                                                       &g_destination_conference,
                                                       &g_destination_dirlist,
                                                       destination_area,
                                                       destination_folder_index);
      if ((destination_folder == NULL) || (*destination_folder == '\0')) {
        strncpy(error_text, "destination storage folder is unknown", sizeof(error_text) - 1U);
        error_text[sizeof(error_text) - 1U] = '\0';
        move_status = -1;
      } else if (tagged_count > 0) {
        move_status = batch_move_tagged_set(&config,
                                            &door,
                                            &g_current_conference,
                                            &g_destination_conference,
                                            active_area,
                                            destination_folder,
                                            g_destination_dirlist.listing_path,
                                            config.list_block_size,
                                            status_message,
                                            (int) sizeof(status_message),
                                            error_text,
                                            (int) sizeof(error_text));
      } else {
        source_folder = resolve_source_folder_path(&config,
                                                   &g_current_conference,
                                                   &g_dirlist,
                                                   active_area,
                                                   selected_entry,
                                                   error_text,
                                                   (int) sizeof(error_text));
        if (source_folder == NULL) {
          move_status = -1;
        } else {
          ui_show_move_progress(&door, selected_filename, source_folder, destination_folder);
          move_status = file_ops_move_selected(&g_dirlist,
                                               selected_entry,
                                               source_folder,
                                               destination_folder,
                                               g_destination_dirlist.listing_path,
                                               error_text,
                                               (int) sizeof(error_text));
        }
      }
      if (move_status != 0) {
        if (status_message[0] == '\0') {
          strncpy(status_message, error_text, sizeof(status_message) - 1U);
          status_message[sizeof(status_message) - 1U] = '\0';
        }
        if (move_status > 0) {
          doorlog_writef(&log, "Move partly complete: %s", status_message);
        } else {
          doorlog_writef(&log, "Move failed: %s", status_message[0] != '\0' ? status_message : error_text);
        }
        ui_show_move_result(&door, move_status > 0 ? 2 : 0, status_message[0] != '\0' ? status_message : error_text);
      } else {
        doorlog_write(&log, "Move completed successfully.");
        if (status_message[0] == '\0') {
          strncpy(status_message, "Move completed successfully.", sizeof(status_message) - 1U);
          status_message[sizeof(status_message) - 1U] = '\0';
        }
        ui_show_move_result(&door, 1, status_message);
        ui_mode = UI_MODE_SOURCE;
        source_block_start_entry = 0L;
        selected_entry = 0;
        tagset_apply_to_dirlist(&g_tagset, &g_dirlist);
      }
      continue;
    }
    if (ui_status == UI_RESULT_DELETE) {
      const char *source_folder;
      const char *selected_filename;
      const char *target_store;
      int orphan_confirm;
      int delete_status;

      selected_filename = (selected_entry >= 0) && (selected_entry < g_dirlist.entry_count)
        ? g_dirlist.entries[selected_entry].filename
        : "(unknown)";
      source_folder = resolve_source_folder_path(&config,
                                                 &g_current_conference,
                                                 &g_dirlist,
                                                 active_area,
                                                 selected_entry,
                                                 error_text,
                                                 (int) sizeof(error_text));
      target_store = delete_mode == UI_DELETE_TRASH ? config.trash_path : NULL;
      if (source_folder == NULL) {
        if (strstr(error_text, "was not found") != NULL) {
          orphan_confirm = ui_confirm_orphan_delete(&door, selected_filename, error_text);
          if (orphan_confirm < 0) {
            strncpy(error_text, "orphan confirmation input failed", sizeof(error_text) - 1U);
            error_text[sizeof(error_text) - 1U] = '\0';
            delete_status = -1;
          } else if (orphan_confirm == 0) {
            strncpy(status_message, "Delete cancelled.", sizeof(status_message) - 1U);
            status_message[sizeof(status_message) - 1U] = '\0';
            delete_mode = UI_DELETE_NONE;
            continue;
          } else {
            ui_show_delete_progress(&door, UI_DELETE_PERMANENT, selected_filename, "(listing only)", NULL);
            delete_status = file_ops_delete_orphan_entry(&g_dirlist,
                                                         selected_entry,
                                                         error_text,
                                                         (int) sizeof(error_text));
            if (delete_status == 0) {
              tagset_remove(&g_tagset, selected_filename);
              strncpy(status_message, "Orphan listing entry removed successfully.", sizeof(status_message) - 1U);
              status_message[sizeof(status_message) - 1U] = '\0';
              ui_show_delete_result(&door, 1, status_message);
              source_block_start_entry = 0L;
              if (selected_entry >= g_dirlist.entry_count - 1) {
                if (selected_entry > 0) {
                  selected_entry--;
                } else {
                  selected_entry = 0;
                }
              }
              delete_mode = UI_DELETE_NONE;
              continue;
            }
          }
        } else {
          delete_status = -1;
        }
      } else if ((delete_mode == UI_DELETE_TRASH) && (config.trash_path[0] == '\0')) {
        strncpy(error_text, "trash folder is not configured", sizeof(error_text) - 1U);
        error_text[sizeof(error_text) - 1U] = '\0';
        delete_status = -1;
      } else {
        ui_show_delete_progress(&door, delete_mode, selected_filename, source_folder, target_store);
        delete_status = file_ops_delete_selected(&g_dirlist,
                                                 selected_entry,
                                                 source_folder,
                                                 config.trash_path,
                                                 delete_mode == UI_DELETE_TRASH,
                                                 error_text,
                                                 (int) sizeof(error_text));
      }
      if (delete_status != 0) {
        doorlog_writef(&log, "Delete failed: %s", error_text);
        if (delete_status < 0) {
          strncpy(status_message, error_text, sizeof(status_message) - 1U);
          status_message[sizeof(status_message) - 1U] = '\0';
          ui_show_delete_result(&door, 0, error_text);
        }
      } else {
        tagset_remove(&g_tagset, selected_filename);
        doorlog_writef(&log,
                       delete_mode == UI_DELETE_TRASH ? "Move to trash completed successfully."
                                                      : "Delete completed successfully.");
        strncpy(status_message,
                delete_mode == UI_DELETE_TRASH ? "Moved to trash successfully."
                                               : "Delete completed successfully.",
                sizeof(status_message) - 1U);
        status_message[sizeof(status_message) - 1U] = '\0';
        ui_show_delete_result(&door, 1, status_message);
        source_block_start_entry = 0L;
        if (selected_entry >= g_dirlist.entry_count - 1) {
          if (selected_entry > 0) {
            selected_entry--;
          } else {
            selected_entry = 0;
          }
        }
      }
      delete_mode = UI_DELETE_NONE;
      continue;
    }
    if (ui_status != UI_RESULT_AREA_CHANGE) {
      break;
    }
    if (ui_mode == UI_MODE_SOURCE) {
      if (tagset_count(&g_tagset) > 0) {
        tagset_reset_scope(&g_tagset, 0, 0, NULL);
        dirlist_clear_tags(&g_dirlist);
        strncpy(status_message, "Tags cleared after source area change.", sizeof(status_message) - 1U);
        status_message[sizeof(status_message) - 1U] = '\0';
      }
      if ((conference_status == 0) &&
          (active_area >= 1) &&
          (active_area <= g_current_conference.base.dir_count)) {
        trash_reference_area = active_area;
        reset_tag_scope_for_source(&g_current_conference, active_area, &g_dirlist);
      }
      source_block_start_entry = 0L;
      selected_entry = 0;
    } else if ((destination_area < 1) || (destination_area > g_destination_conference.base.dir_count)) {
      destination_block_start_entry = 0L;
      destination_folder_index = 1;
    } else if ((previous_ui_mode != ui_mode) || (previous_destination_area != destination_area)) {
      destination_block_start_entry = 0L;
      destination_folder_index = get_default_destination_folder_for_area(&g_destination_conference, destination_area);
    } else if (destination_folder_index > g_destination_conference.base.download_path_count) {
      destination_folder_index = get_default_destination_folder_for_area(&g_destination_conference, destination_area);
    }
    status_message[0] = '\0';
  }

  if (ui_status != 0) {
    doorlog_writef(&log, "UI failed: %s", error_text);
    aedoor_write_line(&door, "Door session ended with an error.");
    aedoor_write_line(&door, error_text);
  }

  doorlog_write(&log, "Restoring AEDoor state and closing.");
  dirlist_free(&g_dirlist);
  dirlist_free(&g_destination_dirlist);
  tagset_free(&g_tagset);
  aedoor_restore_session(&door);
  aedoor_close(&door);
  doorlog_close(&log);

  return ui_status == UI_RESULT_EXIT ? 0 : 40;
}
