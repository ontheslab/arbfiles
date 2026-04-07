/*
 * AEDoor bridge for arbfiles.
 *
 * This module wraps the low-level door open/close, user-context fetch, and
 * simple screen/key helpers used by the UI.
 *
 * Ref: the bridge follows AEDoor docs and the same broad door pattern used in
 * arblink, with redraw and colour handling checked against Ami-Express.
 */
#include "aedoor_bridge.h"
#include "aedoor_inline.h"
#include "door_config.h"

#include <proto/exec.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct Library *AEDBase = NULL;

/* Local error and text helpers */
static void bridge_set_error(char *error_text, int error_text_size, const char *message)
{
  if ((error_text == NULL) || (error_text_size <= 0) || (message == NULL)) {
    return;
  }

  strncpy(error_text, message, (size_t) error_text_size - 1U);
  error_text[error_text_size - 1] = '\0';
}

static void bridge_copy_string_field(struct aedoor_context *context, char *buffer, size_t buffer_size)
{
  if ((buffer == NULL) || (buffer_size == 0U)) {
    return;
  }

  buffer[0] = '\0';
  if ((context == NULL) || (context->string_field == NULL)) {
    return;
  }

  strncpy(buffer, context->string_field, buffer_size - 1U);
  buffer[buffer_size - 1U] = '\0';
}

static void bridge_fetch_text_value(struct aedoor_context *context, unsigned long id, char *buffer, size_t buffer_size)
{
  if ((context == NULL) || (context->diface == NULL) || (buffer == NULL) || (buffer_size == 0U)) {
    return;
  }

  GetDT(context->diface, id, NULL);
  bridge_copy_string_field(context, buffer, buffer_size);
}

static long bridge_fetch_data_value(struct aedoor_context *context, unsigned long id, long default_value)
{
  if ((context == NULL) || (context->diface == NULL) || (context->diface->dif_Data == NULL)) {
    return default_value;
  }

  GetDT(context->diface, id, NULL);
  return (long) (*context->diface->dif_Data);
}

/* Door lifecycle */
int aedoor_open(struct aedoor_context *context, int argc, char **argv, char *error_text, int error_text_size)
{
  unsigned long node;

  if (context == NULL) {
    bridge_set_error(error_text, error_text_size, "invalid AEDoor context");
    return -1;
  }

  memset(context, 0, sizeof(*context));
  node = 0;

  if ((argc > 1) && (argv != NULL) && (argv[1] != NULL) && (argv[1][0] != '\0')) {
    node = (unsigned long) (unsigned char) argv[1][0];
  } else {
    bridge_set_error(error_text, error_text_size, "program was not launched with an AEDoor node argument");
    return -1;
  }

  AEDBase = OpenLibrary(AEDoorName, 0);
  if (AEDBase == NULL) {
    bridge_set_error(error_text, error_text_size, "could not open AEDoor.library");
    return -1;
  }

  context->library_base = AEDBase;
  context->diface = CreateComm(node);
  if (context->diface == NULL) {
    bridge_set_error(error_text, error_text_size, "CreateComm failed");
    CloseLibrary(AEDBase);
    AEDBase = NULL;
    memset(context, 0, sizeof(*context));
    return -1;
  }

  context->string_field = GetString(context->diface);
  if (context->string_field == NULL) {
    bridge_set_error(error_text, error_text_size, "GetString failed");
    DeleteComm(context->diface);
    CloseLibrary(AEDBase);
    AEDBase = NULL;
    memset(context, 0, sizeof(*context));
    return -1;
  }

  context->active = 1;
  if (error_text != NULL) {
    error_text[0] = '\0';
  }
  return 0;
}

/* User context and session setup */
int aedoor_fetch_context(struct aedoor_context *context, char *error_text, int error_text_size)
{
  char number_buffer[32];

  if ((context == NULL) || (context->diface == NULL)) {
    bridge_set_error(error_text, error_text_size, "invalid AEDoor fetch request");
    return -1;
  }

  bridge_fetch_text_value(context, DT_NAME, context->username, sizeof(context->username));
  bridge_fetch_text_value(context, BB_CONFNAME, context->conf_name, sizeof(context->conf_name));
  bridge_fetch_text_value(context, BB_CONFLOCAL, context->conf_location, sizeof(context->conf_location));
  bridge_fetch_text_value(context, BB_CONFNUM, number_buffer, sizeof(number_buffer));

  if (context->username[0] == '\0') {
    bridge_set_error(error_text, error_text_size, "AEDoor returned an empty username");
    return -1;
  }

  context->current_conf = number_buffer[0] != '\0' ? atoi(number_buffer) : 0;
  if (error_text != NULL) {
    error_text[0] = '\0';
  }
  return 0;
}

