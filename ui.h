/*
 * Public interface for the arbfiles text UI.
 */
#ifndef UI_H
#define UI_H

struct aedoor_context;
struct ae_conference_info;
struct ae_current_conference_info;
struct ae_system_config;
struct door_config;
struct dirlist_data;
struct doorlog;

#define UI_RESULT_EXIT 0
#define UI_RESULT_AREA_CHANGE 1
#define UI_RESULT_CONFERENCE_CHANGE 2
#define UI_RESULT_MOVE 3

#define UI_MODE_SOURCE 0
#define UI_MODE_DESTINATION 1

/* Drive one UI interaction loop until the caller requests an action. */
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
           int error_text_size);

#endif
