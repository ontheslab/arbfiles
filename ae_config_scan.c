/*
 * AmiExpress configuration discovery.
 *
 * This pass reads CONFCONFIG and the per-conference config objects via
 * icon.library tooltypes. It is intentionally conservative and only gathers
 * the metadata needed for the next UI and parser stages.
 */
#include "ae_config_scan.h"
#include "door_config.h"

#include <exec/libraries.h>
#include <proto/exec.h>
#include <proto/icon.h>
#include <proto/dos.h>
#include <workbench/workbench.h>

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct Library *IconBase = NULL;
static struct ae_system_config g_confconfig_metadata;

/* Small shared helpers */
static void scan_set_error(char *error_text, int error_text_size, const char *message)
{
  if ((error_text == NULL) || (error_text_size <= 0) || (message == NULL)) {
    return;
  }

  strncpy(error_text, message, (size_t) error_text_size - 1U);
  error_text[error_text_size - 1] = '\0';
}

static void scan_join_path(char *output, size_t output_size, const char *base_path, const char *leaf_name)
{
  size_t text_length;

  if ((output == NULL) || (output_size == 0U)) {
    return;
  }

  output[0] = '\0';
  if ((base_path == NULL) || (*base_path == '\0') || (leaf_name == NULL)) {
    return;
  }

  text_length = strlen(base_path);
  if ((text_length > 0U) && ((base_path[text_length - 1U] == '/') || (base_path[text_length - 1U] == ':'))) {
    snprintf(output, output_size, "%s%s", base_path, leaf_name);
  } else {
    snprintf(output, output_size, "%s/%s", base_path, leaf_name);
  }
}

static int scan_text_equals(const char *left, const char *right)
{
  unsigned char lch;
  unsigned char rch;

  if ((left == NULL) || (right == NULL)) {
    return 0;
  }

  while ((*left != '\0') && (*right != '\0')) {
    lch = (unsigned char) toupper((unsigned char) *left);
    rch = (unsigned char) toupper((unsigned char) *right);
    if (lch != rch) {
      return 0;
    }
    left++;
    right++;
  }

  return (*left == '\0') && (*right == '\0');
}

static const char *scan_tool_value(const char *tool_text)
{
  const char *separator;

  if (tool_text == NULL) {
    return NULL;
  }

  separator = strchr(tool_text, '=');
  if (separator == NULL) {
    return "";
  }

  return separator + 1;
}

static void scan_copy_text(char *output, size_t output_size, const char *input)
{
  if ((output == NULL) || (output_size == 0U)) {
    return;
  }

  output[0] = '\0';
  if (input == NULL) {
    return;
  }

  strncpy(output, input, output_size - 1U);
  output[output_size - 1U] = '\0';
}

static void scan_trim_trailing_separators(char *path)
{
  size_t length;

  if (path == NULL) {
    return;
  }

  length = strlen(path);
  while ((length > 0U) && (path[length - 1U] == '/')) {
    path[length - 1U] = '\0';
    length--;
  }
}

static void scan_ensure_trailing_separator(char *path, size_t path_size)
{
  size_t length;

  if ((path == NULL) || (path_size == 0U)) {
    return;
  }

  length = strlen(path);
  if (length + 1U >= path_size) {
    return;
  }

  if ((length > 0U) && (path[length - 1U] != '/') && (path[length - 1U] != ':')) {
    path[length] = '/';
    path[length + 1U] = '\0';
  }
}

static int scan_read_int_file(const char *path, int *value)
{
  FILE *handle;
  char line[64];

  if ((path == NULL) || (*path == '\0') || (value == NULL)) {
    return -1;
  }

  handle = fopen(path, "r");
  if (handle == NULL) {
    return -1;
  }

  if (fgets(line, sizeof(line), handle) == NULL) {
    fclose(handle);
    return -1;
  }

  fclose(handle);
  *value = atoi(line);
  return 0;
}

static int scan_read_text_file(const char *path, char *output, size_t output_size)
{
  FILE *handle;
  char line[256];
  size_t length;

  if ((path == NULL) || (*path == '\0') || (output == NULL) || (output_size == 0U)) {
    return -1;
  }

  handle = fopen(path, "r");
  if (handle == NULL) {
    return -1;
  }

  if (fgets(line, sizeof(line), handle) == NULL) {
    fclose(handle);
    return -1;
  }

  fclose(handle);
  length = strlen(line);
  while ((length > 0U) && ((line[length - 1U] == '\n') || (line[length - 1U] == '\r'))) {
    line[length - 1U] = '\0';
    length--;
  }

  scan_copy_text(output, output_size, line);
  return output[0] != '\0' ? 0 : -1;
}