int aedoor_prepare_session(struct aedoor_context *context, const struct door_config *config, char *error_text, int error_text_size)
{
  char state_buffer[16];

  if ((context == NULL) || (config == NULL)) {
    bridge_set_error(error_text, error_text_size, "invalid AEDoor session setup");
    return -1;
  }

  if (config->disable_paging) {
    GetDT(context->diface, BB_NONSTOPTEXT, NULL);
    bridge_copy_string_field(context, state_buffer, sizeof(state_buffer));
    if (state_buffer[0] != '\0') {
      context->nonstop_was_enabled = atoi(state_buffer) != 0;
      context->paging_state_known = 1;
    }
    SetDT(context->diface, BB_NONSTOPTEXT, "1");
  }

  context->ansi_capable = bridge_fetch_data_value(context, DT_ISANSI, 0) != 0;

  if (context->ansi_capable) {
    SendDataCmd(context->diface, RAWARROW, 0);
    context->raw_arrow_enabled = 1;
  }

  if (error_text != NULL) {
    error_text[0] = '\0';
  }
  return 0;
}

/* Output and input helpers */
void aedoor_clear_screen(struct aedoor_context *context)
{
  if ((context == NULL) || (context->diface == NULL)) {
    return;
  }

  /*
   * Ami-Express uses form-feed as its native clear-screen marker and tracks
   * page flow separately through BB_LINECOUNT. Some live paths still appear
   * to ignore that on redraw, so also send an explicit ANSI clear/home
   * sequence through AEDoor to force a real repaint on ANSI users.
   */
  SetDT(context->diface, BB_NONSTOPTEXT, "1");
  WriteStr(context->diface, "\f", NOLF);
  if (context->ansi_capable) {
    WriteStr(context->diface, "\033[2J\033[H", NOLF);
  }
  SetDT(context->diface, BB_LINECOUNT, "0");
  SendDataCmd(context->diface, CON_CURSOR, 1);
}

void aedoor_write_line(struct aedoor_context *context, const char *text)
{
  if ((context != NULL) && (context->diface != NULL) && (text != NULL)) {
    WriteStr(context->diface, (char *) text, WSF_LF);
  }
}

void aedoor_write_text(struct aedoor_context *context, const char *text)
{
  if ((context != NULL) && (context->diface != NULL) && (text != NULL)) {
    WriteStr(context->diface, (char *) text, NOLF);
  }
}

int aedoor_poll_key(struct aedoor_context *context, long *key_value)
{
  long key;

  if ((context == NULL) || (context->diface == NULL) || (context->string_field == NULL) || (key_value == NULL)) {
    return -1;
  }

  SendCmd(context->diface, GETKEY);
  if (context->string_field[0] != '1') {
    return 0;
  }

  key = Hotkey(context->diface, "");
  SendDataCmd(context->diface, CON_CURSOR, 1);
  if (key < 0) {
    return -1;
  }

  *key_value = key;
  return 1;
}

/* Session shutdown */
void aedoor_restore_session(struct aedoor_context *context)
{
  if ((context == NULL) || !context->active || (context->diface == NULL)) {
    return;
  }

  if (context->raw_arrow_enabled) {
    SendDataCmd(context->diface, RAWARROW, 0);
    context->raw_arrow_enabled = 0;
  }

  SendDataCmd(context->diface, CON_CURSOR, 1);
  if (context->paging_state_known) {
    SetDT(context->diface, BB_NONSTOPTEXT, context->nonstop_was_enabled ? "1" : "0");
  }
}

void aedoor_close(struct aedoor_context *context)
{
  if (context == NULL) {
    return;
  }

  if (context->diface != NULL) {
    DeleteComm(context->diface);
  }
  if (context->library_base != NULL) {
    CloseLibrary(context->library_base);
  }

  context->active = 0;
  context->diface = NULL;
  context->string_field = NULL;
  context->library_base = NULL;
  AEDBase = NULL;
}
