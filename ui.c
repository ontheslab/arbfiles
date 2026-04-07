/*
 * Interactive browse UI.
 *
 * Ref: the current screen style is ARBFILES-specific, but the colour and menu
 * feel are checked against Ami-Express screens rather than generic ANSI UIs.
 */
#include "ui.h"
#include "aedoor_bridge.h"
#include "ae_config_scan.h"
#include "dirlist.h"
#include "door_config.h"
#include "door_version.h"
#include "doorlog.h"
#include "tagset.h"

#include <proto/dos.h>

#include <ctype.h>
#include <stdio.h>
#include <string.h>

#define UI_VISIBLE_ENTRIES 9
#define UI_LIST_WIDTH 76
#define UI_PREVIEW_WIDTH 74
#define UI_PREVIEW_LINES 2
#define UI_MODAL_DESCRIPTION_WIDTH 79
#define UI_MODAL_BODY_LINES 16

#define UI_COLOUR_RESET   "\033[0m"
#define UI_COLOUR_CYAN    "\033[36m"
#define UI_COLOUR_GREEN   "\033[32m"
#define UI_COLOUR_YELLOW  "\033[33m"
#define UI_COLOUR_WHITE   "\033[37m"
#define UI_COLOUR_BLUE    "\033[34m"
#define UI_COLOUR_MAGENTA "\033[35m"
#define UI_COLOUR_RED     "\033[31m"

/* Low-level UI helpers */
static void ui_clear_screen(struct aedoor_context *door);

static int ui_colour_enabled(const struct aedoor_context *door)
{
  return (door != NULL) && (door->ansi_capable != 0);
}

static const char *ui_colour(const struct aedoor_context *door, const char *colour_code)
{
  return ui_colour_enabled(door) ? colour_code : "";
}

static const char *ui_reset(const struct aedoor_context *door)
{
  return ui_colour_enabled(door) ? UI_COLOUR_RESET : "";
}

static void ui_write_heading_line(struct aedoor_context *door, const char *text)
{
  char line[320];

  if ((door == NULL) || (text == NULL)) {
    return;
  }

  if (ui_colour_enabled(door)) {
    snprintf(line, sizeof(line), "%s%s%s", UI_COLOUR_CYAN, text, UI_COLOUR_RESET);
    aedoor_write_line(door, line);
  } else {
    aedoor_write_line(door, text);
  }
}

static void ui_write_green_line(struct aedoor_context *door, const char *text)
{
  char line[320];

  if ((door == NULL) || (text == NULL)) {
    return;
  }

  if (ui_colour_enabled(door)) {
    snprintf(line, sizeof(line), "%s%s%s", UI_COLOUR_GREEN, text, UI_COLOUR_RESET);
    aedoor_write_line(door, line);
  } else {
    aedoor_write_line(door, text);
  }
}

static void ui_append_text(char *buffer, size_t buffer_size, const char *text)
{
  size_t used;
  size_t remaining;

  if ((buffer == NULL) || (buffer_size == 0U) || (text == NULL)) {
    return;
  }

  used = strlen(buffer);
  if (used >= buffer_size - 1U) {
    return;
  }

  remaining = buffer_size - used - 1U;
  strncat(buffer, text, remaining);
}

static void ui_append_key_token(char *buffer,
                                size_t buffer_size,
                                const struct aedoor_context *door,
                                char open_bracket,
                                const char *token,
                                char close_bracket)
{
  char text[96];
  char *output;
  const char *cursor;

  if ((buffer == NULL) || (buffer_size == 0U) || (token == NULL)) {
    return;
  }

  if ((open_bracket == '\0') && (close_bracket == '\0')) {
    if ((strcmp(token, "[/]") == 0) || (strcmp(token, "{/}") == 0) || (strcmp(token, "-/=") == 0)) {
      const char *first_token;
      const char *second_token;
      char first_char;
      char second_char;

      if (strcmp(token, "[/]") == 0) {
        first_char = '[';
        second_char = ']';
      } else if (strcmp(token, "{/}") == 0) {
        first_char = '{';
        second_char = '}';
      } else {
        first_char = '-';
        second_char = '=';
      }

      first_token = (first_char == '[') ? "[" : ((first_char == '{') ? "{" : "-");
      second_token = (second_char == ']') ? "]" : ((second_char == '}') ? "}" : "=");
      ui_append_key_token(buffer, buffer_size, door, '(', first_token, ')');
      if (ui_colour_enabled(door)) {
        ui_append_text(buffer, buffer_size, UI_COLOUR_GREEN);
        ui_append_text(buffer, buffer_size, "/");
        ui_append_text(buffer, buffer_size, UI_COLOUR_RESET);
      } else {
        ui_append_text(buffer, buffer_size, "/");
      }
      ui_append_key_token(buffer, buffer_size, door, '(', second_token, ')');
      return;
    }

    text[0] = '\0';
    output = text;
    cursor = token;
    while ((*cursor != '\0') && ((size_t) (output - text) + 12U < sizeof(text))) {
      if ((*cursor == '[') || (*cursor == ']') || (*cursor == '(') || (*cursor == ')')) {
        output += snprintf(output,
                           sizeof(text) - (size_t) (output - text),
                           "%s%c",
                           ui_colour_enabled(door) ? UI_COLOUR_GREEN : "",
                           *cursor);
      } else {
        output += snprintf(output,
                           sizeof(text) - (size_t) (output - text),
                           "%s%c",
                           ui_colour_enabled(door) ? UI_COLOUR_YELLOW : "",
                           *cursor);
      }
      cursor++;
    }
    if (ui_colour_enabled(door)) {
      ui_append_text(text, sizeof(text), UI_COLOUR_RESET);
    }
    ui_append_text(buffer, buffer_size, text);
    return;
  }

  if (ui_colour_enabled(door)) {
    snprintf(text,
             sizeof(text),
             "%s%c%s%s%s%c%s",
             UI_COLOUR_GREEN,
             open_bracket,
             UI_COLOUR_YELLOW,
             token,
             UI_COLOUR_GREEN,
             close_bracket,
             UI_COLOUR_RESET);
  } else {
    snprintf(text, sizeof(text), "%c%s%c", open_bracket, token, close_bracket);
  }

  ui_append_text(buffer, buffer_size, text);
}

static void ui_write_menu_line(struct aedoor_context *door,
                               const char *label,
                               const char *tokens[][3],
                               int token_count)
{
  char line[512];
  int index;

  if ((door == NULL) || (label == NULL) || (tokens == NULL) || (token_count <= 0)) {
    return;
  }

  line[0] = '\0';
  if (ui_colour_enabled(door)) {
    snprintf(line,
             sizeof(line),
             "%s%s%s:%s ",
             UI_COLOUR_CYAN,
             label,
             UI_COLOUR_YELLOW,
             UI_COLOUR_RESET);
  } else {
    snprintf(line, sizeof(line), "%s: ", label);
  }

  for (index = 0; index < token_count; index++) {
    char open_bracket;
    char close_bracket;

    open_bracket = tokens[index][0][0];
    close_bracket = tokens[index][2][0];
    ui_append_key_token(line, sizeof(line), door, open_bracket, tokens[index][1], close_bracket);
    if (index + 1 < token_count) {
      ui_append_text(line, sizeof(line), " ");
    }
  }

  aedoor_write_line(door, line);
}

static void ui_write_yellow_line(struct aedoor_context *door, const char *text)
{
  char line[320];

  if ((door == NULL) || (text == NULL)) {
    return;
  }

  if (ui_colour_enabled(door)) {
    snprintf(line, sizeof(line), "%s%s%s", UI_COLOUR_YELLOW, text, UI_COLOUR_RESET);
    aedoor_write_line(door, line);
  } else {
    aedoor_write_line(door, text);
  }
}

static void ui_write_label_value_line(struct aedoor_context *door,
                                      const char *label,
                                      const char *value)
{
  char line[320];
  const char *label_start;
  char display_label[128];
  char indent[32];
  size_t indent_length;
  size_t label_length;
  int has_colon;

  if ((door == NULL) || (label == NULL) || (value == NULL)) {
    return;
  }

  label_start = label;
  while (*label_start == ' ') {
    label_start++;
  }

  indent_length = (size_t) (label_start - label);
  if (indent_length >= sizeof(indent)) {
    indent_length = sizeof(indent) - 1U;
  }
  memcpy(indent, label, indent_length);
  indent[indent_length] = '\0';

  label_length = strlen(label_start);
  has_colon = (label_length > 0U) && (label_start[label_length - 1U] == ':');
  if (has_colon) {
    label_length--;
  }
  if (label_length >= sizeof(display_label)) {
    label_length = sizeof(display_label) - 1U;
  }
  memcpy(display_label, label_start, label_length);
  display_label[label_length] = '\0';

  if (ui_colour_enabled(door)) {
    if (has_colon) {
      snprintf(line,
               sizeof(line),
               "%s%s%s%s:%s %s%s%s",
               indent,
               UI_COLOUR_CYAN,
               display_label,
               UI_COLOUR_YELLOW,
               UI_COLOUR_RESET,
               UI_COLOUR_WHITE,
               value,
               UI_COLOUR_RESET);
    } else {
      snprintf(line,
               sizeof(line),
               "%s%s%s%s %s%s%s",
               indent,
               UI_COLOUR_CYAN,
               display_label,
               UI_COLOUR_RESET,
               UI_COLOUR_WHITE,
               value,
               UI_COLOUR_RESET);
    }
    aedoor_write_line(door, line);
  } else {
    snprintf(line, sizeof(line), "%s %s", label, value);
    aedoor_write_line(door, line);
  }
}