static int scan_parse_path_index(const char *text, const char *prefix)
{
  const char *digits;
  const char *equals;

  if ((text == NULL) || (prefix == NULL)) {
    return -1;
  }

  if (strncmp(text, prefix, strlen(prefix)) != 0) {
    return -1;
  }

  digits = text + strlen(prefix);
  if (!isdigit((unsigned char) *digits)) {
    return -1;
  }

  equals = strchr(digits, '=');
  if (equals == NULL) {
    return -1;
  }

  return atoi(digits);
}

static void scan_store_indexed_path(char paths[AE_MAX_AREA_PATHS][256],
                                    int *path_count,
                                    int index,
                                    const char *value)
{
  int slot;

  if ((paths == NULL) || (path_count == NULL) || (value == NULL)) {
    return;
  }

  if ((index < 1) || (index > AE_MAX_AREA_PATHS)) {
    return;
  }

  slot = index - 1;
  scan_copy_text(paths[slot], sizeof(paths[slot]), value);
  if (*path_count < index) {
    *path_count = index;
  }
}

/* Raw icon and tooltype parsing */
static void scan_parse_raw_icon_string(const char *text,
                                       struct ae_current_conference_info *conference,
                                       int *loaded_anything)
{
  const char *value;
  int path_index;

  if ((text == NULL) || (conference == NULL) || (loaded_anything == NULL)) {
    return;
  }

  if (strncmp(text, "NDIRS=", 6) == 0) {
    conference->base.dir_count = atoi(text + 6);
    *loaded_anything = 1;
    return;
  }

  path_index = scan_parse_path_index(text, "DLPATH.");
  if (path_index > 0) {
    value = strchr(text, '=');
    if (value != NULL) {
      scan_store_indexed_path(conference->download_paths,
                              &conference->base.download_path_count,
                              path_index,
                              value + 1);
      *loaded_anything = 1;
    }
    return;
  }

  path_index = scan_parse_path_index(text, "ULPATH.");
  if (path_index > 0) {
    value = strchr(text, '=');
    if (value != NULL) {
      scan_store_indexed_path(conference->upload_paths,
                              &conference->base.upload_path_count,
                              path_index,
                              value + 1);
      *loaded_anything = 1;
    }
  }
}

static void scan_parse_conference_icon_token(const char *text, void *context, int *loaded_anything)
{
  scan_parse_raw_icon_string(text, (struct ae_current_conference_info *) context, loaded_anything);
}

static int scan_parse_numbered_name(const char *text,
                                    const char *prefix,
                                    int *index_out,
                                    const char **value_out)
{
  const char *digits;
  const char *equals;

  if ((text == NULL) || (prefix == NULL) || (index_out == NULL) || (value_out == NULL)) {
    return -1;
  }

  if (strncmp(text, prefix, strlen(prefix)) != 0) {
    return -1;
  }

  digits = text + strlen(prefix);
  if (!isdigit((unsigned char) *digits)) {
    return -1;
  }

  equals = strchr(digits, '=');
  if (equals == NULL) {
    return -1;
  }

  *index_out = atoi(digits);
  *value_out = equals + 1;
  return 0;
}

static int scan_load_raw_icon_file(const char *object_path,
                                   void (*token_handler)(const char *text, void *context, int *loaded_anything),
                                   void *context)
{
  BPTR handle;
  LONG read_length;
  unsigned char buffer[512];
  char info_path[260];
  char object_base[256];
  char token[256];
  size_t token_length;
  int loaded_anything;
  LONG index;
  unsigned char ch;

  if ((object_path == NULL) || (*object_path == '\0') || (token_handler == NULL)) {
    return -1;
  }

  scan_copy_text(object_base, sizeof(object_base), object_path);
  scan_trim_trailing_separators(object_base);
  snprintf(info_path, sizeof(info_path), "%s.info", object_base);

  handle = Open((STRPTR) info_path, MODE_OLDFILE);
  if (handle == 0) {
    return -1;
  }

  token_length = 0U;
  loaded_anything = 0;
  while ((read_length = Read(handle, buffer, sizeof(buffer))) > 0) {
    for (index = 0; index < read_length; index++) {
      ch = buffer[index];
      if ((ch >= 32U) && (ch <= 126U)) {
        if (token_length + 1U < sizeof(token)) {
          token[token_length++] = (char) ch;
        }
        continue;
      }

      if (token_length > 0U) {
        token[token_length] = '\0';
        token_handler(token, context, &loaded_anything);
        token_length = 0U;
      }
    }
  }
  Close(handle);

  if (token_length > 0U) {
    token[token_length] = '\0';
    token_handler(token, context, &loaded_anything);
  }

  return (read_length < 0) ? -1 : (loaded_anything ? 0 : -1);
}

