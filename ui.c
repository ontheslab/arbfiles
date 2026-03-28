/*
 * Interactive browse UI.
 */
#include "ui.h"
#include "aedoor_bridge.h"
#include "ae_config_scan.h"
#include "dirlist.h"
#include "door_config.h"
#include "door_version.h"
#include "doorlog.h"

#include <proto/dos.h>

#include <stdio.h>
#include <string.h>

#define UI_VISIBLE_ENTRIES 9
#define UI_PREVIEW_WIDTH 74
#define UI_PREVIEW_LINES 2
#define UI_MODAL_BODY_LINES 16

/* Low-level UI helpers */
static void ui_set_error(char *error_text, int error_text_size, const char *message)
{
  if ((error_text == NULL) || (error_text_size <= 0) || (message == NULL)) {
    return;
  }

  strncpy(error_text, message, (size_t) error_text_size - 1U);
  error_text[error_text_size - 1] = '\0';
}

static int ui_wait_for_key(struct aedoor_context *door, long *key_value)
{
  int poll_status;

  if ((door == NULL) || (key_value == NULL)) {
    return -1;
  }

  for (;;) {
    poll_status = aedoor_poll_key(door, key_value);
    if (poll_status < 0) {
      return -1;
    }
    if (poll_status > 0) {
      return 0;
    }
    Delay(2);
  }
}

static int ui_conference_has_hold_area(const struct door_config *config,
                                       const struct ae_current_conference_info *conference)
{
  return (config != NULL) &&
         (config->allow_hold_area != 0) &&
         (conference != NULL) &&
         (conference->base.location[0] != '\0');
}

static int ui_get_normal_area_count(const struct ae_current_conference_info *conference)
{
  if (conference == NULL) {
    return 0;
  }

  return conference->base.dir_count;
}

static int ui_get_hold_area_index(const struct door_config *config,
                                  const struct ae_current_conference_info *conference)
{
  if (!ui_conference_has_hold_area(config, conference) || (conference == NULL)) {
    return 0;
  }

  return conference->base.dir_count + 1;
}

static int ui_has_direct_area_folder_map(const struct ae_current_conference_info *conference)
{
  return (conference != NULL) &&
         (conference->base.dir_count > 0) &&
         (conference->base.dir_count == conference->base.download_path_count);
}

static const char *ui_get_area_label(const struct door_config *config,
                                     const struct ae_current_conference_info *conference,
                                     int active_area)
{
  static char label[32];

  if ((conference != NULL) && (active_area >= 1) && (active_area <= conference->base.dir_count)) {
    snprintf(label, sizeof(label), "DIR%d", active_area);
    return label;
  }

  if (ui_conference_has_hold_area(config, conference) &&
      (conference != NULL) &&
      (active_area == conference->base.dir_count + 1)) {
    return "Hold/Held";
  }

  return "(unknown)";
}

static void ui_clear_screen(struct aedoor_context *door)
{
  if (door != NULL) {
    aedoor_clear_screen(door);
  }
}

static int ui_get_selected_index(const struct dirlist_data *dirlist, int selected_entry)
{
  if ((dirlist == NULL) || (dirlist->entry_count <= 0)) {
    return 0;
  }

  if (selected_entry < 0) {
    return 0;
  }
  if (selected_entry >= dirlist->entry_count) {
    return dirlist->entry_count - 1;
  }
  return selected_entry;
}

static int ui_get_visible_start(const struct dirlist_data *dirlist, int selected_index)
{
  int max_start;
  int start;

  if ((dirlist == NULL) || (dirlist->entry_count <= UI_VISIBLE_ENTRIES)) {
    return 0;
  }

  start = selected_index - (UI_VISIBLE_ENTRIES / 2);
  if (start < 0) {
    start = 0;
  }

  max_start = dirlist->entry_count - UI_VISIBLE_ENTRIES;
  if (start > max_start) {
    start = max_start;
  }
  return start;
}

