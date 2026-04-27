/*
 * ISO 9660 CD-ROM Filesystem Reader
 * Minimal implementation to read files from ISO
 */

#include <stdint.h>
#include <stdbool.h>

extern int disk_read_lba_cdrom(uint32_t lba, uint32_t count, void *buffer);
extern void c_puts(const char *s);

#define ISO_SECTOR_SIZE 2048
#define ISO_PRIMARY_VOL_DESC_LBA 16

typedef struct {
    uint8_t  length;
    uint8_t  ext_attr_length;
    uint32_t extent_lba_le;
    uint32_t extent_lba_be;
    uint32_t size_le;
    uint32_t size_be;
    uint8_t  date[7];
    uint8_t  flags;
    uint8_t  file_unit_size;
    uint8_t  interleave;
    uint16_t volume_seq_le;
    uint16_t volume_seq_be;
    uint8_t  name_len;
    char     name[1];
} __attribute__((packed)) ISO9660DirEntry;

static char iso_shared_buffer[ISO_SECTOR_SIZE];
static char iso_root_buffer[ISO_SECTOR_SIZE];
static char iso_clean_name[128];

/* Simple string compare */
static int iso_strcmp(const char *a, const char *b, int len) {
    for (int i = 0; i < len; i++) {
        char ca = a[i];
        char cb = b[i];
        if (ca >= 'a' && ca <= 'z') ca -= 32;
        if (cb >= 'a' && cb <= 'z') cb -= 32;
        if (ca != cb) return ca - cb;
        if (ca == 0) return 0;
    }
    return 0;
}

/* Help find a file in a specific directory LBA/size */
static bool iso9660_find_in_dir(uint32_t dir_lba, uint32_t dir_size, const char *filename, uint32_t *out_lba, uint32_t *out_size) {
    uint32_t sectors_total = (dir_size + 2047) / 2048;
    if (sectors_total > 32) sectors_total = 32;

    for (uint32_t s = 0; s < sectors_total; s++) {
        if (disk_read_lba_cdrom((dir_lba + s) * 4, 4, iso_shared_buffer) != 0) {
            c_puts("[ISO] Disk read error in dir traversal\n");
            break;
        }

        uint32_t offset = 0;
        while (offset < 2048) {
            ISO9660DirEntry *entry = (ISO9660DirEntry *)(iso_shared_buffer + offset);
            if (entry->length == 0) break;

            if (entry->name_len > 0) {
                /* Extract clean name (strip ;1 suffix) */
                int clean_len = 0;
                for (int k = 0; k < entry->name_len && k < 127; k++) {
                    if (entry->name[k] == ';') break;
                    /* Strip trailing dot if present */
                    if (entry->name[k] == '.' && (k == entry->name_len - 1 || entry->name[k+1] == ';')) break;
                    iso_clean_name[k] = entry->name[k];
                    clean_len++;
                }
                iso_clean_name[clean_len] = 0;
                
                c_puts("[ISO] Matching: "); c_puts(iso_clean_name); c_puts(" vs "); c_puts(filename); c_puts("\n");

                /* Case-insensitive string match */
                bool found = true;
                const char *f = filename;
                const char *c = iso_clean_name;
                while (*f && *c) {
                    char a = *f;
                    char b = *c;
                    if (a >= 'a' && a <= 'z') a -= 32;
                    if (b >= 'a' && b <= 'z') b -= 32;
                    if (a != b) { found = false; break; }
                    f++; c++;
                }
                if (*f != 0 || *c != 0) found = false;

                if (found) {
                    *out_lba = entry->extent_lba_le;
                    *out_size = entry->size_le;
                    return true;
                } else {
                     c_puts("[ISO] Found: "); c_puts(iso_clean_name); c_puts("\n");
                }
            }
            offset += entry->length;
        }
    }
    return false;
}

/* Find file with optional directory traversal (e.g. "Wallpaper/Wallpaper1.bmp") */
bool iso9660_find_file(const char *path, uint32_t *out_lba, uint32_t *out_size) {
    if (disk_read_lba_cdrom(ISO_PRIMARY_VOL_DESC_LBA * 4, 4, iso_root_buffer) != 0) return false;
    
    if (iso_root_buffer[1] != 'C' || iso_root_buffer[2] != 'D' || 
        iso_root_buffer[3] != '0' || iso_root_buffer[4] != '0' || 
        iso_root_buffer[5] != '1') return false;

    ISO9660DirEntry *root = (ISO9660DirEntry *)(iso_root_buffer + 156);
    uint32_t cur_lba = root->extent_lba_le;
    uint32_t cur_size = root->size_le;

    const char *p = path;
    if (*p == '/') p++;

    while (*p) {
        char component[64];
        int ci = 0;
        while (*p && *p != '/' && ci < 63) component[ci++] = *p++;
        component[ci] = 0;

        if (*p == '/') p++; /* skip slash */

        uint32_t next_lba, next_size;
        if (!iso9660_find_in_dir(cur_lba, cur_size, component, &next_lba, &next_size))
            return false;

        if (*p == 0) {
            /* Found the final file */
            *out_lba = next_lba;
            *out_size = next_size;
            return true;
        }

        /* Traverse into directory */
        cur_lba = next_lba;
        cur_size = next_size;
    }

    return false;
}

/* Read full file from ISO into buffer */
int iso9660_read_file(const char *filename, void *buffer, uint32_t max_size) {
    uint32_t lba, size;
    if (!iso9660_find_file(filename, &lba, &size)) return -1;

    if (size > max_size) size = max_size;
    
    uint32_t sectors_512 = (size + 511) / 512;
    if (disk_read_lba_cdrom(lba * 4, sectors_512, buffer) != 0) return -1;
    
    return (int)size;
}