static int scan_load_raw_icon_tooltypes(const char *location, struct ae_current_conference_info *conference)
{
  char config_path[256];
  int loaded_anything;

  if ((location == NULL) || (*location == '\0') || (conference == NULL)) {
    return -1;
  }

  scan_copy_text(config_path, sizeof(config_path), location);
  loaded_anything = 0;
  return scan_load_raw_icon_file(config_path,
                                 scan_parse_conference_icon_token,
                                 conference);
}

/* CONFCONFIG metadata loading */
static void scan_parse_confconfig_token(const char *text, void *context, int *loaded_anything)
{
  struct ae_system_config *system_config;
  const char *value;
  int item_index;

  system_config = (struct ae_system_config *) context;
  if ((text == NULL) || (system_config == NULL) || (loaded_anything == NULL)) {
    return;
  }

  if (strncmp(text, "NCONFS=", 7) == 0) {
    system_config->conference_count = atoi(text + 7);
    *loaded_anything = 1;
    return;
  }

  if ((scan_parse_numbered_name(text, "NAME.", &item_index, &value) == 0) &&
      (item_index >= 1) && (item_index <= AE_MAX_CONFERENCES)) {
    scan_copy_text(system_config->conferences[item_index - 1].name,
                   sizeof(system_config->conferences[item_index - 1].name),
                   value);
    system_config->conferences[item_index - 1].number = item_index;
    *loaded_anything = 1;
    return;
  }

  if ((scan_parse_numbered_name(text, "LOCATION.", &item_index, &value) == 0) &&
      (item_index >= 1) && (item_index <= AE_MAX_CONFERENCES)) {
    scan_copy_text(system_config->conferences[item_index - 1].location,
                   sizeof(system_config->conferences[item_index - 1].location),
                   value);
    system_config->conferences[item_index - 1].number = item_index;
    *loaded_anything = 1;
  }
}

static struct DiskObject *scan_load_confconfig_disk_object(const char *bbs_location,
                                                           char *resolved_path,
                                                           size_t resolved_path_size)
{
  static const char *leaf_names[] = {
    "CONFCONFIG",
    "ConfConfig",
    "confconfig"
  };
  struct DiskObject *disk_object;
  int index;

  if ((bbs_location == NULL) || (*bbs_location == '\0') || (resolved_path == NULL) || (resolved_path_size == 0U)) {
    return NULL;
  }

  for (index = 0; index < (int) (sizeof(leaf_names) / sizeof(leaf_names[0])); index++) {
    scan_join_path(resolved_path, resolved_path_size, bbs_location, leaf_names[index]);
    disk_object = GetDiskObject((STRPTR) resolved_path);
    if (disk_object != NULL) {
      return disk_object;
    }
  }

  resolved_path[0] = '\0';
  return NULL;
}

/* Per-conference path loading */
static int scan_parse_int_tool(STRPTR *tool_types, const char *tool_name, int default_value)
{
  STRPTR tool_text;
  const char *value_text;

  if ((tool_types == NULL) || (tool_name == NULL)) {
    return default_value;
  }

  tool_text = FindToolType((STRPTR *) tool_types, (STRPTR) tool_name);
  if (tool_text == NULL) {
    return default_value;
  }

  value_text = scan_tool_value((const char *) tool_text);
  if ((value_text == NULL) || (*value_text == '\0')) {
    return default_value;
  }

  return atoi(value_text);
}

static int scan_parse_conf_number_from_name(const char *name)
{
  const char *digits;

  if (name == NULL) {
    return 0;
  }

  if ((toupper((unsigned char) name[0]) != 'C') ||
      (toupper((unsigned char) name[1]) != 'O') ||
      (toupper((unsigned char) name[2]) != 'N') ||
      (toupper((unsigned char) name[3]) != 'F')) {
    return 0;
  }

  digits = name + 4;
  if (*digits == '\0') {
    return 0;
  }

  while (*digits != '\0') {
    if (!isdigit((unsigned char) *digits)) {
      return 0;
    }
    digits++;
  }

  return atoi(name + 4);
}

