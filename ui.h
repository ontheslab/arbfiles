/*
 * Text UI entry points for arbfiles.
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
struct tagset_data;

#define UI_RESULT_EXIT 0
#define UI_RESULT_AREA_CHANGE 1
#define UI_RESULT_CONFERENCE_CHANGE 2
#define UI_RESULT_MOVE 3
#define UI_RESULT_DELETE 4
#define UI_RESULT_BLOCK_CHANGE 5

#define UI_MODE_SOURCE 0
#define UI_MODE_DESTINATION 1

#define UI_DELETE_NONE 0
#define UI_DELETE_TRASH 1
#define UI_DELETE_PERMANENT 2

/* Small move-status screens around the live file operation. */
void ui_show_move_progress(struct aedoor_context *door,
                           const char *filename,
                           const char *source_store,
                           const char *destination_store);
void ui_show_batch_move_progress(struct aedoor_context *door,
                                 const char *filename,
                                 const char *source_store,
                                 const char *destination_store,
                                 int current_file,
                                 int total_files);
void ui_show_move_result(struct aedoor_context *door, int move_ok, const char *message);
void ui_show_delete_progress(struct aedoor_context *door,
                             int delete_mode,
                             const char *filename,
                             const char *source_store,
                             const char *target_store);
void ui_show_delete_result(struct aedoor_context *door, int delete_ok, const char *message);
int ui_confirm_orphan_delete(struct aedoor_context *door,
                             const char *filename,
                             const char *message);

/* Drive one UI interaction loop until the user requests an action. */
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
           int error_text_size);

#endif
