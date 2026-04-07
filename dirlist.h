/*
 * Parsed Ami-Express DIR listing data.
 */
#ifndef DIRLIST_H
#define DIRLIST_H

#define DIRLIST_DEFAULT_BLOCK_SIZE 1024
#define DIRLIST_MIN_BLOCK_SIZE 128
#define DIRLIST_MAX_BLOCK_SIZE 4096
#define DIRLIST_GROW_CHUNK 64

/* One file entry plus its captured description text. */
struct dirlist_entry {
  int has_listing_entry;
  int tagged;
  int description_line_count;
  char filename[32];
  char header_line[256];
  char description[1024];
};

/* Parsed listing plus the small bits of UI state tied to it. */
struct dirlist_data {
  int entry_count;
  int entry_capacity;
  long window_start_entry;
  int window_entry_limit;
  long total_entries_seen;
  int has_previous_window;
  int has_next_window;
  char listing_path[256];
  char source_path[256];
  char status_text[80];
  struct dirlist_entry *entries;
};

/* Listing load and status helpers. */
void dirlist_init(struct dirlist_data *dirlist);
void dirlist_free(struct dirlist_data *dirlist);
void dirlist_set_error(char *error_text, int error_text_size, const char *message);
void dirlist_set_source_path(struct dirlist_data *dirlist, const char *source_path);
void dirlist_set_status(struct dirlist_data *dirlist, const char *status_text);
int dirlist_load_file(const char *listing_path, struct dirlist_data *dirlist, char *error_text, int error_text_size);
int dirlist_load_window_file(const char *listing_path,
                             long start_entry,
                             int max_entries,
                             struct dirlist_data *dirlist,
                             char *error_text,
                             int error_text_size);
int dirlist_find_entry_by_filename(const struct dirlist_data *dirlist, const char *filename);
int dirlist_add_synthetic_entry(struct dirlist_data *dirlist,
                                const char *filename,
                                const char *header_text,
                                const char *description_text);
void dirlist_clear_tags(struct dirlist_data *dirlist);
int dirlist_count_tags(const struct dirlist_data *dirlist);
int dirlist_toggle_tag(struct dirlist_data *dirlist, int entry_index);
void dirlist_set_all_tags(struct dirlist_data *dirlist, int tagged_value);

#endif