static void scan_parse_text_tool(STRPTR *tool_types, const char *tool_name, char *output, size_t output_size)
{
  STRPTR tool_text;

  if ((tool_types == NULL) || (tool_name == NULL) || (output == NULL) || (output_size == 0U)) {
    return;
  }

  tool_text = FindToolType((STRPTR *) tool_types, (STRPTR) tool_name);
  if (tool_text == NULL) {
    output[0] = '\0';
    return;
  }

  scan_copy_text(output, output_size, scan_tool_value((const char *) tool_text));
}

static void scan_load_prefixed_paths(STRPTR *tool_types, const char *prefix, char paths[AE_MAX_AREA_PATHS][256], int *path_count)
{
  int index;
  char tool_name[32];

  if ((tool_types == NULL) || (prefix == NULL) || (paths == NULL) || (path_count == NULL)) {
    return;
  }

  *path_count = 0;
  for (index = 0; index < AE_MAX_AREA_PATHS; index++) {
    snprintf(tool_name, sizeof(tool_name), "%s.%d", prefix, index + 1);
    scan_parse_text_tool(tool_types, tool_name, paths[index], 256);
    if (paths[index][0] != '\0') {
      *path_count = index + 1;
    }
  }
}

static int scan_conference_has_file_areas(const struct ae_current_conference_info *conference)
{
  return (conference != NULL) && (conference->base.dir_count > 0);
}

static void scan_store_discovered_conference(struct ae_system_config *system_config,
                                             const struct ae_current_conference_info *current_conference)
{
  struct ae_conference_info *conference;

  if ((system_config == NULL) || (current_conference == NULL)) {
    return;
  }

  if (system_config->discovered_count >= AE_MAX_CONFERENCES) {
    return;
  }

  conference = &system_config->conferences[system_config->discovered_count];
  memset(conference, 0, sizeof(*conference));
  conference->loaded = current_conference->base.loaded;
  conference->number = current_conference->base.number;
  conference->dir_count = current_conference->base.dir_count;
  conference->download_path_count = current_conference->base.download_path_count;
  conference->upload_path_count = current_conference->base.upload_path_count;
  scan_copy_text(conference->name, sizeof(conference->name), current_conference->base.name);
  scan_copy_text(conference->location, sizeof(conference->location), current_conference->base.location);
  system_config->discovered_count++;
}

static const struct ae_conference_info *scan_find_metadata_conference(const struct ae_system_config *system_config,
                                                                      int conference_number)
{
  int index;

  if ((system_config == NULL) || (conference_number <= 0)) {
    return NULL;
  }

  for (index = 0; index < AE_MAX_CONFERENCES; index++) {
    if (system_config->conferences[index].number == conference_number) {
      return &system_config->conferences[index];
    }
  }

  return NULL;
}

static int scan_load_confconfig_metadata(const char *bbs_location,
                                         struct ae_system_config *metadata)
{
  static const char *leaf_names[] = {
    "CONFCONFIG",
    "ConfConfig",
    "confconfig"
  };
  struct DiskObject *confconfig_object;
  int index;
  int loaded_anything;
  int max_to_store;
  char tool_name[32];

  if ((bbs_location == NULL) || (*bbs_location == '\0') || (metadata == NULL)) {
    return -1;
  }

  ae_config_scan_init(metadata);
  scan_copy_text(metadata->bbs_location, sizeof(metadata->bbs_location), bbs_location);
  loaded_anything = 0;

  confconfig_object = scan_load_confconfig_disk_object(bbs_location,
                                                       metadata->confconfig_path,
                                                       sizeof(metadata->confconfig_path));
  if (confconfig_object != NULL) {
    metadata->conference_count = scan_parse_int_tool(confconfig_object->do_ToolTypes, "NCONFS", 0);
    max_to_store = metadata->conference_count;
    if (max_to_store > AE_MAX_CONFERENCES) {
      max_to_store = AE_MAX_CONFERENCES;
    }

    for (index = 0; index < max_to_store; index++) {
      metadata->conferences[index].number = index + 1;

      snprintf(tool_name, sizeof(tool_name), "NAME.%d", index + 1);
      scan_parse_text_tool(confconfig_object->do_ToolTypes,
                           tool_name,
                           metadata->conferences[index].name,
                           sizeof(metadata->conferences[index].name));

      snprintf(tool_name, sizeof(tool_name), "LOCATION.%d", index + 1);
      scan_parse_text_tool(confconfig_object->do_ToolTypes,
                           tool_name,
                           metadata->conferences[index].location,
                           sizeof(metadata->conferences[index].location));

      if ((metadata->conferences[index].name[0] != '\0') ||
          (metadata->conferences[index].location[0] != '\0')) {
        loaded_anything = 1;
      }
    }

    FreeDiskObject(confconfig_object);
  }

