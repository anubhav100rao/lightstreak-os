/*
 * tools/mkramfs.c — Host-side tool: pack a directory into initramfs.img
 *
 * Compiled with the HOST compiler (cc), NOT cross-compiled.
 *
 * Usage: ./mkramfs <input_dir> <output_file>
 *
 * Format:
 *   Header:
 *     uint32_t magic       = 0x52414D46 ("RAMF")
 *     uint32_t file_count
 *   Per file entry (file_count entries):
 *     char     name[64]    (null-padded)
 *     uint32_t offset      (from start of image)
 *     uint32_t size
 *   Raw file data at the specified offsets.
 */

#include <dirent.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#define MAGIC 0x52414D46u
#define NAME_LEN 64
#define MAX_FILES 64

typedef struct {
  char name[NAME_LEN];
  uint32_t offset;
  uint32_t size;
  uint8_t *data;
} file_entry_t;

int main(int argc, char *argv[]) {
  if (argc != 3) {
    fprintf(stderr, "Usage: %s <input_dir> <output_file>\n", argv[0]);
    return 1;
  }

  const char *input_dir = argv[1];
  const char *output_file = argv[2];

  DIR *dir = opendir(input_dir);
  if (!dir) {
    perror("opendir");
    return 1;
  }

  file_entry_t files[MAX_FILES];
  int file_count = 0;

  struct dirent *ent;
  while ((ent = readdir(dir)) != NULL && file_count < MAX_FILES) {
    if (ent->d_name[0] == '.')
      continue; /* Skip . and .. */

    char path[512];
    snprintf(path, sizeof(path), "%s/%s", input_dir, ent->d_name);

    struct stat st;
    if (stat(path, &st) != 0 || !S_ISREG(st.st_mode))
      continue;

    /* Read file into memory */
    FILE *f = fopen(path, "rb");
    if (!f) {
      perror(path);
      continue;
    }

    files[file_count].data = (uint8_t *)malloc((size_t)st.st_size);
    if (!files[file_count].data) {
      fclose(f);
      continue;
    }

    fread(files[file_count].data, 1, (size_t)st.st_size, f);
    fclose(f);

    memset(files[file_count].name, 0, NAME_LEN);
    strncpy(files[file_count].name, ent->d_name, NAME_LEN - 1);
    files[file_count].size = (uint32_t)st.st_size;
    file_count++;

    printf("  Added: %s (%u bytes)\n", ent->d_name, (uint32_t)st.st_size);
  }
  closedir(dir);

  /* Calculate offsets: header(8) + entries(72 each) + data */
  uint32_t header_size = 8 + (uint32_t)(file_count * 72);
  uint32_t data_offset = header_size;

  for (int i = 0; i < file_count; i++) {
    files[i].offset = data_offset;
    data_offset += files[i].size;
  }

  /* Write output file */
  FILE *out = fopen(output_file, "wb");
  if (!out) {
    perror("fopen output");
    return 1;
  }

  /* Header */
  uint32_t magic = MAGIC;
  uint32_t count = (uint32_t)file_count;
  fwrite(&magic, 4, 1, out);
  fwrite(&count, 4, 1, out);

  /* Entry table */
  for (int i = 0; i < file_count; i++) {
    fwrite(files[i].name, NAME_LEN, 1, out);
    fwrite(&files[i].offset, 4, 1, out);
    fwrite(&files[i].size, 4, 1, out);
  }

  /* File data */
  for (int i = 0; i < file_count; i++) {
    fwrite(files[i].data, 1, files[i].size, out);
    free(files[i].data);
  }

  fclose(out);
  printf("Created %s: %d files, %u bytes total\n", output_file, file_count,
         data_offset);
  return 0;
}
