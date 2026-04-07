/*
 * tooltype_dump.c
 *
 * Small Amiga CLI that opens a Workbench object via icon.library and
 * prints the raw tooltypes exactly as returned in the DiskObject structure.
 *
 * This helps investigate odd stored keys such as "$DLPATH.2".
 */
#include <exec/types.h>
#include <proto/exec.h>
#include <proto/icon.h>
#include <workbench/workbench.h>

#include <stdio.h>
#include <string.h>

struct Library *IconBase = NULL;

/* Small local helpers */
static int text_ends_with_info(const char *text)
{
  size_t length;

  if (text == NULL) {
    return 0;
  }

  length = strlen(text);
  if (length < 5U) {
    return 0;
  }

  text += length - 5U;
  return ((text[0] == '.') &&
          ((text[1] == 'i') || (text[1] == 'I')) &&
          ((text[2] == 'n') || (text[2] == 'N')) &&
          ((text[3] == 'f') || (text[3] == 'F')) &&
          ((text[4] == 'o') || (text[4] == 'O')));
}

static void make_diskobject_path(char *output, size_t output_size, const char *input_path)
{
  size_t length;

  if ((output == NULL) || (output_size == 0U)) {
    return;
  }

  output[0] = '\0';
  if (input_path == NULL) {
    return;
  }

  strncpy(output, input_path, output_size - 1U);
  output[output_size - 1U] = '\0';

  if (text_ends_with_info(output)) {
    length = strlen(output);
    output[length - 5U] = '\0';
  }
}

/* Program start */
int main(int argc, char **argv)
{
  struct DiskObject *disk_object;
  STRPTR *tooltypes;
  char object_path[512];
  int index;

  puts("ARBFILES tooltype dump");
  puts("");

  if (argc < 2) {
    puts("Usage:");
    puts("  tooltype_dump <Workbench object or .info path>");
    return 10;
  }

  make_diskobject_path(object_path, sizeof(object_path), argv[1]);

  IconBase = OpenLibrary("icon.library", 0L);
  if (IconBase == NULL) {
    puts("Error: unable to open icon.library");
    return 20;
  }

  disk_object = GetDiskObject(object_path);
  if (disk_object == NULL) {
    printf("Error: GetDiskObject() failed for \"%s\"\n", object_path);
    CloseLibrary(IconBase);
    return 30;
  }

  printf("Input      : %s\n", argv[1]);
  printf("Object path: %s\n", object_path);
  puts("");
  puts("Tooltypes returned by icon.library:");

  tooltypes = disk_object->do_ToolTypes;
  if ((tooltypes == NULL) || (*tooltypes == NULL)) {
    puts("  (none)");
  } else {
    index = 0;
    while (tooltypes[index] != NULL) {
      printf("  [%d] %s\n", index + 1, tooltypes[index]);
      index++;
    }
  }

  FreeDiskObject(disk_object);
  CloseLibrary(IconBase);
  return 0;
}
