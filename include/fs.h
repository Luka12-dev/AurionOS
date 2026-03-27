#ifndef FS_H
#define FS_H

#include <stdint.h>

#define FS_MAX_FILENAME 56
#define FS_MAX_FILES 128
#define MAX_FILE_SIZE 8192

typedef struct
{
  char name[FS_MAX_FILENAME];
  uint32_t size;
  uint8_t type; /* 0=file, 1=dir */
  uint8_t attr;
  uint16_t parent_idx;
  uint16_t reserved;
} FSEntry;

typedef struct
{
  uint16_t file_idx;
  uint32_t size;
  char data[MAX_FILE_SIZE];
} FileContent;

#endif
