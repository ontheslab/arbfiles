/*
 * File move and DIR-list rewrite helpers.
 *
 * This module keeps the on-disk file move and the source/destination listing
 * updates together so rollback can stay in one place.
 */
#include "file_ops.h"
#include "dirlist.h"

#include <dos/dos.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <proto/dos.h>

/* Error helpers */
static void file_ops_set_error(char *error_text, int error_text_size, const char *message)
{
  if ((error_text == NULL) || (error_text_size <= 0) || (message == NULL)) {
    return;
  }

  strncpy(error_text, message, (size_t) error_text_size - 1U);
  error_text[error_text_size - 1] = '\0';
}

static void file_ops_set_dos_error(char *error_text,
                                   int error_text_size,
                                   const char *message,
                                   LONG io_error)
{
  char buffer[160];

  if ((message == NULL) || (error_text == NULL) || (error_text_size <= 0)) {
    return;
  }

  snprintf(buffer, sizeof(buffer), "%s (IoErr %ld)", message, io_error);
  file_ops_set_error(error_text, error_text_size, buffer);
}

static void file_ops_set_runtime_error(char *error_text,
                                       int error_text_size,
                                       const char *message,
                                       int error_code)
{
  char buffer[160];

  if ((message == NULL) || (error_text == NULL) || (error_text_size <= 0)) {
    return;
  }

  snprintf(buffer, sizeof(buffer), "%s (errno %d)", message, error_code);
  file_ops_set_error(error_text, error_text_size, buffer);
}

static int file_ops_replace_with_temp(const char *temp_path, const char *target_path, int *error_code);

/* Path and raw file helpers */
static void file_ops_join_path(char *output, size_t output_size, const char *base_path, const char *leaf_name)
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

static int file_ops_copy_file(const char *source_path, const char *destination_path)
{
  BPTR source;
  BPTR destination;
  char buffer[1024];
  LONG length;

  source = Open((STRPTR) source_path, MODE_OLDFILE);
  if (source == 0) {
    return -1;
  }

  destination = Open((STRPTR) destination_path, MODE_NEWFILE);
  if (destination == 0) {
    Close(source);
    return -1;
  }

  while ((length = Read(source, buffer, sizeof(buffer))) > 0) {
    if (Write(destination, buffer, length) != length) {
      Close(source);
      Close(destination);
      return -1;
    }
  }

  Close(source);
  if (Close(destination) == DOSFALSE) {
    return -1;
  }
  return length < 0 ? -1 : 0;
}

/* Listing rewrite helpers */
static int file_ops_restore_with_backup(const char *backup_path, const char *target_path)
{
  char restore_temp_path[256];

  if ((backup_path == NULL) || (*backup_path == '\0') ||
      (target_path == NULL) || (*target_path == '\0')) {
    return 0;
  }

  snprintf(restore_temp_path, sizeof(restore_temp_path), "%s.afdrst", target_path);
  if (file_ops_copy_file(backup_path, restore_temp_path) != 0) {
    return 0;
  }

  return file_ops_replace_with_temp(restore_temp_path, target_path, NULL) == 0;
}

static int file_ops_remove_if_exists(const char *path, int *error_code)
{
  BPTR lock;

  if ((path == NULL) || (*path == '\0')) {
    return -1;
  }

  lock = Lock((STRPTR) path, ACCESS_READ);
  if (lock != 0) {
    UnLock(lock);
  } else if (IoErr() == ERROR_OBJECT_NOT_FOUND) {
    return 0;
  }

  if (DeleteFile((STRPTR) path) != DOSFALSE) {
    return 0;
  }

  if (error_code != NULL) {
    *error_code = (int) IoErr();
  }
  return -1;
}

static int file_ops_replace_with_temp(const char *temp_path, const char *target_path, int *error_code)
{
  if ((temp_path == NULL) || (*temp_path == '\0') ||
      (target_path == NULL) || (*target_path == '\0')) {
    return -1;
  }

  if (file_ops_remove_if_exists(target_path, error_code) != 0) {
    return -1;
  }

  if (Rename((STRPTR) temp_path, (STRPTR) target_path) == DOSFALSE) {
    if (error_code != NULL) {
      *error_code = (int) IoErr();
    }
    return -1;
  }

  return 0;
}

static int file_ops_line_is_new_file(const char *text)
{
  char ch;

  if (text == NULL) {
    return 0;
  }

  ch = text[0];
  return (ch != '\0') && (ch != ' ') && (ch != '\n') && (ch != '\r');
}

static void file_ops_extract_filename(const char *line, char *output, size_t output_size)
{
  size_t index;

  if ((line == NULL) || (output == NULL) || (output_size == 0U)) {
    return;
  }

  index = 0;
  while ((line[index] != '\0') && (line[index] != ' ') && (line[index] != '\r') && (line[index] != '\n') && (index + 1U < output_size)) {
    output[index] = line[index];
    index++;
  }
  output[index] = '\0';
}

