/*
 * High-level file move operation used by the destination screen.
 */
#ifndef FILE_OPS_H
#define FILE_OPS_H

struct dirlist_data;

/* Move one selected source entry into the chosen destination folder/listing. */
int file_ops_move_selected(const struct dirlist_data *source_dirlist,
                           int selected_entry,
                           const char *source_folder,
                           const char *destination_folder,
                           const char *destination_listing_path,
                           char *error_text,
                           int error_text_size);

#endif
