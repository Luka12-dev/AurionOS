/*
 * ISO 9660 CD-ROM Filesystem Reader
 */

#ifndef ISO9660_H
#define ISO9660_H

#include <stdint.h>
#include <stdbool.h>

/* Find a file in the ISO root directory */
bool iso9660_find_file(const char *filename, uint32_t *out_lba, uint32_t *out_size);

/* Read a file from ISO into a buffer */
int iso9660_read_file(const char *filename, void *buffer, uint32_t max_size);

#endif