static void ui_write_action_choices(struct aedoor_context *door,
                                    const char *tokens[][3],
                                    const char *descriptions[],
                                    int option_count)
{
  char line[512];
  int index;

  if ((door == NULL) || (tokens == NULL) || (descriptions == NULL) || (option_count <= 0)) {
    return;
  }

  line[0] = '\0';
  for (index = 0; index < option_count; index++) {
    ui_append_key_token(line,
                        sizeof(line),
                        door,
                        tokens[index][0][0],
                        tokens[index][1],
                        tokens[index][2][0]);
    ui_append_text(line, sizeof(line), " ");
    if (ui_colour_enabled(door)) {
      ui_append_text(line, sizeof(line), UI_COLOUR_GREEN);
      ui_append_text(line, sizeof(line), descriptions[index]);
      ui_append_text(line, sizeof(line), UI_COLOUR_RESET);
    } else {
      ui_append_text(line, sizeof(line), descriptions[index]);
    }
    if (index + 1 < option_count) {
      ui_append_text(line, sizeof(line), "  ");
    }
  }

  aedoor_write_line(door, line);
}

static void ui_write_key_help_line(struct aedoor_context *door,
                                   char open_bracket,
                                   const char *keys,
                                   char close_bracket,
                                   const char *description)
{
  char line[512];

  if ((door == NULL) || (keys == NULL) || (description == NULL)) {
    return;
  }

  line[0] = '\0';
  ui_append_text(line, sizeof(line), "  ");
  ui_append_key_token(line, sizeof(line), door, open_bracket, keys, close_bracket);
  ui_append_text(line, sizeof(line), " ");
  if (ui_colour_enabled(door)) {
    ui_append_text(line, sizeof(line), UI_COLOUR_GREEN);
    ui_append_text(line, sizeof(line), description);
    ui_append_text(line, sizeof(line), UI_COLOUR_RESET);
  } else {
    ui_append_text(line, sizeof(line), description);
  }
  aedoor_write_line(door, line);
}

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

/* Modal progress, result, and confirm screens */
void ui_show_move_progress(struct aedoor_context *door,
                           const char *filename,
                           const char *source_store,
                           const char *destination_store)
{
  char line[320];

  if (door == NULL) {
    return;
  }

  ui_clear_screen(door);
  ui_write_heading_line(door, "Moving File");
  aedoor_write_line(door, "");
  ui_write_label_value_line(door, "File:", filename != NULL ? filename : "(unknown)");
  ui_write_label_value_line(door, "From:", source_store != NULL ? source_store : "(unknown)");
  snprintf(line, sizeof(line), "To: %s", destination_store != NULL ? destination_store : "(unknown)");
  ui_write_label_value_line(door, "To:", destination_store != NULL ? destination_store : "(unknown)");
  aedoor_write_line(door, "");
  ui_write_green_line(door, "Please wait...");
}

void ui_show_batch_move_progress(struct aedoor_context *door,
                                 const char *filename,
                                 const char *source_store,
                                 const char *destination_store,
                                 int current_file,
                                 int total_files)
{
  char line[320];

  if (door == NULL) {
    return;
  }

  ui_clear_screen(door);
  ui_write_heading_line(door, "Moving Tagged Files");
  aedoor_write_line(door, "");
  snprintf(line, sizeof(line), "%d of %d", current_file, total_files);
  ui_write_label_value_line(door, "File:", line);
  ui_write_label_value_line(door, "Name:", filename != NULL ? filename : "(unknown)");
  ui_write_label_value_line(door, "From:", source_store != NULL ? source_store : "(unknown)");
  ui_write_label_value_line(door, "To:", destination_store != NULL ? destination_store : "(unknown)");
  aedoor_write_line(door, "");
  ui_write_green_line(door, "Please wait...");
}

void ui_show_move_result(struct aedoor_context *door, int move_ok, const char *message)
{
  long key_value;

  if (door == NULL) {
    return;
  }

  ui_clear_screen(door);
  if (move_ok > 1) {
    ui_write_heading_line(door, "Move Partly Complete");
  } else {
    ui_write_heading_line(door, move_ok ? "Move Complete" : "Move Failed");
  }
  aedoor_write_line(door, "");
  if ((message != NULL) && (*message != '\0')) {
    aedoor_write_line(door, message);
    aedoor_write_line(door, "");
  }
  ui_write_green_line(door, "Press any key to return.");
  ui_wait_for_key(door, &key_value);
}

void ui_show_delete_progress(struct aedoor_context *door,
                             int delete_mode,
                             const char *filename,
                             const char *source_store,
                             const char *target_store)
{
  char line[320];

  if (door == NULL) {
    return;
  }

  ui_clear_screen(door);
  ui_write_heading_line(door, delete_mode == UI_DELETE_TRASH ? "Moving File To Trash" : "Deleting File");
  aedoor_write_line(door, "");
  ui_write_label_value_line(door, "File:", filename != NULL ? filename : "(unknown)");
  ui_write_label_value_line(door, "From:", source_store != NULL ? source_store : "(unknown)");
  if ((target_store != NULL) && (*target_store != '\0')) {
    ui_write_label_value_line(door,
                              delete_mode == UI_DELETE_TRASH ? "Trash:" : "Target:",
                              target_store);
  }
  aedoor_write_line(door, "");
  ui_write_green_line(door, "Please wait...");
}

void ui_show_delete_result(struct aedoor_context *door, int delete_ok, const char *message)
{
  long key_value;

  if (door == NULL) {
    return;
  }

  ui_clear_screen(door);
  ui_write_heading_line(door, delete_ok ? "Delete Complete" : "Delete Failed");
  aedoor_write_line(door, "");
  if ((message != NULL) && (*message != '\0')) {
    aedoor_write_line(door, message);
    aedoor_write_line(door, "");
  }
  ui_write_green_line(door, "Press any key to return.");
  ui_wait_for_key(door, &key_value);
}

int ui_confirm_orphan_delete(struct aedoor_context *door,
                             const char *filename,
                             const char *message)
{
  long key_value;
  static const char *orphan_tokens[][3] = {{"(", "Y", ")"}, {"(", "N", ")"}};
  static const char *orphan_descriptions[] = {"remove orphan entry", "cancel"};

  if (door == NULL) {
    return -1;
  }

  ui_clear_screen(door);
  ui_write_heading_line(door, "Orphan File Entry");
  aedoor_write_line(door, "");
  ui_write_label_value_line(door, "File:", filename != NULL ? filename : "(unknown)");
  if ((message != NULL) && (*message != '\0')) {
    aedoor_write_line(door, "");
    aedoor_write_line(door, message);
  }
  aedoor_write_line(door, "");
  ui_write_green_line(door, "Remove the listing entry only?");
  ui_write_action_choices(door, orphan_tokens, orphan_descriptions, 2);

  for (;;) {
    if (ui_wait_for_key(door, &key_value) != 0) {
      return -1;
    }
    if ((key_value == 'Y') || (key_value == 'y')) {
      return 1;
    }
    if ((key_value == 'N') || (key_value == 'n') || (key_value == 27)) {
      return 0;
    }
  }
}

/* Source and destination browse helpers */
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

static int ui_has_trash_area(const struct door_config *config)
{
  return (config != NULL) && (config->trash_path[0] != '\0');
}

static int ui_get_trash_area_index(const struct door_config *config,
                                   const struct ae_current_conference_info *conference)
{
  if ((conference == NULL) || !ui_has_trash_area(config)) {
    return 0;
  }

  return conference->base.dir_count + (ui_conference_has_hold_area(config, conference) ? 2 : 1);
}

static int ui_has_direct_area_folder_map(const struct ae_current_conference_info *conference)
{
  return (conference != NULL) &&
         (conference->base.dir_count > 0) &&
         (conference->base.dir_count == conference->base.download_path_count);
}

static int ui_paths_match(const char *left, const char *right)
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

static int ui_get_populated_download_folder_count(const struct ae_current_conference_info *conference)
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