static const char *ui_get_preview_text(const struct dirlist_data *dirlist, int selected_index)
{
  if ((dirlist == NULL) || (dirlist->entry_count <= 0)) {
    return "(no entries loaded)";
  }

  if ((selected_index < 0) || (selected_index >= dirlist->entry_count)) {
    return "(selection unavailable)";
  }

  if (dirlist->entries[selected_index].description[0] != '\0') {
    return dirlist->entries[selected_index].description;
  }

  if (dirlist->entries[selected_index].header_line[0] != '\0') {
    return dirlist->entries[selected_index].header_line;
  }

  return "(no preview text)";
}

static void ui_write_preview_lines(struct aedoor_context *door, const char *text)
{
  int line_count;
  const char *cursor;
  char chunk[UI_PREVIEW_WIDTH + 1];
  char output[UI_PREVIEW_WIDTH + 3];
  int text_length;
  int split_at;
  int index;

  if (door == NULL) {
    return;
  }

  cursor = text != NULL ? text : "";
  for (line_count = 0; line_count < UI_PREVIEW_LINES; line_count++) {
    while (*cursor == ' ') {
      cursor++;
    }

    if (*cursor == '\0') {
      if (line_count == 0) {
        aedoor_write_line(door, "  (no preview text)");
      }
      return;
    }

    text_length = (int) strlen(cursor);
    if (text_length <= UI_PREVIEW_WIDTH) {
      snprintf(output, sizeof(output), "  %s", cursor);
      aedoor_write_line(door, output);
      return;
    }

    split_at = UI_PREVIEW_WIDTH;
    for (index = UI_PREVIEW_WIDTH; index > 0; index--) {
      if (cursor[index] == ' ') {
        split_at = index;
        break;
      }
    }

    memcpy(chunk, cursor, (size_t) split_at);
    chunk[split_at] = '\0';
    snprintf(output, sizeof(output), "  %s", chunk);
    aedoor_write_line(door, output);

    cursor += split_at;
    while (*cursor == ' ') {
      cursor++;
    }
  }
}

static void ui_write_description_text(struct aedoor_context *door, const char *text)
{
  const char *cursor;
  const char *separator;
  char line[256];
  size_t length;

  if (door == NULL) {
    return;
  }

  cursor = text != NULL ? text : "";
  if (*cursor == '\0') {
    aedoor_write_line(door, "  (no description text)");
    return;
  }

  while (*cursor != '\0') {
    separator = strstr(cursor, " | ");
    if (separator == NULL) {
      length = strlen(cursor);
    } else {
      length = (size_t) (separator - cursor);
    }

    if (length >= sizeof(line)) {
      length = sizeof(line) - 1U;
    }

    memcpy(line, cursor, length);
    line[length] = '\0';
    aedoor_write_line(door, line[0] != '\0' ? line : " ");

    if (separator == NULL) {
      break;
    }
    cursor = separator + 3;
  }
}

static void ui_show_help_screen(struct aedoor_context *door, int ui_mode)
{
  long key_value;

  if (door == NULL) {
    return;
  }

  ui_clear_screen(door);
  aedoor_write_line(door, "arbfiles Help");
  aedoor_write_line(door, "");
  aedoor_write_line(door, "Navigation:");
  aedoor_write_line(door, "  , / .  previous or next conference");
  aedoor_write_line(door, "  N / P  next or previous listing area");
  aedoor_write_line(door, "  H      jump to or from held files");
  aedoor_write_line(door, "");
  if (ui_mode == UI_MODE_SOURCE) {
    aedoor_write_line(door, "Source mode:");
    aedoor_write_line(door, "  J / K  move selected file");
    aedoor_write_line(door, "  V      view full selected description");
    aedoor_write_line(door, "  D      switch to destination browse");
  } else {
    aedoor_write_line(door, "Destination mode:");
    aedoor_write_line(door, "  [ / ]  choose destination storage folder");
    aedoor_write_line(door, "  V      view full selected source description");
    aedoor_write_line(door, "  M      move selected source file here");
    aedoor_write_line(door, "  S      return to source browse");
  }
  aedoor_write_line(door, "");
  aedoor_write_line(door, "Notes:");
  aedoor_write_line(door, "  Area is the DIR listing area.");
  aedoor_write_line(door, "  Store is the real file storage folder.");
  aedoor_write_line(door, "  'shared folder' means one folder is shared across listing areas.");
  aedoor_write_line(door, "");
  aedoor_write_line(door, "Press any key to return.");
  ui_wait_for_key(door, &key_value);
}

