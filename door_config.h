/*
 * Runtime configuration for arbfiles.
 */
#ifndef DOOR_CONFIG_H
#define DOOR_CONFIG_H

/* Simple `key=value` settings used at startup. */
struct door_config {
  char bbs_location[256];
  char debug_log[128];
  char trash_path[256];
  int debug_enabled;
  int disable_paging;
  int allow_hold_area;
  int start_in_current_conf;
  int list_block_size;
};

/* Defaults and config-file load helpers. */
void config_set_defaults(struct door_config *config);
int config_load_file(const char *path, struct door_config *config, char *error_text, int error_text_size);

#endif