static int ui_get_download_folder_slot_for_order(const struct ae_current_conference_info *conference,
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

static int ui_uses_rotated_area_folder_map(const struct ae_current_conference_info *conference)
{
  if (conference == NULL) {
    return 0;
  }

  if ((conference->base.dir_count <= 1) ||
      (ui_get_populated_download_folder_count(conference) < conference->base.dir_count) ||
      (conference->base.upload_path_count <= 0)) {
    return 0;
  }

  if ((ui_get_download_folder_slot_for_order(conference, 1) <= 0) ||
      (conference->upload_paths[0][0] == '\0')) {
    return 0;
  }

  return ui_paths_match(conference->download_paths[ui_get_download_folder_slot_for_order(conference, 1) - 1],
                        conference->upload_paths[0]);
}

static int ui_get_preferred_folder_for_area(const struct ae_current_conference_info *conference,
                                            int active_area)
{
  int rotated_index;

  if (conference == NULL) {
    return 1;
  }

  if ((active_area < 1) || (active_area > conference->base.dir_count)) {
    return 1;
  }

  if (ui_uses_rotated_area_folder_map(conference)) {
    rotated_index = active_area + 1;
    if (rotated_index > conference->base.dir_count) {
      rotated_index = 1;
    }
    rotated_index = ui_get_download_folder_slot_for_order(conference, rotated_index);
    if ((rotated_index >= 1) &&
        (rotated_index <= conference->base.download_path_count) &&
        (conference->download_paths[rotated_index - 1][0] != '\0')) {
      return rotated_index;
    }
  }

  rotated_index = ui_get_download_folder_slot_for_order(conference, active_area);
  if ((rotated_index >= 1) &&
      (rotated_index <= conference->base.download_path_count) &&
      (conference->download_paths[rotated_index - 1][0] != '\0')) {
    return rotated_index;
  }

  for (rotated_index = 1; rotated_index <= conference->base.download_path_count; rotated_index++) {
    if (conference->download_paths[rotated_index - 1][0] != '\0') {
      return rotated_index;
    }
  }

  return 1;
}

static const char *ui_get_source_store_text(const struct door_config *config,
                                            const struct ae_current_conference_info *conference,
                                            const struct dirlist_data *dirlist,
                                            int active_area);

static const char *ui_get_destination_store_text(const struct door_config *config,
                                                 const struct ae_current_conference_info *conference,
                                                 const struct dirlist_data *dirlist,
                                                 int active_area,
                                                 int destination_folder_index);

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

  if ((ui_get_trash_area_index(config, conference) > 0) &&
      (active_area == ui_get_trash_area_index(config, conference))) {
    return "Trash";
  }

  return "(unknown)";
}

static void ui_clear_screen(struct aedoor_context *door)
{
  if (door != NULL) {
    aedoor_clear_screen(door);
  }
}

/* Selection and preview helpers */
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
  const char *separator;
  char line[UI_PREVIEW_WIDTH + 1];
  char output[UI_PREVIEW_WIDTH + 3];
  size_t length;
  int output_width;

  if (door == NULL) {
    return;
  }

  output_width = UI_PREVIEW_WIDTH + 2;
  cursor = text != NULL ? text : "";
  for (line_count = 0; line_count < UI_PREVIEW_LINES; line_count++) {
    if (*cursor == '\0') {
      if (line_count == 0) {
        aedoor_write_line(door, "  (no preview text)");
      }
      return;
    }

    separator = strchr(cursor, '\n');
    if (separator == NULL) {
      length = strlen(cursor);
    } else {
      length = (size_t) (separator - cursor);
    }

    if (length > UI_PREVIEW_WIDTH) {
      length = UI_PREVIEW_WIDTH;
    }

    memset(output, ' ', (size_t) output_width);
    output[output_width] = '\0';
    output[0] = ' ';
    output[1] = ' ';
    memcpy(line, cursor, length);
    line[length] = '\0';
    if (line[0] != '\0') {
      memcpy(output + 2, line, length);
    }
    aedoor_write_line(door, output);

    if (separator == NULL) {
      return;
    }
    cursor = separator + 1;
  }
}

static void ui_write_cut_text_line(struct aedoor_context *door, const char *text, int max_width)
{
  char line[UI_MODAL_DESCRIPTION_WIDTH + 1];
  size_t length;

  if ((door == NULL) || (max_width <= 0)) {
    return;
  }

  if ((text == NULL) || (*text == '\0')) {
    aedoor_write_line(door, " ");
    return;
  }

  length = strlen(text);
  if ((int) length > max_width) {
    length = (size_t) max_width;
  }

  memcpy(line, text, length);
  line[length] = '\0';
  aedoor_write_line(door, line[0] != '\0' ? line : " ");
}

static void ui_write_description_text(struct aedoor_context *door, const char *text)
{
  const char *cursor;
  const char *separator;
  const char *line_cursor;
  char output[UI_MODAL_DESCRIPTION_WIDTH + 2];
  size_t line_length;
  size_t chunk_length;

  if (door == NULL) {
    return;
  }

  cursor = text != NULL ? text : "";
  if (*cursor == '\0') {
    aedoor_write_line(door, " (no description text)");
    return;
  }

  while (*cursor != '\0') {
    separator = strchr(cursor, '\n');
    if (separator == NULL) {
      line_length = strlen(cursor);
    } else {
      line_length = (size_t) (separator - cursor);
    }

    if (line_length == 0U) {
      aedoor_write_line(door, " ");
    } else {
      line_cursor = cursor;
      while (line_length > 0U) {
        chunk_length = line_length;
        if (chunk_length > UI_MODAL_DESCRIPTION_WIDTH) {
          chunk_length = UI_MODAL_DESCRIPTION_WIDTH;
        }

        output[0] = ' ';
        memcpy(output + 1, line_cursor, chunk_length);
        output[chunk_length + 1U] = '\0';
        aedoor_write_line(door, output);

        line_cursor += chunk_length;
        line_length -= chunk_length;
      }
    }

    if (separator == NULL) {
      break;
    }
    cursor = separator + 1;
  }
}

static void ui_show_help_screen(struct aedoor_context *door,
                                const struct door_config *config,
                                int ui_mode)
{
  long key_value;
  static const char *source_menu_tokens[][3] = {
    {"(", "Q", ")"}, {"(", "R", ")"}, {"[", "A/Z", "]"}, {"(", "S", ")"},
    {"(", "D", ")"}, {"(", "G", ")"}, {"(", "W", ")"}, {"(", "E", ")"}, {"(", "C", ")"},
    {"", "[/]", ""}, {"", "{/}", ""}, {"(", "T", ")"},
    {"(", "V", ")"}, {"(", "?", ")"}
  };
  static const char *source_menu_tokens_no_trash[][3] = {
    {"(", "Q", ")"}, {"(", "R", ")"}, {"[", "A/Z", "]"}, {"(", "S", ")"},
    {"(", "D", ")"}, {"(", "G", ")"}, {"(", "W", ")"}, {"(", "E", ")"}, {"(", "C", ")"},
    {"", "[/]", ""}, {"", "{/}", ""}, {"(", "V", ")"},
    {"(", "?", ")"}
  };
  static const char *destination_menu_tokens[][3] = {
    {"(", "Q", ")"}, {"(", "R", ")"}, {"(", "S", ")"}, {"(", "M", ")"},
    {"", "[/]", ""}, {"", "{/}", ""}, {"", "(-)/(=)", ""}, {"(", "V", ")"},
    {"(", "?", ")"}
  };

  if (door == NULL) {
    return;
  }

  ui_clear_screen(door);
  if (ui_colour_enabled(door)) {
    char title_line[320];
    snprintf(title_line,
             sizeof(title_line),
             "%sARBFILES Help%s  %s(c) 2026 Intangybles%s",
             UI_COLOUR_CYAN,
             UI_COLOUR_RESET,
             UI_COLOUR_MAGENTA,
             UI_COLOUR_RESET);
    aedoor_write_line(door, title_line);
  } else {
    aedoor_write_line(door, "ARBFILES Help  (c) 2026 Intangybles");
  }
  aedoor_write_line(door, "");
  ui_write_green_line(door, "Navigation:");
  ui_write_key_help_line(door, '\0', "(,)/(.)", '\0', "previous or next conference");
  ui_write_key_help_line(door, '\0', "[/]", '\0', "next or previous listing area");
  ui_write_key_help_line(door, '\0', "{/}", '\0', "next or previous loaded file block");
  aedoor_write_line(door, "");
  if (ui_mode == UI_MODE_SOURCE) {
    char line[320];

    ui_write_heading_line(door, "Source & Destination keys:");
    ui_write_key_help_line(door, '[', "A/Z", ']', "move selected file");
    ui_write_key_help_line(door, '\0', "[/]", '\0', "change listing area");
    ui_write_key_help_line(door, '\0', "{/}", '\0', "change loaded file block");
    snprintf(line,
             sizeof(line),
             "  %s(%s-%s/%s=%s)%s %schange destination storage folder%s %s(Destination Only)%s",
             ui_colour(door, UI_COLOUR_GREEN),
             ui_colour(door, UI_COLOUR_YELLOW),
             ui_colour(door, UI_COLOUR_GREEN),
             ui_colour(door, UI_COLOUR_YELLOW),
             ui_colour(door, UI_COLOUR_GREEN),
             ui_reset(door),
             ui_colour(door, UI_COLOUR_GREEN),
             ui_reset(door),
             ui_colour(door, UI_COLOUR_CYAN),
             ui_reset(door));
    aedoor_write_line(door, line);
    ui_write_key_help_line(door, '(', "G", ')', "tag or untag selected file");
    ui_write_key_help_line(door, '(', "W", ')', "tag all files in this loaded block");
    ui_write_key_help_line(door, '(', "E", ')', "tag all files in this DIR");
    ui_write_key_help_line(door, '(', "C", ')', "clear all current tags");
    ui_write_key_help_line(door, '(', "V", ')', "view full selected description");
    ui_write_key_help_line(door, '(', "S", ')', "select destination");
    ui_write_key_help_line(door, '(', "D", ')', "delete or trash selected file");
    if (ui_has_trash_area(config)) {
      ui_write_key_help_line(door, '(', "T", ')', "jump to or from trash view");
    }
    ui_write_menu_line(door,
                       "Menu",
                       ui_has_trash_area(config) ? source_menu_tokens : source_menu_tokens_no_trash,
                       ui_has_trash_area(config) ? 14 : 13);
  } else {
    ui_write_heading_line(door, "Destination mode:");
    ui_write_key_help_line(door, '\0', "[/]", '\0', "change destination listing area");
    ui_write_key_help_line(door, '\0', "{/}", '\0', "change destination file block");
    ui_write_key_help_line(door, '\0', "(-)/(=)", '\0', "choose destination storage folder");
    ui_write_key_help_line(door, '(', "V", ')', "view full selected source description");
    ui_write_key_help_line(door, '(', "M", ')', "move selected source file here");
    ui_write_key_help_line(door, '(', "S", ')', "return to source browse");
    ui_write_menu_line(door, "Menu", destination_menu_tokens, 8);
  }
  aedoor_write_line(door, "");
  ui_write_heading_line(door, "Notes:");
  ui_write_green_line(door, "  Area is the DIR listing area.");
  ui_write_green_line(door, "  Store is the real file storage folder.");
  ui_write_green_line(door, "  'shared folder' means one folder is shared across listing areas.");
  aedoor_write_line(door, "");
  ui_write_green_line(door, "Press any key to return.");
  ui_wait_for_key(door, &key_value);
}