  for (index = 0; index < (int) (sizeof(leaf_names) / sizeof(leaf_names[0])); index++) {
    char raw_path[256];

    scan_join_path(raw_path, sizeof(raw_path), bbs_location, leaf_names[index]);
    if (scan_load_raw_icon_file(raw_path,
                                scan_parse_confconfig_token,
                                metadata) == 0) {
      if (metadata->confconfig_path[0] == '\0') {
        scan_copy_text(metadata->confconfig_path, sizeof(metadata->confconfig_path), raw_path);
      }
      loaded_anything = 1;
      break;
    }
  }

  return loaded_anything ? 0 : -1;
}

static void scan_sort_conferences(struct ae_system_config *system_config)
{
  int index;
  int pass;
  struct ae_conference_info temp;

  if ((system_config == NULL) || (system_config->discovered_count <= 1)) {
    return;
  }

  for (pass = 0; pass < system_config->discovered_count - 1; pass++) {
    for (index = 0; index < system_config->discovered_count - 1 - pass; index++) {
      if (system_config->conferences[index].number > system_config->conferences[index + 1].number) {
        temp = system_config->conferences[index];
        system_config->conferences[index] = system_config->conferences[index + 1];
        system_config->conferences[index + 1] = temp;
      }
    }
  }
}

static int scan_load_current_conference_paths(const char *location, struct ae_current_conference_info *conference)
{
  struct DiskObject *disk_object;
  char config_path[256];
  char ndirs_path[256];
  char paths_path[256];
  int loaded_anything;

  if ((location == NULL) || (*location == '\0') || (conference == NULL)) {
    return -1;
  }

  loaded_anything = 0;
  scan_copy_text(config_path, sizeof(config_path), location);
  scan_trim_trailing_separators(config_path);

  disk_object = GetDiskObject((STRPTR) config_path);
  if ((disk_object == NULL) && (scan_text_equals(config_path, location) == 0)) {
    disk_object = GetDiskObject((STRPTR) location);
  }
  if (disk_object != NULL) {
    conference->base.dir_count = scan_parse_int_tool(disk_object->do_ToolTypes, "NDIRS", 0);
    scan_load_prefixed_paths(disk_object->do_ToolTypes, "DLPATH", conference->download_paths, &conference->base.download_path_count);
    scan_load_prefixed_paths(disk_object->do_ToolTypes, "ULPATH", conference->upload_paths, &conference->base.upload_path_count);
    if ((conference->base.dir_count > 0) ||
        (conference->base.download_path_count > 0) ||
        (conference->base.upload_path_count > 0)) {
      loaded_anything = 1;
    }

    FreeDiskObject(disk_object);
  }

  if ((conference->base.dir_count <= 0) ||
      (conference->base.download_path_count <= 0) ||
      (conference->base.upload_path_count <= 0)) {
    if (scan_load_raw_icon_tooltypes(location, conference) == 0) {
      loaded_anything = 1;
    }
  }

  if (conference->base.dir_count <= 0) {
    scan_join_path(ndirs_path, sizeof(ndirs_path), location, "NDirs");
    if (scan_read_int_file(ndirs_path, &conference->base.dir_count) == 0) {
      loaded_anything = 1;
    } else {
      scan_join_path(ndirs_path, sizeof(ndirs_path), location, "NDIRS");
      if (scan_read_int_file(ndirs_path, &conference->base.dir_count) == 0) {
        loaded_anything = 1;
      }
    }
  }

  if (conference->base.upload_path_count <= 0) {
    scan_join_path(paths_path, sizeof(paths_path), location, "paths");
    if (scan_read_text_file(paths_path, conference->upload_paths[0], sizeof(conference->upload_paths[0])) == 0) {
      conference->base.upload_path_count = 1;
      loaded_anything = 1;
    } else {
      scan_join_path(paths_path, sizeof(paths_path), location, "PATHS");
      if (scan_read_text_file(paths_path, conference->upload_paths[0], sizeof(conference->upload_paths[0])) == 0) {
        conference->base.upload_path_count = 1;
        loaded_anything = 1;
      }
    }
  }

  conference->base.loaded = loaded_anything ? 1 : 0;
  return loaded_anything ? 0 : -1;
}

