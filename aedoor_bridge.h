/*
 * Public interface for the AEDoor bridge layer.
 */
#ifndef AEDOOR_BRIDGE_H
#define AEDOOR_BRIDGE_H

struct door_config;
struct DIFace;
struct Library;

/* Runtime state for the active Ami-Express caller session. */
struct aedoor_context {
  int active;
  int paging_state_known;
  int nonstop_was_enabled;
  int ansi_capable;
  int raw_arrow_enabled;
  int current_conf;
  struct Library *library_base;
  struct DIFace *diface;
  char *string_field;
  char username[64];
  char conf_name[64];
  char conf_location[128];
};

/* Door lifecycle and caller I/O helpers. */
int aedoor_open(struct aedoor_context *context, int argc, char **argv, char *error_text, int error_text_size);
int aedoor_fetch_context(struct aedoor_context *context, char *error_text, int error_text_size);
int aedoor_prepare_session(struct aedoor_context *context, const struct door_config *config, char *error_text, int error_text_size);
void aedoor_clear_screen(struct aedoor_context *context);
void aedoor_write_line(struct aedoor_context *context, const char *text);
void aedoor_write_text(struct aedoor_context *context, const char *text);
int aedoor_poll_key(struct aedoor_context *context, long *key_value);
void aedoor_restore_session(struct aedoor_context *context);
void aedoor_close(struct aedoor_context *context);

#endif
