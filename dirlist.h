/*
 * Minimal parsed representation of an Ami-Express DIR listing.
 */
#ifndef DIRLIST_H
#define DIRLIST_H

#define DIRLIST_MAX_ENTRIES 256

/* One visible file entry plus its captured description text. */
struct dirlist_entry {
  int description_line_count;
  char filename[32];
  char header_line[256];
  char description[256];
};

/* Parsed listing plus a little UI/debug context. */
struct dirlist_data {
  int entry_count;
  char listing_path[256];
  char source_path[256];
  char status_text[80];
  struct dirlist_entry entries[DIRLIST_MAX_ENTRIES];
};

/* Listing load and status helpers. */
void dirlist_init(struct dirlist_data *dirlist);
void dirlist_set_error(char *error_text, int error_text_size, const char *message);
void dirlist_set_source_path(struct dirlist_data *dirlist, const char *source_path);
void dirlist_set_status(struct dirlist_data *dirlist, const char *status_text);
int dirlist_load_file(const char *listing_path, struct dirlist_data *dirlist, char *error_text, int error_text_size);

#endif