/* Public scan entry points */
void ae_config_scan_init(struct ae_system_config *system_config)
{
  if (system_config == NULL) {
    return;
  }

  memset(system_config, 0, sizeof(*system_config));
  system_config->conference_count = -1;
  strncpy(system_config->status_text, "AmiExpress discovery not yet attempted.", sizeof(system_config->status_text) - 1U);
  system_config->status_text[sizeof(system_config->status_text) - 1U] = '\0';
}

const struct ae_conference_info *ae_config_find_conference(const struct ae_system_config *system_config, int conference_number)
{
  int index;

  index = ae_config_find_conference_index(system_config, conference_number);
  if (index < 0) {
    return NULL;
  }

  return &system_config->conferences[index];
}

int ae_config_find_conference_index(const struct ae_system_config *system_config, int conference_number)
{
  int index;

  if ((system_config == NULL) || (conference_number <= 0)) {
    return -1;
  }

  for (index = 0; index < system_config->discovered_count; index++) {
    if (system_config->conferences[index].number == conference_number) {
      return index;
    }
  }

  return -1;
}

int ae_config_scan_load_current_conference(const char *conf_name,
                                           const char *conf_location,
                                           int conference_number,
                                           struct ae_current_conference_info *conference,
                                           char *error_text,
                                           int error_text_size)
{
  if ((conf_location == NULL) || (*conf_location == '\0') || (conference == NULL)) {
    scan_set_error(error_text, error_text_size, "invalid conference fallback request");
    return -1;
  }

  memset(conference, 0, sizeof(*conference));
  conference->base.number = conference_number;
  scan_copy_text(conference->base.name, sizeof(conference->base.name), conf_name);
  scan_copy_text(conference->base.location, sizeof(conference->base.location), conf_location);

  IconBase = OpenLibrary("icon.library", 0);
  if (IconBase == NULL) {
    scan_set_error(error_text, error_text_size, "could not open icon.library");
    return -1;
  }

  if (scan_load_current_conference_paths(conf_location, conference) != 0) {
    CloseLibrary(IconBase);
    IconBase = NULL;
    scan_set_error(error_text, error_text_size, "conference config could not be opened through icon.library");
    return -1;
  }

  CloseLibrary(IconBase);
  IconBase = NULL;

  if (error_text != NULL) {
    error_text[0] = '\0';
  }
  return 0;
}

