/*
 * Conference discovery and cached Ami-Express metadata.
 */
#ifndef AE_CONFIG_SCAN_H
#define AE_CONFIG_SCAN_H

struct door_config;

#define AE_MAX_CONFERENCES 64
#define AE_MAX_AREA_PATHS 32

/* Lightweight conference metadata used by browse and move flows. */
struct ae_conference_info {
  int loaded;
  int number;
  int dir_count;
  int download_path_count;
  int upload_path_count;
  char name[64];
  char location[256];
};

/* Expanded conference record with resolved storage paths. */
struct ae_current_conference_info {
  struct ae_conference_info base;
  char download_paths[AE_MAX_AREA_PATHS][256];
  char upload_paths[AE_MAX_AREA_PATHS][256];
};

/* Whole-system discovery cache built from CONFCONFIG and directory scans. */
struct ae_system_config {
  int loaded;
  int conference_count;
  int discovered_count;
  char bbs_location[256];
  char confconfig_path[256];
  char status_text[160];
  struct ae_conference_info conferences[AE_MAX_CONFERENCES];
};

/* Discovery and lookup helpers. */
void ae_config_scan_init(struct ae_system_config *system_config);
int ae_config_scan_load(const struct door_config *config, struct ae_system_config *system_config, char *error_text, int error_text_size);
const struct ae_conference_info *ae_config_find_conference(const struct ae_system_config *system_config, int conference_number);
int ae_config_find_conference_index(const struct ae_system_config *system_config, int conference_number);
int ae_config_scan_load_current_conference(const char *conf_name,
                                           const char *conf_location,
                                           int conference_number,
                                           struct ae_current_conference_info *conference,
                                           char *error_text,
                                           int error_text_size);

#endif
