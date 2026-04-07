/*
 * Tag tracking for work that spans more than one loaded block.
 */
#ifndef TAGSET_H
#define TAGSET_H

struct dirlist_data;

#define TAGSET_GROW_CHUNK 128

struct tagset_entry {
  char filename[32];
};

struct tagset_data {
  int count;
  int capacity;
  int source_conf_number;
  int source_area;
  char listing_path[256];
  struct tagset_entry *entries;
};

/* Setup, cleanup, and source-scope helpers. */
void tagset_init(struct tagset_data *tagset);
void tagset_free(struct tagset_data *tagset);
void tagset_clear(struct tagset_data *tagset);
void tagset_reset_scope(struct tagset_data *tagset, int source_conf_number, int source_area, const char *listing_path);

/* Small tag lookups and updates. */
int tagset_count(const struct tagset_data *tagset);
int tagset_contains(const struct tagset_data *tagset, const char *filename);
int tagset_add(struct tagset_data *tagset, const char *filename);
int tagset_remove(struct tagset_data *tagset, const char *filename);
int tagset_toggle(struct tagset_data *tagset, const char *filename);

/* Apply or build tags from listings. */
void tagset_apply_to_dirlist(const struct tagset_data *tagset, struct dirlist_data *dirlist);
int tagset_add_all_from_dirlist(struct tagset_data *tagset, const struct dirlist_data *dirlist);
int tagset_add_all_from_listing(struct tagset_data *tagset,
                                const char *listing_path,
                                char *error_text,
                                int error_text_size);

#endif