static int file_ops_extract_entry_block(const char *listing_path,
                                        const char *target_filename,
                                        const char *temp_listing_path,
                                        char *moved_block,
                                        size_t moved_block_size)
{
  FILE *input;
  FILE *output;
  char line[256];
  char line_filename[64];
  int copying_target;
  int found_target;
  size_t used_length;

  if ((listing_path == NULL) || (target_filename == NULL) || (temp_listing_path == NULL) ||
      (moved_block == NULL) || (moved_block_size == 0U)) {
    return -1;
  }

  moved_block[0] = '\0';
  input = fopen(listing_path, "r");
  if (input == NULL) {
    return -1;
  }

  output = fopen(temp_listing_path, "w");
  if (output == NULL) {
    fclose(input);
    return -1;
  }

  copying_target = 0;
  found_target = 0;
  while (fgets(line, sizeof(line), input) != NULL) {
    if (file_ops_line_is_new_file(line)) {
      file_ops_extract_filename(line, line_filename, sizeof(line_filename));
      if (!found_target && (strcmp(line_filename, target_filename) == 0)) {
        copying_target = 1;
        found_target = 1;
      } else {
        copying_target = 0;
      }
    }

    if (copying_target) {
      used_length = strlen(moved_block);
      if (used_length + strlen(line) + 1U < moved_block_size) {
        strcat(moved_block, line);
      }
    } else {
      fputs(line, output);
    }
  }

  fclose(input);
  fclose(output);
  return found_target ? 0 : -1;
}

static int file_ops_write_destination_listing(const char *listing_path,
                                              const char *temp_listing_path,
                                              const char *moved_block)
{
  FILE *source;
  FILE *destination;
  char buffer[1024];
  size_t length;

  destination = fopen(temp_listing_path, "w");
  if (destination == NULL) {
    return -1;
  }

  source = fopen(listing_path, "r");
  if (source != NULL) {
    while ((length = fread(buffer, 1U, sizeof(buffer), source)) > 0U) {
      if (fwrite(buffer, 1U, length, destination) != length) {
        fclose(source);
        fclose(destination);
        return -1;
      }
    }
    fclose(source);
  }

  if ((moved_block != NULL) && (*moved_block != '\0')) {
    fputs(moved_block, destination);
    if (moved_block[strlen(moved_block) - 1U] != '\n') {
      fputc('\n', destination);
    }
  }

  fclose(destination);
  return 0;
}