int ae_config_scan_load(const struct door_config *config, struct ae_system_config *system_config, char *error_text, int error_text_size)
{
  struct ae_system_config *metadata;
  const struct ae_conference_info *metadata_conference;
  BPTR lock;
  struct FileInfoBlock *fib;
  int index;
  int max_to_store;
  struct ae_current_conference_info current_conference;

  if ((config == NULL) || (system_config == NULL)) {
    scan_set_error(error_text, error_text_size, "invalid AmiExpress scan request");
    return -1;
  }

  ae_config_scan_init(system_config);
  scan_copy_text(system_config->bbs_location, sizeof(system_config->bbs_location), config->bbs_location);
  metadata = &g_confconfig_metadata;
  ae_config_scan_init(metadata);
  scan_copy_text(metadata->bbs_location, sizeof(metadata->bbs_location), config->bbs_location);

  if (config->bbs_location[0] == '\0') {
    strncpy(system_config->status_text, "Set bbs_location in arbfiles.cfg to enable CONFCONFIG discovery.", sizeof(system_config->status_text) - 1U);
    system_config->status_text[sizeof(system_config->status_text) - 1U] = '\0';
    scan_set_error(error_text, error_text_size, "bbs_location is not configured");
    return -1;
  }

  IconBase = OpenLibrary("icon.library", 0);
  if (IconBase == NULL) {
    strncpy(system_config->status_text, "icon.library could not be opened.", sizeof(system_config->status_text) - 1U);
    system_config->status_text[sizeof(system_config->status_text) - 1U] = '\0';
    scan_set_error(error_text, error_text_size, "could not open icon.library");
    return -1;
  }

  if (scan_load_confconfig_metadata(config->bbs_location, metadata) == 0) {
    system_config->conference_count = metadata->conference_count;
    scan_copy_text(system_config->confconfig_path,
                   sizeof(system_config->confconfig_path),
                   metadata->confconfig_path);

    max_to_store = metadata->conference_count;
    if (max_to_store > AE_MAX_CONFERENCES) {
      max_to_store = AE_MAX_CONFERENCES;
    }

    for (index = 0; index < max_to_store; index++) {
      if (metadata->conferences[index].number <= 0) {
        continue;
      }

      memset(&current_conference, 0, sizeof(current_conference));
      current_conference.base.number = metadata->conferences[index].number;
      scan_copy_text(current_conference.base.name,
                     sizeof(current_conference.base.name),
                     metadata->conferences[index].name);
      scan_copy_text(current_conference.base.location,
                     sizeof(current_conference.base.location),
                     metadata->conferences[index].location);

      if (current_conference.base.location[0] == '\0') {
        continue;
      }

      scan_load_current_conference_paths(current_conference.base.location, &current_conference);
      if (!scan_conference_has_file_areas(&current_conference)) {
        continue;
      }

      scan_store_discovered_conference(system_config, &current_conference);
    }
  }

  if (system_config->discovered_count <= 0) {
    lock = Lock((STRPTR) config->bbs_location, ACCESS_READ);
    if (lock != 0) {
      fib = AllocDosObject(DOS_FIB, NULL);
      if (fib != NULL) {
        if (Examine(lock, fib) != DOSFALSE) {
          while ((system_config->discovered_count < AE_MAX_CONFERENCES) && (ExNext(lock, fib) != DOSFALSE)) {
            int conference_number;

            if (fib->fib_DirEntryType <= 0) {
              continue;
            }

            conference_number = scan_parse_conf_number_from_name((const char *) fib->fib_FileName);
            if (conference_number <= 0) {
              continue;
            }

            memset(&current_conference, 0, sizeof(current_conference));
            current_conference.base.number = conference_number;
            metadata_conference = scan_find_metadata_conference(metadata, conference_number);
            if ((metadata_conference != NULL) && (metadata_conference->name[0] != '\0')) {
              scan_copy_text(current_conference.base.name,
                             sizeof(current_conference.base.name),
                             metadata_conference->name);
            } else {
              scan_copy_text(current_conference.base.name,
                             sizeof(current_conference.base.name),
                             (const char *) fib->fib_FileName);
            }

            if ((metadata_conference != NULL) && (metadata_conference->location[0] != '\0')) {
              scan_copy_text(current_conference.base.location,
                             sizeof(current_conference.base.location),
                             metadata_conference->location);
            } else {
              scan_join_path(current_conference.base.location,
                             sizeof(current_conference.base.location),
                             config->bbs_location,
                             (const char *) fib->fib_FileName);
              scan_ensure_trailing_separator(current_conference.base.location, sizeof(current_conference.base.location));
            }

            scan_load_current_conference_paths(current_conference.base.location, &current_conference);
            if (!scan_conference_has_file_areas(&current_conference)) {
              continue;
            }

            scan_store_discovered_conference(system_config, &current_conference);
          }
        }
        FreeDosObject(DOS_FIB, fib);
      }
      UnLock(lock);
    }
  }

  scan_sort_conferences(system_config);

  CloseLibrary(IconBase);
  IconBase = NULL;

  system_config->loaded = 1;
  if (system_config->discovered_count <= 0) {
    snprintf(system_config->status_text,
             sizeof(system_config->status_text),
             "Loaded 0 conferences from CONFCONFIG.");
  } else if (system_config->conference_count > AE_MAX_CONFERENCES) {
    snprintf(system_config->status_text,
             sizeof(system_config->status_text),
             "Loaded %d of %d conferences from CONFCONFIG.",
             system_config->discovered_count,
             system_config->conference_count);
  } else if (system_config->conference_count > 0) {
    snprintf(system_config->status_text,
             sizeof(system_config->status_text),
             "Loaded %d conferences from CONFCONFIG.",
             system_config->discovered_count);
  } else {
    snprintf(system_config->status_text,
             sizeof(system_config->status_text),
             "Loaded %d conferences from directory scan.",
             system_config->discovered_count);
  }

  if (error_text != NULL) {
    error_text[0] = '\0';
  }
  return 0;
}