static void ui_show_full_view(struct aedoor_context *door,
                              const struct dirlist_data *source_dirlist,
                              int selected_entry)
{
  long key_value;
  const struct dirlist_entry *entry;
  char line[320];

  if ((door == NULL) || (source_dirlist == NULL) || (source_dirlist->entry_count <= 0)) {
    return;
  }

  selected_entry = ui_get_selected_index(source_dirlist, selected_entry);
  entry = &source_dirlist->entries[selected_entry];

  ui_clear_screen(door);
  aedoor_write_line(door, "Full Description");
  aedoor_write_line(door, "");
  snprintf(line,
           sizeof(line),
           "File %d/%d: %s",
           selected_entry + 1,
           source_dirlist->entry_count,
           entry->filename[0] != '\0' ? entry->filename : "(unnamed)");
  aedoor_write_line(door, line);
  aedoor_write_line(door, "");
  ui_write_description_text(door,
                            entry->description[0] != '\0'
                              ? entry->description
                              : entry->header_line);
  aedoor_write_line(door, "");
  aedoor_write_line(door, "Press any key to return.");
  ui_wait_for_key(door, &key_value);
}

/* Store and label helpers */
static const char *ui_get_source_store_text(const struct door_config *config,
                                            const struct ae_current_conference_info *conference,
                                            const struct dirlist_data *dirlist,
                                            int active_area)
{
  static char text[64];

  if (conference == NULL) {
    return "(unknown)";
  }

  if ((active_area >= 1) && (active_area <= conference->base.dir_count) &&
      ui_has_direct_area_folder_map(conference) &&
      (conference->download_paths[active_area - 1][0] != '\0')) {
    return conference->download_paths[active_area - 1];
  }

  if ((active_area >= 1) && (active_area <= conference->base.dir_count)) {
    if ((conference->base.download_path_count == 1) &&
        (conference->download_paths[0][0] != '\0')) {
      return conference->download_paths[0];
    }
    if (conference->base.download_path_count > 1) {
      snprintf(text, sizeof(text), "searches %d folders", conference->base.download_path_count);
      return text;
    }
    return "(folder unknown)";
  }

  if (ui_conference_has_hold_area(config, conference) &&
      (active_area == conference->base.dir_count + 1) &&
      (dirlist != NULL) &&
      (dirlist->source_path[0] != '\0')) {
    return dirlist->source_path;
  }

  return "(unknown)";
}

static const char *ui_get_destination_store_text(const struct door_config *config,
                                                 const struct ae_current_conference_info *conference,
                                                 const struct dirlist_data *dirlist,
                                                 int active_area,
                                                 int destination_folder_index)
{
  static char text[320];
  const char *folder_path;

  if (conference == NULL) {
    return "(unknown)";
  }

  if ((active_area >= 1) && (active_area <= conference->base.dir_count)) {
    folder_path = NULL;
    if ((destination_folder_index >= 1) &&
        (destination_folder_index <= conference->base.download_path_count) &&
        (conference->download_paths[destination_folder_index - 1][0] != '\0')) {
      folder_path = conference->download_paths[destination_folder_index - 1];
    }

    if ((conference->base.download_path_count == 1) && (folder_path != NULL)) {
      snprintf(text, sizeof(text), "shared folder 1/1 %s", folder_path);
      return text;
    }

    if (folder_path != NULL) {
      snprintf(text,
               sizeof(text),
               "%d/%d %s",
               destination_folder_index,
               conference->base.download_path_count,
               folder_path);
      return text;
    }

    if (conference->base.download_path_count > 0) {
      snprintf(text, sizeof(text), "choose folder 1-%d", conference->base.download_path_count);
      return text;
    }

    return "(folder unknown)";
  }

  if (ui_conference_has_hold_area(config, conference) &&
      (active_area == conference->base.dir_count + 1) &&
      (dirlist != NULL) &&
      (dirlist->source_path[0] != '\0')) {
    return dirlist->source_path;
  }

  return "(unknown)";
}

