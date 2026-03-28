/*
 * arbfiles main control flow.
 *
 * This module owns the top-level door lifecycle, conference loading, browse
 * state, and move orchestration.
 */
#include "aedoor_bridge.h"
#include "ae_config_scan.h"
#include "dirlist.h"
#include "door_config.h"
#include "door_version.h"
#include "doorlog.h"
#include "file_ops.h"
#include "ui.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * Keep the larger working sets out of the 68k stack frame. vbcc quickly runs
 * out of displacement range once DIR listings and conference discovery buffers
 * are both local to main().
 */
static struct ae_system_config g_system_config;
static struct ae_current_conference_info g_current_conference;
static struct dirlist_data g_dirlist;
static struct ae_current_conference_info g_destination_conference;
static struct dirlist_data g_destination_dirlist;

/* Conference and storage helpers */

static int conference_has_hold_area(const struct door_config *config,
                                    const struct ae_current_conference_info *conference)
{
  return (config != NULL) &&
         (config->allow_hold_area != 0) &&
         (conference != NULL) &&
         (conference->base.location[0] != '\0');
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

/* Configuration path handling */

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

/* Generic path helpers */
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

/* Conference metadata and listing loading */

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

static int load_current_conference_info(const struct aedoor_context *door,
                                        const struct ae_system_config *system_config,
                                        struct ae_current_conference_info *current_conference,
                                        char *error_text,
                                        int error_text_size)
{
  const struct ae_conference_info *conference;
  int display_conf_number;

  if ((door == NULL) || (system_config == NULL) || (current_conference == NULL)) {
    return -1;
  }

  memset(current_conference, 0, sizeof(*current_conference));
  display_conf_number = door->current_conf + 1;
  conference = ae_config_find_conference(system_config, display_conf_number);
  if (conference != NULL) {
    return ae_config_scan_load_current_conference(door->conf_name[0] != '\0' ? door->conf_name : conference->name,
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
  if (conference->number == (door->current_conf + 1)) {
    preferred_name = door->conf_name;
  }

  status = ae_config_scan_load_current_conference(preferred_name,
                                                  conference->location,
                                                  conference->number,
                                                  current_conference,
                                                  error_text,
                                                  error_text_size);
  if (status == 0) {
    if (current_conference->base.name[0] != '\0') {
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

  if ((active_area >= 1) &&
      (active_area <= conference->base.dir_count) &&
      (active_area <= conference->base.download_path_count) &&
      (conference->download_paths[active_area - 1][0] != '\0')) {
    return active_area;
  }

  if (conference->base.download_path_count > 0) {
    return 1;
  }

  return 1;
}

static int load_current_area_listing(const struct ae_current_conference_info *conference,
                                     const struct door_config *config,
                                     int active_area,
                                     struct dirlist_data *dirlist,
                                     char *error_text,
                                     int error_text_size)
{
  int max_area;

  if ((conference == NULL) || (dirlist == NULL)) {
    return -1;
  }

  max_area = conference_get_max_area(config, conference);
  if ((active_area < 1) || (active_area > max_area)) {
    dirlist_init(dirlist);
    if (error_text != NULL) {
      error_text[0] = '\0';
    }
    return -1;
  }

  if (active_area <= conference->base.dir_count) {
    return load_numbered_area_listing(conference,
                                      active_area,
                                      dirlist,
                                      error_text,
                                      error_text_size);
  }

  if (conference_has_hold_area(config, conference) &&
      (active_area == conference->base.dir_count + 1)) {
    return load_hold_area_listing(conference, dirlist, error_text, error_text_size);
  }

  dirlist_init(dirlist);
  dirlist_set_error(error_text, error_text_size, "unsupported area");
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
  const char *filename;
  const char *matched_folder;
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

  matched_folder = NULL;
  match_count = 0;
  for (folder_index = 1; folder_index <= conference_get_download_folder_count(conference); folder_index++) {
    folder_path = conference_get_download_folder_path(conference, folder_index);
    if ((folder_path == NULL) || (*folder_path == '\0')) {
      continue;
    }

    join_path(candidate_path, sizeof(candidate_path), folder_path, filename);
    if (file_exists_at_path(candidate_path)) {
      matched_folder = folder_path;
      match_count++;
    }
  }

  if (match_count == 1) {
    return matched_folder;
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
  int selected_entry;
  int destination_conference_index;
  int destination_area;
  int destination_folder_index;
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
    aedoor_write_line(&door, "Door start failed while fetching caller context.");
    aedoor_write_line(&door, error_text);
    aedoor_close(&door);
    doorlog_close(&log);
    return 20;
  }
  doorlog_writef(&log, "Caller %s in conference %d", door.username, door.current_conf);

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
      strncpy(g_system_config.conferences[active_conference_index].name,
              g_current_conference.base.name,
              sizeof(g_system_config.conferences[active_conference_index].name) - 1U);
      g_system_config.conferences[active_conference_index].name[sizeof(g_system_config.conferences[active_conference_index].name) - 1U] = '\0';
      g_system_config.conferences[active_conference_index].dir_count = g_current_conference.base.dir_count;
      g_system_config.conferences[active_conference_index].download_path_count = g_current_conference.base.download_path_count;
      g_system_config.conferences[active_conference_index].upload_path_count = g_current_conference.base.upload_path_count;
      g_system_config.conferences[active_conference_index].loaded = g_current_conference.base.loaded;
    }
  } else {
    doorlog_writef(&log, "Current conference fallback failed: %s", error_text);
  }

  active_area = 1;
  selected_entry = 0;
  destination_conference_index = active_conference_index;
  destination_area = 1;
  destination_folder_index = 1;
  ui_mode = UI_MODE_SOURCE;
  if (conference_status == 0) {
    active_area = get_default_area_for_conference(&config, &g_current_conference);
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
                              &g_dirlist,
                              error_text,
                              (int) sizeof(error_text));
    load_current_area_listing(destination_status == 0 ? &g_destination_conference : NULL,
                              &config,
                              destination_area,
                              &g_destination_dirlist,
                              error_text,
                              (int) sizeof(error_text));
    ui_status = ui_run(&config,
                       &door,
                       &g_system_config,
                       conference_status == 0 ? &g_current_conference : NULL,
                       &g_dirlist,
                       destination_status == 0 ? &g_destination_conference : NULL,
                       &g_destination_dirlist,
                       status_message,
                       &ui_mode,
                       &active_conference_index,
                       &active_area,
                       &selected_entry,
                       &destination_conference_index,
                       &destination_area,
                       &destination_folder_index,
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
          destination_folder_index = get_default_destination_folder_for_area(&g_destination_conference, destination_area);
        } else {
          destination_area = 1;
          destination_folder_index = 1;
        }
        status_message[0] = '\0';
      } else {
        conference_status = load_browsed_conference_info(&door,
                                                         &g_system_config,
                                                         active_conference_index,
                                                         &g_current_conference,
                                                         error_text,
                                                         (int) sizeof(error_text));
        if (conference_status == 0) {
          active_area = get_default_area_for_conference(&config, &g_current_conference);
        } else {
          active_area = 1;
        }
        selected_entry = 0;
        status_message[0] = '\0';
      }
      continue;
    }
    if (ui_status == UI_RESULT_MOVE) {
      const char *source_folder;
      const char *destination_folder;
      const char *selected_filename;
      int move_status;

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
      destination_folder = get_destination_folder_path(&config,
                                                       &g_destination_conference,
                                                       &g_destination_dirlist,
                                                       destination_area,
                                                       destination_folder_index);
      if (source_folder == NULL) {
        move_status = -1;
      } else if ((destination_folder == NULL) || (*destination_folder == '\0')) {
        strncpy(error_text, "destination storage folder is unknown", sizeof(error_text) - 1U);
        error_text[sizeof(error_text) - 1U] = '\0';
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
      if (move_status != 0) {
        doorlog_writef(&log, "Move failed: %s", error_text);
        strncpy(status_message, error_text, sizeof(status_message) - 1U);
        status_message[sizeof(status_message) - 1U] = '\0';
        ui_show_move_result(&door, 0, error_text);
      } else {
        doorlog_write(&log, "Move completed successfully.");
        strncpy(status_message, "Move completed successfully.", sizeof(status_message) - 1U);
        status_message[sizeof(status_message) - 1U] = '\0';
        ui_show_move_result(&door, 1, status_message);
        ui_mode = UI_MODE_SOURCE;
        selected_entry = 0;
      }
      continue;
    }
    if (ui_status != UI_RESULT_AREA_CHANGE) {
      break;
    }
    if (ui_mode == UI_MODE_SOURCE) {
      selected_entry = 0;
    } else if ((destination_area < 1) || (destination_area > g_destination_conference.base.dir_count)) {
      destination_folder_index = 1;
    } else if ((previous_ui_mode != ui_mode) || (previous_destination_area != destination_area)) {
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
  aedoor_restore_session(&door);
  aedoor_close(&door);
  doorlog_close(&log);

  return ui_status == UI_RESULT_EXIT ? 0 : 40;
}