static void ui_show_full_view(struct aedoor_context *door,
                              const struct dirlist_data *source_dirlist,
                              int selected_entry)
{
  long key_value;
  const struct dirlist_entry *entry;
  const char *description_text;
  char line[320];

  if ((door == NULL) || (source_dirlist == NULL) || (source_dirlist->entry_count <= 0)) {
    return;
  }

  selected_entry = ui_get_selected_index(source_dirlist, selected_entry);
  entry = &source_dirlist->entries[selected_entry];

  ui_clear_screen(door);
  ui_write_heading_line(door, "Full Description");
  aedoor_write_line(door, "");
  snprintf(line,
           sizeof(line),
           "%d/%d: %s",
           selected_entry + 1,
           source_dirlist->entry_count,
           entry->filename[0] != '\0' ? entry->filename : "(unnamed)");
  ui_write_label_value_line(door, "File:", line);
  aedoor_write_line(door, "");

  description_text = entry->description[0] != '\0' ? entry->description : entry->header_line;
  ui_write_description_text(door, description_text);

  aedoor_write_line(door, "");
  ui_write_green_line(door, "Press any key to return.");
  ui_wait_for_key(door, &key_value);
}

static int ui_confirm_move(struct aedoor_context *door,
                           const struct door_config *config,
                           const struct ae_current_conference_info *source_conference,
                           const struct dirlist_data *source_dirlist,
                           int source_tagged_total,
                           const struct ae_current_conference_info *destination_conference,
                           const struct dirlist_data *destination_dirlist,
                           int source_area,
                           int selected_entry,
                           int destination_area,
                           int destination_folder_index)
{
  long key_value;
  const struct dirlist_entry *source_selected;
  const char *source_store_text;
  const char *destination_store_text;
  char line[320];
  int source_trash_area;
  static const char *move_tokens[][3] = {{"(", "Y", ")"}, {"(", "N", ")"}}; 
  static const char *move_descriptions[] = {"confirm move", "cancel"};

  if ((door == NULL) || (source_dirlist == NULL) || (source_dirlist->entry_count <= 0)) {
    return 0;
  }

  selected_entry = ui_get_selected_index(source_dirlist, selected_entry);
  source_selected = &source_dirlist->entries[selected_entry];
  source_store_text = ui_get_source_store_text(config, source_conference, source_dirlist, source_area);
  destination_store_text = ui_get_destination_store_text(config,
                                                         destination_conference,
                                                         destination_dirlist,
                                                         destination_area,
                                                         destination_folder_index);
  source_trash_area = (source_area == ui_get_trash_area_index(config, source_conference));
  ui_clear_screen(door);
  ui_write_heading_line(door, source_tagged_total > 0 ? "Confirm Batch Move" : "Confirm Move");
  aedoor_write_line(door, "");
  if (source_tagged_total > 0) {
    snprintf(line, sizeof(line), "%d tagged file(s)", source_tagged_total);
    ui_write_label_value_line(door, "Files:", line);
  } else {
    ui_write_label_value_line(door,
                              "File:",
                              source_selected->filename[0] != '\0' ? source_selected->filename : "(unnamed)");
  }
  aedoor_write_line(door, "");

  ui_write_heading_line(door, "From:");
  if (source_trash_area) {
    ui_write_label_value_line(door, "  Source:", "Trash");
  } else {
    snprintf(line,
             sizeof(line),
             "%d: %s",
             source_conference != NULL ? source_conference->base.number : 0,
             (source_conference != NULL) && (source_conference->base.name[0] != '\0')
               ? source_conference->base.name
               : "(unknown)");
    ui_write_label_value_line(door, "  Conf:", line);
    ui_write_label_value_line(door, "  Area:", ui_get_area_label(config, source_conference, source_area));
  }
  ui_write_label_value_line(door, "  Store:", source_store_text != NULL ? source_store_text : "(unknown)");
  aedoor_write_line(door, "");

  ui_write_heading_line(door, "To:");
  snprintf(line,
           sizeof(line),
           "%d: %s",
           destination_conference != NULL ? destination_conference->base.number : 0,
           (destination_conference != NULL) && (destination_conference->base.name[0] != '\0')
             ? destination_conference->base.name
             : "(unknown)");
  ui_write_label_value_line(door, "  Conf:", line);
  ui_write_label_value_line(door, "  Area:", ui_get_area_label(config, destination_conference, destination_area));
  ui_write_label_value_line(door, "  Store:", destination_store_text != NULL ? destination_store_text : "(unknown)");
  aedoor_write_line(door, "");
  ui_write_action_choices(door, move_tokens, move_descriptions, 2);

  for (;;) {
    if (ui_wait_for_key(door, &key_value) != 0) {
      return -1;
    }

    if ((key_value == 'Y') || (key_value == 'y')) {
      return 1;
    }
    if ((key_value == 'N') || (key_value == 'n') || (key_value == 27)) {
      return 0;
    }
  }
}

static int ui_confirm_delete(struct aedoor_context *door,
                             const struct door_config *config,
                             const struct ae_current_conference_info *source_conference,
                             const struct dirlist_data *source_dirlist,
                             int source_area,
                             int selected_entry)
{
  long key_value;
  const struct dirlist_entry *source_selected;
  const char *source_store_text;
  char line[320];
  int trash_enabled;
  static const char *delete_tokens[][3] = {{"(", "T", ")"}, {"(", "D", ")"}, {"(", "N", ")"}};
  static const char *delete_descriptions[] = {"move to trash", "delete permanently", "cancel"};
  static const char *confirm_delete_tokens[][3] = {{"(", "Y", ")"}, {"(", "N", ")"}};
  static const char *confirm_delete_descriptions[] = {"confirm delete", "cancel"};

  if ((door == NULL) || (source_dirlist == NULL) || (source_dirlist->entry_count <= 0)) {
    return UI_DELETE_NONE;
  }

  selected_entry = ui_get_selected_index(source_dirlist, selected_entry);
  source_selected = &source_dirlist->entries[selected_entry];
  source_store_text = ui_get_source_store_text(config, source_conference, source_dirlist, source_area);
  trash_enabled = (config != NULL) &&
                  (config->trash_path[0] != '\0') &&
                  (source_area != ui_get_trash_area_index(config, source_conference));

  ui_clear_screen(door);
  ui_write_heading_line(door, "Confirm Delete");
  aedoor_write_line(door, "");
  ui_write_label_value_line(door,
                            "File:",
                            source_selected->filename[0] != '\0' ? source_selected->filename : "(unnamed)");
  aedoor_write_line(door, "");
  snprintf(line,
           sizeof(line),
           "%d: %s",
           source_conference != NULL ? source_conference->base.number : 0,
           (source_conference != NULL) && (source_conference->base.name[0] != '\0')
             ? source_conference->base.name
             : "(unknown)");
  ui_write_label_value_line(door, "Conf:", line);
  ui_write_label_value_line(door, "Area:", ui_get_area_label(config, source_conference, source_area));
  ui_write_label_value_line(door, "Store:", source_store_text != NULL ? source_store_text : "(unknown)");
  aedoor_write_line(door, "");

  if (trash_enabled) {
    ui_write_label_value_line(door, "Trash:", config->trash_path);
    aedoor_write_line(door, "");
    ui_write_action_choices(door, delete_tokens, delete_descriptions, 3);
  } else {
    ui_write_green_line(door, "Permanent delete only.");
    aedoor_write_line(door, "");
    ui_write_action_choices(door, confirm_delete_tokens, confirm_delete_descriptions, 2);
  }

  for (;;) {
    if (ui_wait_for_key(door, &key_value) != 0) {
      return -1;
    }

    if (trash_enabled && ((key_value == 'T') || (key_value == 't'))) {
      return UI_DELETE_TRASH;
    }
    if (trash_enabled && ((key_value == 'D') || (key_value == 'd'))) {
      return UI_DELETE_PERMANENT;
    }
    if (!trash_enabled && ((key_value == 'Y') || (key_value == 'y'))) {
      return UI_DELETE_PERMANENT;
    }
    if ((key_value == 'N') || (key_value == 'n') || (key_value == 27)) {
      return UI_DELETE_NONE;
    }
  }
}