/* Public move operation */
int file_ops_move_selected(const struct dirlist_data *source_dirlist,
                           int selected_entry,
                           const char *source_folder,
                           const char *destination_folder,
                           const char *destination_listing_path,
                           char *error_text,
                           int error_text_size)
{
  char filename[64];
  char source_file_path[256];
  char destination_file_path[256];
  char source_temp_path[256];
  char source_backup_path[256];
  char destination_temp_path[256];
  char destination_backup_path[256];
  char moved_block[4096];
  int rollback_needed;
  BPTR existing_lock;

  if ((source_dirlist == NULL) || (selected_entry < 0) || (selected_entry >= source_dirlist->entry_count) ||
      (source_folder == NULL) || (*source_folder == '\0') ||
      (destination_folder == NULL) || (*destination_folder == '\0') ||
      (destination_listing_path == NULL) || (*destination_listing_path == '\0')) {
    file_ops_set_error(error_text, error_text_size, "invalid move request");
    return -1;
  }

  strncpy(filename, source_dirlist->entries[selected_entry].filename, sizeof(filename) - 1U);
  filename[sizeof(filename) - 1U] = '\0';
  if (filename[0] == '\0') {
    file_ops_set_error(error_text, error_text_size, "selected entry has no filename");
    return -1;
  }

  file_ops_join_path(source_file_path, sizeof(source_file_path), source_folder, filename);
  file_ops_join_path(destination_file_path, sizeof(destination_file_path), destination_folder, filename);
  if (strcmp(source_file_path, destination_file_path) == 0) {
    file_ops_set_error(error_text, error_text_size, "source and destination are the same");
    return -1;
  }

  existing_lock = Lock((STRPTR) destination_file_path, ACCESS_READ);
  if (existing_lock != 0) {
    UnLock(existing_lock);
    file_ops_set_error(error_text, error_text_size, "destination file already exists");
    return -1;
  }

  snprintf(source_temp_path, sizeof(source_temp_path), "%s.afdtmp", source_dirlist->listing_path);
  snprintf(source_backup_path, sizeof(source_backup_path), "%s.afdbak", source_dirlist->listing_path);
  snprintf(destination_temp_path, sizeof(destination_temp_path), "%s.afdtmp", destination_listing_path);
  snprintf(destination_backup_path, sizeof(destination_backup_path), "%s.afdbak", destination_listing_path);

  if (file_ops_extract_entry_block(source_dirlist->listing_path,
                                   filename,
                                   source_temp_path,
                                   moved_block,
                                   sizeof(moved_block)) != 0) {
    file_ops_set_error(error_text, error_text_size, "source listing entry could not be extracted");
    return -1;
  }

  if (file_ops_write_destination_listing(destination_listing_path, destination_temp_path, moved_block) != 0) {
    int saved_errno;

    saved_errno = errno;
    remove(source_temp_path);
    file_ops_set_runtime_error(error_text, error_text_size, "destination listing could not be staged", saved_errno);
    return -1;
  }

  if (file_ops_copy_file(source_dirlist->listing_path, source_backup_path) != 0) {
    int saved_errno;

    saved_errno = errno;
    remove(source_temp_path);
    remove(destination_temp_path);
    file_ops_set_runtime_error(error_text, error_text_size, "source listing backup failed", saved_errno);
    return -1;
  }

  rollback_needed = 0;
  existing_lock = Lock((STRPTR) destination_listing_path, ACCESS_READ);
  if (existing_lock != 0) {
    UnLock(existing_lock);
    if (file_ops_copy_file(destination_listing_path, destination_backup_path) != 0) {
      LONG saved_ioerr;

      saved_ioerr = IoErr();
      remove(source_temp_path);
      remove(destination_temp_path);
      remove(source_backup_path);
      file_ops_set_dos_error(error_text, error_text_size, "destination listing backup failed", saved_ioerr);
      return -1;
    }
    rollback_needed = 1;
  }

  if (Rename((STRPTR) source_file_path, (STRPTR) destination_file_path) == DOSFALSE) {
    LONG saved_ioerr;

    saved_ioerr = IoErr();
    remove(source_temp_path);
    remove(destination_temp_path);
    remove(source_backup_path);
    if (rollback_needed) {
      remove(destination_backup_path);
    }
    file_ops_set_dos_error(error_text, error_text_size, "file move failed", saved_ioerr);
    return -1;
  }

  {
    int rewrite_errno;
    if (file_ops_replace_with_temp(source_temp_path, source_dirlist->listing_path, &rewrite_errno) != 0) {
      int rollback_ok;

      rollback_ok = (Rename((STRPTR) destination_file_path, (STRPTR) source_file_path) != DOSFALSE) &&
                    file_ops_restore_with_backup(source_backup_path, source_dirlist->listing_path);
      if (rollback_needed) {
        rollback_ok = rollback_ok && file_ops_restore_with_backup(destination_backup_path, destination_listing_path);
      }
      if (rollback_ok) {
        remove(source_temp_path);
        remove(destination_temp_path);
        remove(source_backup_path);
        if (rollback_needed) {
          remove(destination_backup_path);
        }
        file_ops_set_dos_error(error_text,
                               error_text_size,
                               "source listing rewrite failed; move was rolled back",
                               (LONG) rewrite_errno);
      } else {
        file_ops_set_dos_error(error_text,
                               error_text_size,
                               "source listing rewrite failed; rollback needs checking; .afdbak/.afdtmp kept",
                               (LONG) rewrite_errno);
      }
      return -1;
    }
  }
  {
    int rewrite_errno;
    if (file_ops_replace_with_temp(destination_temp_path, destination_listing_path, &rewrite_errno) != 0) {
      int rollback_ok;

      rollback_ok = (Rename((STRPTR) destination_file_path, (STRPTR) source_file_path) != DOSFALSE) &&
                    file_ops_restore_with_backup(source_backup_path, source_dirlist->listing_path);
      if (rollback_needed) {
        rollback_ok = rollback_ok && file_ops_restore_with_backup(destination_backup_path, destination_listing_path);
      }
      if (rollback_ok) {
        remove(source_temp_path);
        remove(destination_temp_path);
        remove(source_backup_path);
        if (rollback_needed) {
          remove(destination_backup_path);
        }
        file_ops_set_dos_error(error_text,
                               error_text_size,
                               "destination listing rewrite failed; move was rolled back",
                               (LONG) rewrite_errno);
      } else {
        file_ops_set_dos_error(error_text,
                               error_text_size,
                               "destination listing rewrite failed; rollback needs checking; .afdbak/.afdtmp kept",
                               (LONG) rewrite_errno);
      }
      return -1;
    }
  }

  remove(source_temp_path);
  remove(destination_temp_path);
  remove(source_backup_path);
  if (rollback_needed) {
    remove(destination_backup_path);
  }

  if (error_text != NULL) {
    error_text[0] = '\0';
  }
  return 0;
}