/* Screen drawing */
static void ui_write_common_header(struct aedoor_context *door,
                                   const struct door_config *config,
                                   const struct ae_system_config *system_config,
                                   const struct ae_current_conference_info *conference,
                                   int conference_index,
                                   int active_area,
                                   int ui_mode,
                                   const char *store_text)
{
  char line[320];
  int normal_area_count;

  normal_area_count = ui_get_normal_area_count(conference);

  snprintf(line, sizeof(line), "arbfiles %s  Caller: %s", ARBFILES_VERSION, door->username);
  aedoor_write_line(door, line);
  snprintf(line,
           sizeof(line),
           "%s  Conf %d%s: %s",
           ui_mode == UI_MODE_DESTINATION ? "Destination" : "Source",
           conference != NULL ? conference->base.number : (door->current_conf + 1),
           ((conference != NULL) && (conference->base.number == (door->current_conf + 1))) ? " current" : "",
           (conference != NULL) && (conference->base.name[0] != '\0')
             ? conference->base.name
             : (door->conf_name[0] != '\0' ? door->conf_name : "(unknown)"));
  aedoor_write_line(door, line);
  snprintf(line,
           sizeof(line),
           "Browse %d/%d  Area %d/%d %s",
           (conference_index >= 0 ? conference_index + 1 : 1),
           system_config != NULL && system_config->discovered_count > 0 ? system_config->discovered_count : 1,
           active_area,
           normal_area_count > 0 ? normal_area_count : 1,
           ui_get_area_label(config, conference, active_area));
  aedoor_write_line(door, line);
  snprintf(line, sizeof(line), "Store: %s", store_text);
  aedoor_write_line(door, line);
}

static void ui_write_source_screen(struct aedoor_context *door,
                                   const struct door_config *config,
                                   const struct ae_system_config *system_config,
                                   const struct ae_current_conference_info *conference,
                                   const struct dirlist_data *dirlist,
                                   const char *status_message,
                                   int conference_index,
                                   int active_area,
                                   int selected_entry)
{
  char line[320];
  int displayed_entries;
  int selected_index;
  int visible_start;
  int index;

  ui_write_common_header(door,
                         config,
                         system_config,
                         conference,
                         conference_index,
                         active_area,
                         UI_MODE_SOURCE,
                         ui_get_source_store_text(config, conference, dirlist, active_area));

  snprintf(line,
           sizeof(line),
           "Files: %d  Dirs:%d  Status: %s",
           dirlist != NULL ? dirlist->entry_count : 0,
           conference != NULL ? conference->base.dir_count : 0,
           (dirlist != NULL) && (dirlist->status_text[0] != '\0') ? dirlist->status_text : "(unknown)");
  aedoor_write_line(door, line);
  aedoor_write_line(door, "Source files:");

  if ((dirlist == NULL) || (dirlist->entry_count <= 0)) {
    aedoor_write_line(door, "  (no entries loaded)");
  } else {
    selected_index = ui_get_selected_index(dirlist, selected_entry);
    visible_start = ui_get_visible_start(dirlist, selected_index);
    displayed_entries = dirlist->entry_count;
    if (displayed_entries > UI_VISIBLE_ENTRIES) {
      displayed_entries = UI_VISIBLE_ENTRIES;
    }
    if (visible_start + displayed_entries > dirlist->entry_count) {
      displayed_entries = dirlist->entry_count - visible_start;
    }

    for (index = 0; index < displayed_entries; index++) {
      int entry_index;

      entry_index = visible_start + index;
      snprintf(line,
               sizeof(line),
               " %c %s",
               entry_index == selected_index ? '>' : ' ',
               dirlist->entries[entry_index].header_line);
      aedoor_write_line(door, line);
    }

    snprintf(line,
             sizeof(line),
             "Selected %d/%d: %s",
             selected_index + 1,
             dirlist->entry_count,
             dirlist->entries[selected_index].filename[0] != '\0'
               ? dirlist->entries[selected_index].filename
               : "(unnamed)");
    aedoor_write_line(door, line);
    aedoor_write_line(door, "Preview:");
    ui_write_preview_lines(door, ui_get_preview_text(dirlist, selected_index));
  }

  if ((system_config != NULL) && (system_config->discovered_count > 1)) {
    aedoor_write_line(door, "Confs: , previous  . next");
  }
  if ((status_message != NULL) && (status_message[0] != '\0')) {
    snprintf(line, sizeof(line), "Message: %s", status_message);
    aedoor_write_line(door, line);
  }
  aedoor_write_line(door, "Keys: Q quit  R redraw  J/K select  D destination  N/P areas  H hold");
  aedoor_write_line(door, "More: V view full desc  ? help");
}