/* Store and label helpers */
static const char *ui_get_source_store_text(const struct door_config *config,
                                            const struct ae_current_conference_info *conference,
                                            const struct dirlist_data *dirlist,
                                            int active_area)
{
  static char text[64];
  int preferred_folder_index;

  if (conference == NULL) {
    return "(unknown)";
  }

  if ((active_area >= 1) && (active_area <= conference->base.dir_count)) {
    if ((dirlist != NULL) && (dirlist->source_path[0] != '\0')) {
      return dirlist->source_path;
    }

    if ((conference->base.download_path_count == 1) &&
        (conference->download_paths[0][0] != '\0')) {
      return conference->download_paths[0];
    }
    if (ui_uses_rotated_area_folder_map(conference)) {
      preferred_folder_index = ui_get_preferred_folder_for_area(conference, active_area);
      if ((preferred_folder_index >= 1) &&
          (preferred_folder_index <= conference->base.download_path_count) &&
          (conference->download_paths[preferred_folder_index - 1][0] != '\0')) {
        return conference->download_paths[preferred_folder_index - 1];
      }
    }
    if (ui_has_direct_area_folder_map(conference) &&
        (conference->download_paths[active_area - 1][0] != '\0')) {
      return conference->download_paths[active_area - 1];
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

  if ((ui_get_trash_area_index(config, conference) > 0) &&
      (active_area == ui_get_trash_area_index(config, conference)) &&
      (config != NULL) &&
      (config->trash_path[0] != '\0')) {
    return config->trash_path;
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
  int preferred_folder_index;

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
      return folder_path;
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

    preferred_folder_index = ui_get_preferred_folder_for_area(conference, active_area);
    if ((preferred_folder_index >= 1) &&
        (preferred_folder_index <= conference->base.download_path_count) &&
        (conference->download_paths[preferred_folder_index - 1][0] != '\0')) {
      snprintf(text,
               sizeof(text),
               "%d/%d %s",
               preferred_folder_index,
               conference->base.download_path_count,
               conference->download_paths[preferred_folder_index - 1]);
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
  const char *conference_name;
  int normal_area_count;
  int trash_area_index;

  normal_area_count = ui_get_normal_area_count(conference);
  trash_area_index = ui_get_trash_area_index(config, conference);
  conference_name = (conference != NULL) && (conference->base.name[0] != '\0')
                      ? conference->base.name
                      : (door->conf_name[0] != '\0' ? door->conf_name : "(unknown)");

  snprintf(line,
           sizeof(line),
           "%sARBFILES%s %s%s%s  %s(c) 2026 Intangybles%s  %sUser%s:%s %s%s%s",
           ui_colour(door, UI_COLOUR_MAGENTA),
           ui_reset(door),
           ui_colour(door, UI_COLOUR_YELLOW),
           ARBFILES_VERSION,
           ui_reset(door),
           ui_colour(door, UI_COLOUR_GREEN),
           ui_reset(door),
           ui_colour(door, UI_COLOUR_CYAN),
           ui_colour(door, UI_COLOUR_YELLOW),
           ui_reset(door),
           ui_colour(door, UI_COLOUR_WHITE),
           door->username,
           ui_reset(door));
  aedoor_write_line(door, line);

  if ((trash_area_index > 0) && (active_area == trash_area_index)) {
    snprintf(line,
             sizeof(line),
             "%sSource%s:%s %sTrash%s",
             ui_colour(door, UI_COLOUR_CYAN),
             ui_colour(door, UI_COLOUR_YELLOW),
             ui_reset(door),
             ui_colour(door, UI_COLOUR_WHITE),
             ui_reset(door));
    aedoor_write_line(door, line);
    snprintf(line,
             sizeof(line),
             "%sBrowse%s:%s %sTrash view%s  %sArea%s:%s %sTrash%s",
             ui_colour(door, UI_COLOUR_CYAN),
             ui_colour(door, UI_COLOUR_YELLOW),
             ui_reset(door),
             ui_colour(door, UI_COLOUR_WHITE),
             ui_reset(door),
             ui_colour(door, UI_COLOUR_CYAN),
             ui_colour(door, UI_COLOUR_YELLOW),
             ui_reset(door),
             ui_colour(door, UI_COLOUR_WHITE),
             ui_reset(door));
    aedoor_write_line(door, line);
    ui_write_label_value_line(door, "Store:", store_text != NULL ? store_text : "(unknown)");
    return;
  }

  snprintf(line,
           sizeof(line),
           "%s%s%s  %sConf%s:%s %s%d%s%s %s%s%s",
           ui_colour(door, UI_COLOUR_CYAN),
           ui_mode == UI_MODE_DESTINATION ? "Destination" : "Source",
           ui_reset(door),
           ui_colour(door, UI_COLOUR_CYAN),
           ui_colour(door, UI_COLOUR_YELLOW),
           ui_reset(door),
           ui_colour(door, UI_COLOUR_YELLOW),
           conference != NULL ? conference->base.number : (door->current_conf + 1),
           ui_reset(door),
           ((conference != NULL) && (conference->base.number == (door->current_conf + 1))) ? " current" : "",
           ui_colour(door, UI_COLOUR_WHITE),
           conference_name,
           ui_reset(door));
  aedoor_write_line(door, line);
  snprintf(line,
           sizeof(line),
           "%sBrowse%s:%s %s%d/%d%s  %sArea%s:%s %s%d/%d%s %s%s%s",
           ui_colour(door, UI_COLOUR_CYAN),
           ui_colour(door, UI_COLOUR_YELLOW),
           ui_reset(door),
           ui_colour(door, UI_COLOUR_WHITE),
           (conference_index >= 0 ? conference_index + 1 : 1),
           system_config != NULL && system_config->discovered_count > 0 ? system_config->discovered_count : 1,
           ui_reset(door),
           ui_colour(door, UI_COLOUR_CYAN),
           ui_colour(door, UI_COLOUR_YELLOW),
           ui_reset(door),
           ui_colour(door, UI_COLOUR_WHITE),
           active_area,
           normal_area_count > 0 ? normal_area_count : 1,
           ui_reset(door),
           ui_colour(door, UI_COLOUR_WHITE),
           ui_get_area_label(config, conference, active_area),
           ui_reset(door));
  aedoor_write_line(door, line);
  ui_write_label_value_line(door, "Store:", store_text != NULL ? store_text : "(unknown)");
}

static void ui_write_block_line(struct aedoor_context *door, const struct dirlist_data *dirlist)
{
  char line[160];
  long first_entry;
  long last_entry;

  if ((door == NULL) || (dirlist == NULL)) {
    return;
  }

  if (!(dirlist->has_previous_window || dirlist->has_next_window || (dirlist->window_start_entry > 0L))) {
    return;
  }

  if (dirlist->entry_count <= 0) {
    ui_write_label_value_line(door, "Block:", "(empty)");
    return;
  }

  first_entry = dirlist->window_start_entry + 1L;
  last_entry = dirlist->window_start_entry + dirlist->entry_count;
  if (dirlist->has_next_window && (dirlist->total_entries_seen <= last_entry)) {
    snprintf(line, sizeof(line), "%ld-%ld of more than %ld", first_entry, last_entry, last_entry);
  } else {
    snprintf(line, sizeof(line), "%ld-%ld of %ld", first_entry, last_entry, dirlist->total_entries_seen);
  }
  ui_write_label_value_line(door, "Block:", line);
}

static void ui_write_source_screen(struct aedoor_context *door,
                                   const struct door_config *config,
                                   const struct ae_system_config *system_config,
                                   const struct ae_current_conference_info *conference,
                                   const struct dirlist_data *dirlist,
                                   const char *status_message,
                                   int source_tagged_total,
                                   int conference_index,
                                   int active_area,
                                   int selected_entry)
{
  char line[320];
  int displayed_entries;
  int selected_index;
  int visible_start;
  int index;
  static const char *conf_tokens[][3] = {{"(", ",", ")"}, {"(", ".", ")"}}; 
  static const char *source_menu_tokens[][3] = {
    {"(", "Q", ")"}, {"(", "R", ")"}, {"[", "A/Z", "]"}, {"(", "S", ")"},
    {"(", "D", ")"}, {"(", "G", ")"}, {"(", "W", ")"}, {"(", "E", ")"}, {"(", "C", ")"},
    {"", "[/]", ""}, {"", "{/}", ""}, {"(", "H", ")"}, {"(", "T", ")"},
    {"(", "V", ")"}, {"(", "?", ")"}
  };
  static const char *source_menu_tokens_no_trash[][3] = {
    {"(", "Q", ")"}, {"(", "R", ")"}, {"[", "A/Z", "]"}, {"(", "S", ")"},
    {"(", "D", ")"}, {"(", "G", ")"}, {"(", "W", ")"}, {"(", "E", ")"}, {"(", "C", ")"},
    {"", "[/]", ""}, {"", "{/}", ""}, {"(", "H", ")"}, {"(", "V", ")"},
    {"(", "?", ")"}
  };

  ui_write_common_header(door,
                         config,
                         system_config,
                         conference,
                         conference_index,
                         active_area,
                         UI_MODE_SOURCE,
                         ui_get_source_store_text(config, conference, dirlist, active_area));

  if ((dirlist != NULL) &&
      ((dirlist->window_start_entry > 0) || dirlist->has_previous_window || dirlist->has_next_window)) {
    long total_count;

    total_count = dirlist->total_entries_seen > 0 ? dirlist->total_entries_seen
                                                  : (dirlist->window_start_entry + dirlist->entry_count);
    snprintf(line,
             sizeof(line),
             "%sLoaded%s:%s %s%d%s  %sTotal%s:%s %s%ld%s  %sDirs%s:%s %s%d%s",
             ui_colour(door, UI_COLOUR_CYAN),
             ui_colour(door, UI_COLOUR_YELLOW),
             ui_reset(door),
             ui_colour(door, UI_COLOUR_WHITE),
             dirlist->entry_count,
             ui_reset(door),
             ui_colour(door, UI_COLOUR_CYAN),
             ui_colour(door, UI_COLOUR_YELLOW),
             ui_reset(door),
             ui_colour(door, UI_COLOUR_WHITE),
             total_count,
             ui_reset(door),
             ui_colour(door, UI_COLOUR_CYAN),
             ui_colour(door, UI_COLOUR_YELLOW),
             ui_reset(door),
             ui_colour(door, UI_COLOUR_WHITE),
             conference != NULL ? conference->base.dir_count : 0,
             ui_reset(door));
  } else {
    snprintf(line,
             sizeof(line),
             "%sFiles%s:%s %s%d%s  %sDirs%s:%s %s%d%s  %sStatus%s:%s %s%s%s",
             ui_colour(door, UI_COLOUR_CYAN),
             ui_colour(door, UI_COLOUR_YELLOW),
             ui_reset(door),
             ui_colour(door, UI_COLOUR_WHITE),
             dirlist != NULL ? dirlist->entry_count : 0,
             ui_reset(door),
             ui_colour(door, UI_COLOUR_CYAN),
             ui_colour(door, UI_COLOUR_YELLOW),
             ui_reset(door),
             ui_colour(door, UI_COLOUR_WHITE),
             conference != NULL ? conference->base.dir_count : 0,
             ui_reset(door),
             ui_colour(door, UI_COLOUR_CYAN),
             ui_colour(door, UI_COLOUR_YELLOW),
             ui_reset(door),
             ui_colour(door, UI_COLOUR_WHITE),
             (dirlist != NULL) && (dirlist->status_text[0] != '\0') ? dirlist->status_text : "(unknown)",
             ui_reset(door));
  }
  aedoor_write_line(door, line);
  ui_write_block_line(door, dirlist);
  if (source_tagged_total > 0) {
    snprintf(line,
             sizeof(line),
             "%sTagged%s:%s %s%d%s",
             ui_colour(door, UI_COLOUR_CYAN),
             ui_colour(door, UI_COLOUR_YELLOW),
             ui_reset(door),
             ui_colour(door, UI_COLOUR_WHITE),
             source_tagged_total,
             ui_reset(door));
    aedoor_write_line(door, line);
  }
  ui_write_heading_line(door, "Source files:");

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
               " %c%c %s",
               entry_index == selected_index ? '>' : ' ',
               dirlist->entries[entry_index].tagged ? '*' : ' ',
               dirlist->entries[entry_index].header_line);
      if ((int) strlen(line) > UI_LIST_WIDTH) {
        line[UI_LIST_WIDTH] = '\0';
      }
      aedoor_write_line(door, line);
    }

    if ((dirlist->window_start_entry > 0) || dirlist->has_previous_window || dirlist->has_next_window) {
      long absolute_index;
      long total_count;

      absolute_index = dirlist->window_start_entry + selected_index + 1;
      total_count = dirlist->total_entries_seen > 0 ? dirlist->total_entries_seen
                                                    : (dirlist->window_start_entry + dirlist->entry_count);
      snprintf(line,
               sizeof(line),
               "%sSelected%s %ld/%ld: %s",
               ui_colour(door, UI_COLOUR_CYAN),
               ui_reset(door),
               absolute_index,
               total_count,
               dirlist->entries[selected_index].filename[0] != '\0'
                 ? dirlist->entries[selected_index].filename
                 : "(unnamed)");
    } else {
      snprintf(line,
               sizeof(line),
               "%sSelected%s %d/%d: %s",
               ui_colour(door, UI_COLOUR_CYAN),
               ui_reset(door),
               selected_index + 1,
               dirlist->entry_count,
               dirlist->entries[selected_index].filename[0] != '\0'
                 ? dirlist->entries[selected_index].filename
                 : "(unnamed)");
    }
    aedoor_write_line(door, line);
    ui_write_heading_line(door, "Preview:");
    ui_write_preview_lines(door, ui_get_preview_text(dirlist, selected_index));
  }

  if ((system_config != NULL) && (system_config->discovered_count > 1)) {
    ui_write_menu_line(door, "Confs", conf_tokens, 2);
  }
  if ((status_message != NULL) && (status_message[0] != '\0')) {
    snprintf(line,
             sizeof(line),
             "%sMessage%s:%s %s%s%s",
             ui_colour(door, UI_COLOUR_CYAN),
             ui_colour(door, UI_COLOUR_YELLOW),
             ui_reset(door),
             status_message[0] == 'D' || status_message[0] == 'F'
               ? ui_colour(door, UI_COLOUR_RED)
               : ui_colour(door, UI_COLOUR_WHITE),
             status_message,
             ui_reset(door));
    aedoor_write_line(door, line);
  }
  ui_write_menu_line(door,
                     "Menu",
                     ui_has_trash_area(config) ? source_menu_tokens : source_menu_tokens_no_trash,
                     ui_has_trash_area(config) ? 15 : 14);
}

static void ui_write_destination_screen(struct aedoor_context *door,
                                        const struct door_config *config,
                                        const struct ae_system_config *system_config,
                                        const struct ae_current_conference_info *source_conference,
                                        const struct dirlist_data *source_dirlist,
                                        int source_tagged_total,
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
  static const char *conf_tokens[][3] = {{"(", ",", ")"}, {"(", ".", ")"}}; 
  static const char *destination_menu_tokens[][3] = {
    {"(", "Q", ")"}, {"(", "R", ")"}, {"(", "S", ")"}, {"(", "M", ")"},
    {"", "[/]", ""}, {"", "-/=", ""}, {"(", "H", ")"}, {"(", "V", ")"},
    {"(", "?", ")"}
  };

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
           "%s",
           (source_selected != NULL) && (source_selected->filename[0] != '\0')
             ? source_selected->filename
             : "(no source file selected)");
  ui_write_label_value_line(door, "Target:", line);
  if (source_tagged_total > 0) {
    snprintf(line,
             sizeof(line),
             "From Conf %d Area %d  Files: %d  Tagged: %d",
             source_conference != NULL ? source_conference->base.number : (source_conference_index + 1),
             source_area,
             source_dirlist != NULL ? source_dirlist->entry_count : 0,
             source_tagged_total);
  } else {
    snprintf(line,
             sizeof(line),
             "From Conf %d Area %d  Files: %d",
             source_conference != NULL ? source_conference->base.number : (source_conference_index + 1),
             source_area,
             source_dirlist != NULL ? source_dirlist->entry_count : 0);
  }
  ui_write_label_value_line(door, "From:", line);
  ui_write_block_line(door, destination_dirlist);
  ui_write_heading_line(door, "Destination files:");

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
      if ((int) strlen(line) > UI_LIST_WIDTH) {
        line[UI_LIST_WIDTH] = '\0';
      }
      aedoor_write_line(door, line);
    }

    if (destination_dirlist->entry_count > displayed_entries) {
      snprintf(line, sizeof(line), "  ... %d more", destination_dirlist->entry_count - displayed_entries);
      aedoor_write_line(door, line);
    }
  }

  if ((system_config != NULL) && (system_config->discovered_count > 1)) {
    ui_write_menu_line(door, "Confs", conf_tokens, 2);
  }
  if ((status_message != NULL) && (status_message[0] != '\0')) {
    snprintf(line,
             sizeof(line),
             "%sMessage%s:%s %s%s%s",
             ui_colour(door, UI_COLOUR_CYAN),
             ui_colour(door, UI_COLOUR_YELLOW),
             ui_reset(door),
             status_message[0] == 'D' || status_message[0] == 'F'
               ? ui_colour(door, UI_COLOUR_RED)
               : ui_colour(door, UI_COLOUR_WHITE),
             status_message,
             ui_reset(door));
    aedoor_write_line(door, line);
  }
  ui_write_menu_line(door, "Menu", destination_menu_tokens, 9);
}

