/*
 * High-level file and listing changes used by arbfiles.
 */
#ifndef FILE_OPS_H
#define FILE_OPS_H

struct dirlist_data;

/* Move one selected source entry into the chosen destination folder and DIR. */
int file_ops_move_selected(const struct dirlist_data *source_dirlist,
                           int selected_entry,
                           const char *source_folder,
                           const char *destination_folder,
                           const char *destination_listing_path,
                           char *error_text,
                           int error_text_size);

/* Delete one selected source entry, or move it to trash first. */
int file_ops_delete_selected(const struct dirlist_data *source_dirlist,
                             int selected_entry,
                             const char *source_folder,
                             const char *trash_folder,
                             int use_trash,
                             char *error_text,
                             int error_text_size);

/* Remove a listing entry only when the payload file is already missing. */
int file_ops_delete_orphan_entry(const struct dirlist_data *source_dirlist,
                                 int selected_entry,
                                 char *error_text,
                                 int error_text_size);

#endif