static void ui_write_destination_screen(struct aedoor_context *door,
                                        const struct door_config *config,
                                        const struct ae_system_config *system_config,
                                        const struct ae_current_conference_info *source_conference,
                                        const struct dirlist_data *source_dirlist,
                                        const struct ae_current_conference_info *destination_conference,
                                        const struct dirlist_data *destination_dirlist,
                                        const char *status_message,
                                        int source_conference_index,
                                        int source_area,
                                        int selected_entry,
                                        int destination_conference_index,
                                        int destination_area,
                                        int destination_folder_index)
{
  char line[320];
  const struct dirlist_entry *source_selected;
  int displayed_entries;
  int visible_start;
  int index;

  ui_write_common_header(door,
                         config,
                         system_config,
                         destination_conference,
                         destination_conference_index,
                         destination_area,
                         UI_MODE_DESTINATION,
                         ui_get_destination_store_text(config,
                                                       destination_conference,
                                                       destination_dirlist,
                                                       destination_area,
                                                       destination_folder_index));

  source_selected = NULL;
  if ((source_dirlist != NULL) && (source_dirlist->entry_count > 0)) {
    source_selected = &source_dirlist->entries[ui_get_selected_index(source_dirlist, selected_entry)];
  }

  snprintf(line,
           sizeof(line),
           "Target: %s",
           (source_selected != NULL) && (source_selected->filename[0] != '\0')
             ? source_selected->filename
             : "(no source file selected)");
  aedoor_write_line(door, line);
  snprintf(line,
           sizeof(line),
           "From Conf %d Area %d  Files: %d",
           source_conference != NULL ? source_conference->base.number : (source_conference_index + 1),
           source_area,
           destination_dirlist != NULL ? destination_dirlist->entry_count : 0);
  aedoor_write_line(door, line);
  aedoor_write_line(door, "Destination files:");

  if ((destination_dirlist == NULL) || (destination_dirlist->entry_count <= 0)) {
    aedoor_write_line(door, "  (no entries loaded)");
  } else {
    visible_start = 0;
    displayed_entries = destination_dirlist->entry_count;
    if (displayed_entries > UI_VISIBLE_ENTRIES) {
      displayed_entries = UI_VISIBLE_ENTRIES;
    }

    for (index = 0; index < displayed_entries; index++) {
      int entry_index;

      entry_index = visible_start + index;
      snprintf(line, sizeof(line), "   %s", destination_dirlist->entries[entry_index].header_line);
      aedoor_write_line(door, line);
    }

    if (destination_dirlist->entry_count > displayed_entries) {
      snprintf(line, sizeof(line), "  ... %d more", destination_dirlist->entry_count - displayed_entries);
      aedoor_write_line(door, line);
    }
  }

  if ((system_config != NULL) && (system_config->discovered_count > 1)) {
    aedoor_write_line(door, "Confs: , previous  . next");
  }
  if ((status_message != NULL) && (status_message[0] != '\0')) {
    snprintf(line, sizeof(line), "Message: %s", status_message);
    aedoor_write_line(door, line);
  }
  aedoor_write_line(door, "Keys: Q quit  R redraw  S source  M move  N/P areas  [/ ] store  H hold");
  aedoor_write_line(door, "More: V view full desc  ? help");
}