/* Top-level redraw and event loop */
static void ui_draw_screen(struct aedoor_context *door,
                           const struct door_config *config,
                           const struct ae_system_config *system_config,
                           const struct ae_current_conference_info *source_conference,
                           const struct dirlist_data *source_dirlist,
                           int source_tagged_total,
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
                                source_tagged_total,
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
                           source_tagged_total,
                           active_conference_index,
                           active_area,
                           selected_entry);
  }
}

int ui_run(const struct door_config *config,
           struct aedoor_context *door,
           const struct ae_system_config *system_config,
           const struct ae_current_conference_info *source_conference,
           struct dirlist_data *source_dirlist,
           struct tagset_data *source_tagset,
           const struct ae_current_conference_info *destination_conference,
           const struct dirlist_data *destination_dirlist,
           const char *status_message,
           int source_tagged_total,
           int *ui_mode,
           int *active_conference_index,
           int *active_area,
           long *source_block_start_entry,
           int *selected_entry,
           int *destination_conference_index,
           int *destination_area,
           long *destination_block_start_entry,
           int *destination_folder_index,
           int *trash_reference_area,
           int *delete_mode,
           int list_block_size,
           struct doorlog *log,
           char *error_text,
           int error_text_size)
{
  const struct ae_current_conference_info *display_conference;
  int *display_conference_index;
  int *display_area;
  long key_value;
  int hold_area_index;
  int trash_area_index;
  int normal_area_count;
  int poll_status;

  if ((config == NULL) || (door == NULL) || (system_config == NULL) ||
      (ui_mode == NULL) ||
      (active_conference_index == NULL) || (active_area == NULL) || (source_block_start_entry == NULL) ||
      (selected_entry == NULL) ||
      (destination_conference_index == NULL) || (destination_area == NULL) || (destination_block_start_entry == NULL) ||
      (destination_folder_index == NULL) || (trash_reference_area == NULL) || (delete_mode == NULL)) {
    ui_set_error(error_text, error_text_size, "invalid UI request");
    return -1;
  }

  *delete_mode = UI_DELETE_NONE;
  display_conference = *ui_mode == UI_MODE_DESTINATION ? destination_conference : source_conference;
  display_conference_index = *ui_mode == UI_MODE_DESTINATION ? destination_conference_index : active_conference_index;
  display_area = *ui_mode == UI_MODE_DESTINATION ? destination_area : active_area;

  *selected_entry = ui_get_selected_index(source_dirlist, *selected_entry);
  normal_area_count = ui_get_normal_area_count(display_conference);
  hold_area_index = ui_get_hold_area_index(config, display_conference);
  trash_area_index = ui_get_trash_area_index(config, display_conference);

  ui_draw_screen(door,
                 config,
                 system_config,
                 source_conference,
                 source_dirlist,
                 source_tagged_total,
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
  doorlog_write(log, "UI scaffold shown to user.");

  for (;;) {
    poll_status = aedoor_poll_key(door, &key_value);
    if (poll_status < 0) {
      ui_set_error(error_text, error_text_size, "user input failed");
      doorlog_write(log, "UI input polling failed.");
      return -1;
    }

    if (poll_status == 0) {
      Delay(2);
      continue;
    }

    if ((key_value == 'Q') || (key_value == 'q')) {
      doorlog_write(log, "User left the scaffold UI.");
      if (error_text != NULL) {
        error_text[0] = '\0';
      }
      return UI_RESULT_EXIT;
    }

    if ((key_value == 'R') || (key_value == 'r')) {
      doorlog_write(log, "User requested UI redraw.");
      ui_draw_screen(door,
                     config,
                     system_config,
                     source_conference,
                     source_dirlist,
                     source_tagged_total,
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
      doorlog_write(log, "User opened the help screen.");
      ui_show_help_screen(door, config, *ui_mode);
      ui_draw_screen(door,
                     config,
                     system_config,
                     source_conference,
                     source_dirlist,
                     source_tagged_total,
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
      doorlog_write(log, "User opened full description view.");
      ui_show_full_view(door, source_dirlist, *selected_entry);
      ui_draw_screen(door,
                     config,
                     system_config,
                     source_conference,
                     source_dirlist,
                     source_tagged_total,
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
        ((key_value == 'S') || (key_value == 's'))) {
      *ui_mode = UI_MODE_DESTINATION;
      doorlog_write(log, "User switched to destination browse mode.");
      if (error_text != NULL) {
        error_text[0] = '\0';
      }
      return UI_RESULT_AREA_CHANGE;
    }

    if ((*ui_mode == UI_MODE_DESTINATION) && ((key_value == 'S') || (key_value == 's'))) {
      *ui_mode = UI_MODE_SOURCE;
      doorlog_write(log, "User returned to source browse mode.");
      if (error_text != NULL) {
        error_text[0] = '\0';
      }
      return UI_RESULT_AREA_CHANGE;
    }

    if ((*ui_mode == UI_MODE_SOURCE) && ((key_value == 'T') || (key_value == 't')) && (trash_area_index > 0)) {
      if (*display_area == trash_area_index) {
        *display_area = *trash_reference_area > 0 ? *trash_reference_area : 1;
        doorlog_write(log, "User returned from trash browse mode.");
      } else if ((*display_area >= 1) && (*display_area <= normal_area_count)) {
        *trash_reference_area = *display_area;
        *display_area = trash_area_index;
        doorlog_writef(log, "User entered trash browse mode for DIR%d", *trash_reference_area);
      }
      if (error_text != NULL) {
        error_text[0] = '\0';
      }
      return UI_RESULT_AREA_CHANGE;
    }

    if ((*ui_mode == UI_MODE_DESTINATION) && ((key_value == 'M') || (key_value == 'm'))) {
      int confirm_status;

      doorlog_write(log, "User requested move confirmation.");
      confirm_status = ui_confirm_move(door,
                                       config,
                                       source_conference,
                                       source_dirlist,
                                       source_tagged_total,
                                       destination_conference,
                                       destination_dirlist,
                                       *active_area,
                                       *selected_entry,
                                       *destination_area,
                                       *destination_folder_index);
      if (confirm_status < 0) {
        ui_set_error(error_text, error_text_size, "move confirmation input failed");
        return -1;
      }
      if (confirm_status == 0) {
        doorlog_write(log, "User cancelled move confirmation.");
        ui_draw_screen(door,
                       config,
                       system_config,
                       source_conference,
                       source_dirlist,
                       source_tagged_total,
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
      doorlog_write(log, "User confirmed move operation.");
      if (error_text != NULL) {
        error_text[0] = '\0';
      }
      return UI_RESULT_MOVE;
    }

    if ((*ui_mode == UI_MODE_SOURCE) &&
        ((key_value == 'D') || (key_value == 'd')) &&
        (source_dirlist != NULL) &&
        (source_dirlist->entry_count > 0)) {
      int confirm_delete_mode;

      doorlog_write(log, "User requested delete confirmation.");
      confirm_delete_mode = ui_confirm_delete(door,
                                              config,
                                              source_conference,
                                              source_dirlist,
                                              *active_area,
                                              *selected_entry);
      if (confirm_delete_mode < 0) {
        ui_set_error(error_text, error_text_size, "delete confirmation input failed");
        return -1;
      }
      if (confirm_delete_mode == UI_DELETE_NONE) {
        doorlog_write(log, "User cancelled delete confirmation.");
        ui_draw_screen(door,
                       config,
                       system_config,
                       source_conference,
                       source_dirlist,
                       source_tagged_total,
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
      *delete_mode = confirm_delete_mode;
      doorlog_writef(log, "User confirmed delete operation mode %d", confirm_delete_mode);
      if (error_text != NULL) {
        error_text[0] = '\0';
      }
      return UI_RESULT_DELETE;
    }

    if ((*ui_mode == UI_MODE_SOURCE) &&
        (((key_value == 'A') || (key_value == 'a')) &&
         (source_dirlist != NULL) && (source_dirlist->entry_count > 0))) {
      if (*selected_entry > 0) {
        (*selected_entry)--;
        doorlog_writef(log, "User moved selection to file %d", *selected_entry + 1);
      }
      ui_draw_screen(door,
                     config,
                     system_config,
                     source_conference,
                     source_dirlist,
                     source_tagged_total,
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
        (((key_value == 'Z') || (key_value == 'z')) &&
         (source_dirlist != NULL) && (source_dirlist->entry_count > 0))) {
      if (*selected_entry + 1 < source_dirlist->entry_count) {
        (*selected_entry)++;
        doorlog_writef(log, "User moved selection to file %d", *selected_entry + 1);
      }
      ui_draw_screen(door,
                     config,
                     system_config,
                     source_conference,
                     source_dirlist,
                     source_tagged_total,
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
        (((key_value == 'G') || (key_value == 'g')) &&
         (source_dirlist != NULL) && (source_dirlist->entry_count > 0))) {
      int tag_index;
      int tag_state;

      tag_index = ui_get_selected_index(source_dirlist, *selected_entry);
      if (source_tagset != NULL) {
        tag_state = tagset_toggle(source_tagset, source_dirlist->entries[tag_index].filename);
        if (tag_state < 0) {
          ui_set_error(error_text, error_text_size, "tag list could not grow");
          return -1;
        }
        tagset_apply_to_dirlist(source_tagset, source_dirlist);
        source_tagged_total = tagset_count(source_tagset);
      } else {
        tag_state = dirlist_toggle_tag(source_dirlist, tag_index);
        source_tagged_total = dirlist_count_tags(source_dirlist);
      }
      doorlog_writef(log, "User %s tag on file %d", tag_state ? "set" : "cleared", tag_index + 1);
      if (tag_state && (*selected_entry + 1 < source_dirlist->entry_count)) {
        (*selected_entry)++;
      }
      ui_draw_screen(door,
                     config,
                     system_config,
                     source_conference,
                     source_dirlist,
                     source_tagged_total,
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
        (((key_value == 'W') || (key_value == 'w')) &&
         (source_dirlist != NULL) && (source_dirlist->entry_count > 0))) {
      if (source_tagset != NULL) {
        if (tagset_add_all_from_dirlist(source_tagset, source_dirlist) < 0) {
          ui_set_error(error_text, error_text_size, "tag list could not grow");
          return -1;
        }
        tagset_apply_to_dirlist(source_tagset, source_dirlist);
        source_tagged_total = tagset_count(source_tagset);
      } else {
        dirlist_set_all_tags(source_dirlist, 1);
        source_tagged_total = dirlist_count_tags(source_dirlist);
      }
      doorlog_writef(log, "User tagged all %d files in the current block", source_dirlist->entry_count);
      ui_draw_screen(door,
                     config,
                     system_config,
                     source_conference,
                     source_dirlist,
                     source_tagged_total,
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
        (((key_value == 'E') || (key_value == 'e')) &&
         (source_dirlist != NULL) &&
         (source_dirlist->listing_path[0] != '\0'))) {
      if (source_tagset != NULL) {
        if (tagset_add_all_from_listing(source_tagset,
                                        source_dirlist->listing_path,
                                        error_text,
                                        error_text_size) < 0) {
          return -1;
        }
        tagset_apply_to_dirlist(source_tagset, source_dirlist);
        source_tagged_total = tagset_count(source_tagset);
      } else {
        dirlist_set_all_tags(source_dirlist, 1);
        source_tagged_total = dirlist_count_tags(source_dirlist);
      }
      doorlog_writef(log, "User tagged all files in DIR listing %s", source_dirlist->listing_path);
      ui_draw_screen(door,
                     config,
                     system_config,
                     source_conference,
                     source_dirlist,
                     source_tagged_total,
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
        (((key_value == 'C') || (key_value == 'c')) &&
         (source_dirlist != NULL) && (source_dirlist->entry_count > 0))) {
      if (source_tagset != NULL) {
        tagset_clear(source_tagset);
        tagset_apply_to_dirlist(source_tagset, source_dirlist);
        source_tagged_total = 0;
      } else {
        dirlist_clear_tags(source_dirlist);
        source_tagged_total = 0;
      }
      doorlog_write(log, "User cleared tags in the current block.");
      ui_draw_screen(door,
                     config,
                     system_config,
                     source_conference,
                     source_dirlist,
                     source_tagged_total,
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
        ((key_value == '=') || (key_value == '+')) &&
        (*destination_folder_index < display_conference->base.download_path_count)) {
      (*destination_folder_index)++;
      doorlog_writef(log, "User moved to destination folder %d", *destination_folder_index);
      ui_draw_screen(door,
                     config,
                     system_config,
                     source_conference,
                     source_dirlist,
                     source_tagged_total,
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
        ((key_value == '-') || (key_value == '_')) &&
        (*destination_folder_index > 1)) {
      (*destination_folder_index)--;
      doorlog_writef(log, "User moved to destination folder %d", *destination_folder_index);
      ui_draw_screen(door,
                     config,
                     system_config,
                     source_conference,
                     source_dirlist,
                     source_tagged_total,
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
      doorlog_writef(log, "User moved to conference slot %d", *display_conference_index + 1);
      if (error_text != NULL) {
        error_text[0] = '\0';
      }
      return UI_RESULT_CONFERENCE_CHANGE;
    }

    if ((key_value == ',') &&
        (system_config->discovered_count > 1) &&
        (*display_conference_index > 0)) {
      (*display_conference_index)--;
      doorlog_writef(log, "User moved to conference slot %d", *display_conference_index + 1);
      if (error_text != NULL) {
        error_text[0] = '\0';
      }
      return UI_RESULT_CONFERENCE_CHANGE;
    }

    if ((key_value == '}') &&
        (*ui_mode == UI_MODE_SOURCE) &&
        (source_dirlist != NULL) &&
        source_dirlist->has_next_window) {
      *source_block_start_entry = source_dirlist->window_start_entry + source_dirlist->entry_count;
      *selected_entry = 0;
      doorlog_writef(log, "User moved to source block starting at file %ld", *source_block_start_entry + 1L);
      if (error_text != NULL) {
        error_text[0] = '\0';
      }
      return UI_RESULT_BLOCK_CHANGE;
    }

    if ((key_value == '{') &&
        (*ui_mode == UI_MODE_SOURCE) &&
        (source_dirlist != NULL) &&
        source_dirlist->has_previous_window) {
      *source_block_start_entry -= list_block_size;
      if (*source_block_start_entry < 0L) {
        *source_block_start_entry = 0L;
      }
      *selected_entry = 0;
      doorlog_writef(log, "User moved to source block starting at file %ld", *source_block_start_entry + 1L);
      if (error_text != NULL) {
        error_text[0] = '\0';
      }
      return UI_RESULT_BLOCK_CHANGE;
    }

    if ((key_value == '}') &&
        (*ui_mode == UI_MODE_DESTINATION) &&
        (destination_dirlist != NULL) &&
        destination_dirlist->has_next_window) {
      *destination_block_start_entry = destination_dirlist->window_start_entry + destination_dirlist->entry_count;
      doorlog_writef(log, "User moved to destination block starting at file %ld", *destination_block_start_entry + 1L);
      if (error_text != NULL) {
        error_text[0] = '\0';
      }
      return UI_RESULT_BLOCK_CHANGE;
    }

    if ((key_value == '{') &&
        (*ui_mode == UI_MODE_DESTINATION) &&
        (destination_dirlist != NULL) &&
        destination_dirlist->has_previous_window) {
      *destination_block_start_entry -= list_block_size;
      if (*destination_block_start_entry < 0L) {
        *destination_block_start_entry = 0L;
      }
      doorlog_writef(log, "User moved to destination block starting at file %ld", *destination_block_start_entry + 1L);
      if (error_text != NULL) {
        error_text[0] = '\0';
      }
      return UI_RESULT_BLOCK_CHANGE;
    }

    if ((key_value == ']') &&
        (normal_area_count > 0) &&
        (*display_area >= 1) &&
        (*display_area < normal_area_count)) {
      (*display_area)++;
      doorlog_writef(log, "User moved to area %d", *display_area);
      if (error_text != NULL) {
        error_text[0] = '\0';
      }
      return UI_RESULT_AREA_CHANGE;
    }

    if ((key_value == '[') &&
        (*display_area == hold_area_index) &&
        (normal_area_count > 0)) {
      *display_area = normal_area_count;
      doorlog_writef(log, "User returned from hold area to area %d", *display_area);
      if (error_text != NULL) {
        error_text[0] = '\0';
      }
      return UI_RESULT_AREA_CHANGE;
    }

    if ((key_value == '[') &&
        (normal_area_count > 0) &&
        (*display_area > 1) &&
        (*display_area <= normal_area_count)) {
      (*display_area)--;
      doorlog_writef(log, "User moved to area %d", *display_area);
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
      doorlog_writef(log, "User switched to special area %d", *display_area);
      if (error_text != NULL) {
        error_text[0] = '\0';
      }
      return UI_RESULT_AREA_CHANGE;
    }
  }
}