/* Top-level redraw and event loop */
static void ui_draw_screen(struct aedoor_context *door,
                           const struct door_config *config,
                           const struct ae_system_config *system_config,
                           const struct ae_current_conference_info *source_conference,
                           const struct dirlist_data *source_dirlist,
                           const struct ae_current_conference_info *destination_conference,
                           const struct dirlist_data *destination_dirlist,
                           const char *status_message,
                           int ui_mode,
                           int active_conference_index,
                           int active_area,
                           int selected_entry,
                           int destination_conference_index,
                           int destination_area,
                           int destination_folder_index)
{
  ui_clear_screen(door);
  if (ui_mode == UI_MODE_DESTINATION) {
    ui_write_destination_screen(door,
                                config,
                                system_config,
                                source_conference,
                                source_dirlist,
                                destination_conference,
                                destination_dirlist,
                                status_message,
                                active_conference_index,
                                active_area,
                                selected_entry,
                                destination_conference_index,
                                destination_area,
                                destination_folder_index);
  } else {
    ui_write_source_screen(door,
                           config,
                           system_config,
                           source_conference,
                           source_dirlist,
                           status_message,
                           active_conference_index,
                           active_area,
                           selected_entry);
  }
}

int ui_run(const struct door_config *config,
           struct aedoor_context *door,
           const struct ae_system_config *system_config,
           const struct ae_current_conference_info *source_conference,
           const struct dirlist_data *source_dirlist,
           const struct ae_current_conference_info *destination_conference,
           const struct dirlist_data *destination_dirlist,
           const char *status_message,
           int *ui_mode,
           int *active_conference_index,
           int *active_area,
           int *selected_entry,
           int *destination_conference_index,
           int *destination_area,
           int *destination_folder_index,
           struct doorlog *log,
           char *error_text,
           int error_text_size)
{
  const struct ae_current_conference_info *display_conference;
  int *display_conference_index;
  int *display_area;
  long key_value;
  int hold_area_index;
  int normal_area_count;
  int poll_status;

  if ((config == NULL) || (door == NULL) || (system_config == NULL) ||
      (ui_mode == NULL) ||
      (active_conference_index == NULL) || (active_area == NULL) || (selected_entry == NULL) ||
      (destination_conference_index == NULL) || (destination_area == NULL) || (destination_folder_index == NULL)) {
    ui_set_error(error_text, error_text_size, "invalid UI request");
    return -1;
  }

  display_conference = *ui_mode == UI_MODE_DESTINATION ? destination_conference : source_conference;
  display_conference_index = *ui_mode == UI_MODE_DESTINATION ? destination_conference_index : active_conference_index;
  display_area = *ui_mode == UI_MODE_DESTINATION ? destination_area : active_area;

  *selected_entry = ui_get_selected_index(source_dirlist, *selected_entry);
  normal_area_count = ui_get_normal_area_count(display_conference);
  hold_area_index = ui_get_hold_area_index(config, display_conference);

  ui_draw_screen(door,
                 config,
                 system_config,
                 source_conference,
                 source_dirlist,
                 destination_conference,
                 destination_dirlist,
                 status_message,
                 *ui_mode,
                 *active_conference_index,
                 *active_area,
                 *selected_entry,
                 *destination_conference_index,
                 *destination_area,
                 *destination_folder_index);
  doorlog_write(log, "UI scaffold shown to caller.");

  for (;;) {
    poll_status = aedoor_poll_key(door, &key_value);
    if (poll_status < 0) {
      ui_set_error(error_text, error_text_size, "caller input failed");
      doorlog_write(log, "UI input polling failed.");
      return -1;
    }

    if (poll_status == 0) {
      Delay(2);
      continue;
    }

    if ((key_value == 'Q') || (key_value == 'q')) {
      doorlog_write(log, "Caller left the scaffold UI.");
      if (error_text != NULL) {
        error_text[0] = '\0';
      }
      return UI_RESULT_EXIT;
    }

    if ((key_value == 'R') || (key_value == 'r')) {
      doorlog_write(log, "Caller requested UI redraw.");
      ui_draw_screen(door,
                     config,
                     system_config,
                     source_conference,
                     source_dirlist,
                     destination_conference,
                     destination_dirlist,
                     status_message,
                     *ui_mode,
                     *active_conference_index,
                     *active_area,
                     *selected_entry,
                     *destination_conference_index,
                     *destination_area,
                     *destination_folder_index);
      continue;
    }

    if (key_value == '?') {
      doorlog_write(log, "Caller opened the help screen.");
      ui_show_help_screen(door, *ui_mode);
      ui_draw_screen(door,
                     config,
                     system_config,
                     source_conference,
                     source_dirlist,
                     destination_conference,
                     destination_dirlist,
                     status_message,
                     *ui_mode,
                     *active_conference_index,
                     *active_area,
                     *selected_entry,
                     *destination_conference_index,
                     *destination_area,
                     *destination_folder_index);
      continue;
    }

    if (((key_value == 'V') || (key_value == 'v')) &&
        (source_dirlist != NULL) &&
        (source_dirlist->entry_count > 0)) {
      doorlog_write(log, "Caller opened full description view.");
      ui_show_full_view(door, source_dirlist, *selected_entry);
      ui_draw_screen(door,
                     config,
                     system_config,
                     source_conference,
                     source_dirlist,
                     destination_conference,
                     destination_dirlist,
                     status_message,
                     *ui_mode,
                     *active_conference_index,
                     *active_area,
                     *selected_entry,
                     *destination_conference_index,
                     *destination_area,
                     *destination_folder_index);
      continue;
    }

    if ((*ui_mode == UI_MODE_SOURCE) && ((key_value == 'D') || (key_value == 'd'))) {
      *ui_mode = UI_MODE_DESTINATION;
      doorlog_write(log, "Caller switched to destination browse mode.");
      if (error_text != NULL) {
        error_text[0] = '\0';
      }
      return UI_RESULT_AREA_CHANGE;
    }

    if ((*ui_mode == UI_MODE_DESTINATION) && ((key_value == 'S') || (key_value == 's'))) {
      *ui_mode = UI_MODE_SOURCE;
      doorlog_write(log, "Caller returned to source browse mode.");
      if (error_text != NULL) {
        error_text[0] = '\0';
      }
      return UI_RESULT_AREA_CHANGE;
    }

    if ((*ui_mode == UI_MODE_DESTINATION) && ((key_value == 'M') || (key_value == 'm'))) {
      doorlog_write(log, "Caller requested a move operation.");
      if (error_text != NULL) {
        error_text[0] = '\0';
      }
      return UI_RESULT_MOVE;
    }

    if ((*ui_mode == UI_MODE_SOURCE) &&
        (((key_value == 'K') || (key_value == 'k') || (key_value == 4)) &&
         (source_dirlist != NULL) && (source_dirlist->entry_count > 0))) {
      if (*selected_entry > 0) {
        (*selected_entry)--;
        doorlog_writef(log, "Caller moved selection to file %d", *selected_entry + 1);
      }
      ui_draw_screen(door,
                     config,
                     system_config,
                     source_conference,
                     source_dirlist,
                     destination_conference,
                     destination_dirlist,
                     status_message,
                     *ui_mode,
                     *active_conference_index,
                     *active_area,
                     *selected_entry,
                     *destination_conference_index,
                     *destination_area,
                     *destination_folder_index);
      continue;
    }

    if ((*ui_mode == UI_MODE_SOURCE) &&
        (((key_value == 'J') || (key_value == 'j') || (key_value == 5)) &&
         (source_dirlist != NULL) && (source_dirlist->entry_count > 0))) {
      if (*selected_entry + 1 < source_dirlist->entry_count) {
        (*selected_entry)++;
        doorlog_writef(log, "Caller moved selection to file %d", *selected_entry + 1);
      }
      ui_draw_screen(door,
                     config,
                     system_config,
                     source_conference,
                     source_dirlist,
                     destination_conference,
                     destination_dirlist,
                     status_message,
                     *ui_mode,
                     *active_conference_index,
                     *active_area,
                     *selected_entry,
                     *destination_conference_index,
                     *destination_area,
                     *destination_folder_index);
      continue;
    }

    if ((*ui_mode == UI_MODE_DESTINATION) &&
        (display_conference != NULL) &&
        (*display_area >= 1) &&
        (*display_area <= display_conference->base.dir_count) &&
        ((key_value == ']') || (key_value == '}')) &&
        (*destination_folder_index < display_conference->base.download_path_count)) {
      (*destination_folder_index)++;
      doorlog_writef(log, "Caller moved to destination folder %d", *destination_folder_index);
      ui_draw_screen(door,
                     config,
                     system_config,
                     source_conference,
                     source_dirlist,
                     destination_conference,
                     destination_dirlist,
                     status_message,
                     *ui_mode,
                     *active_conference_index,
                     *active_area,
                     *selected_entry,
                     *destination_conference_index,
                     *destination_area,
                     *destination_folder_index);
      continue;
    }

    if ((*ui_mode == UI_MODE_DESTINATION) &&
        (display_conference != NULL) &&
        (*display_area >= 1) &&
        (*display_area <= display_conference->base.dir_count) &&
        ((key_value == '[') || (key_value == '{')) &&
        (*destination_folder_index > 1)) {
      (*destination_folder_index)--;
      doorlog_writef(log, "Caller moved to destination folder %d", *destination_folder_index);
      ui_draw_screen(door,
                     config,
                     system_config,
                     source_conference,
                     source_dirlist,
                     destination_conference,
                     destination_dirlist,
                     status_message,
                     *ui_mode,
                     *active_conference_index,
                     *active_area,
                     *selected_entry,
                     *destination_conference_index,
                     *destination_area,
                     *destination_folder_index);
      continue;
    }

    if ((key_value == '.') &&
        (system_config->discovered_count > 1) &&
        (*display_conference_index + 1 < system_config->discovered_count)) {
      (*display_conference_index)++;
      doorlog_writef(log, "Caller moved to conference slot %d", *display_conference_index + 1);
      if (error_text != NULL) {
        error_text[0] = '\0';
      }
      return UI_RESULT_CONFERENCE_CHANGE;
    }

    if ((key_value == ',') &&
        (system_config->discovered_count > 1) &&
        (*display_conference_index > 0)) {
      (*display_conference_index)--;
      doorlog_writef(log, "Caller moved to conference slot %d", *display_conference_index + 1);
      if (error_text != NULL) {
        error_text[0] = '\0';
      }
      return UI_RESULT_CONFERENCE_CHANGE;
    }

    if (((key_value == 'N') || (key_value == 'n')) &&
        (normal_area_count > 0) &&
        (*display_area >= 1) &&
        (*display_area < normal_area_count)) {
      (*display_area)++;
      doorlog_writef(log, "Caller moved to area %d", *display_area);
      if (error_text != NULL) {
        error_text[0] = '\0';
      }
      return UI_RESULT_AREA_CHANGE;
    }

    if (((key_value == 'P') || (key_value == 'p')) &&
        (*display_area == hold_area_index) &&
        (normal_area_count > 0)) {
      *display_area = normal_area_count;
      doorlog_writef(log, "Caller returned from hold area to area %d", *display_area);
      if (error_text != NULL) {
        error_text[0] = '\0';
      }
      return UI_RESULT_AREA_CHANGE;
    }

    if (((key_value == 'P') || (key_value == 'p')) &&
        (normal_area_count > 0) &&
        (*display_area > 1) &&
        (*display_area <= normal_area_count)) {
      (*display_area)--;
      doorlog_writef(log, "Caller moved to area %d", *display_area);
      if (error_text != NULL) {
        error_text[0] = '\0';
      }
      return UI_RESULT_AREA_CHANGE;
    }

    if (((key_value == 'H') || (key_value == 'h')) && (hold_area_index > 0)) {
      if (*display_area == hold_area_index) {
        *display_area = normal_area_count > 0 ? normal_area_count : 1;
      } else {
        *display_area = hold_area_index;
      }
      doorlog_writef(log, "Caller switched to special area %d", *display_area);
      if (error_text != NULL) {
        error_text[0] = '\0';
      }
      return UI_RESULT_AREA_CHANGE;
    }
  }
}
