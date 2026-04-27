/*
 * FAT32 Filesystem Reader for AurionOS
 * Full read-only implementation for accessing USB drives and FAT32 partitions
 */

#include <stdint.h>
#include <stdbool.h>

extern int disk_read_lba_hdd(uint32_t lba, uint32_t count, void *buffer);
extern void c_puts(const char *s);

#define FAT_SECTOR_SIZE 512
#define FAT_MAX_PATH 256

static void fat32_debug_print_hex(uint32_t val)
{
    char buf[16];
    int i = 0;
    c_puts("0x");
    if (val == 0)
    {
        c_puts("0");
        return;
    }
    while (val > 0 && i < 15)
    {
        uint32_t digit = val % 16;
        buf[i++] = (digit < 10) ? ('0' + digit) : ('A' + digit - 10);
        val /= 16;
    }
    while (i > 0)
    {
        c_puts(buf + i - 1);
        i--;
    }
}

static void fat32_debug_print_num(uint32_t val)
{
    char buf[16];
    int i = 0;
    if (val == 0)
    {
        c_puts("0");
        return;
    }
    while (val > 0 && i < 15)
    {
        buf[i++] = '0' + (val % 10);
        val /= 10;
    }
    while (i > 0)
    {
        c_puts(buf + i - 1);
        i--;
    }
}

typedef struct
{
    uint8_t jump[3];
    uint8_t oem_name[8];
    uint16_t bytes_per_sector;
    uint8_t sectors_per_cluster;
    uint16_t reserved_sectors;
    uint8_t num_fats;
    uint16_t root_entry_count;
    uint16_t total_sectors_16;
    uint8_t media_type;
    uint16_t sectors_per_fat_16;
    uint16_t sectors_per_track;
    uint16_t num_heads;
    uint32_t hidden_sectors;
    uint32_t total_sectors_32;
    uint32_t sectors_per_fat_32;
    uint16_t fs_flags;
    uint16_t version;
    uint32_t root_cluster;
    uint16_t fs_info_sector;
    uint16_t backup_boot_sector;
    uint8_t reserved[12];
    uint8_t drive_number;
    uint8_t flags;
    uint8_t signature;
    uint32_t volume_id;
    uint8_t volume_label[11];
    uint8_t fs_type[8];
} __attribute__((packed)) FAT32_BPB;

typedef struct
{
    char name[8];
    char ext[3];
    uint8_t attributes;
    uint8_t reserved;
    uint8_t creation_time_tenths;
    uint16_t creation_time;
    uint16_t creation_date;
    uint16_t last_access_date;
    uint16_t first_cluster_high;
    uint16_t last_write_time;
    uint16_t last_write_date;
    uint16_t first_cluster_low;
    uint32_t file_size;
} __attribute__((packed)) FAT32_DirEntry;

typedef struct
{
    uint32_t sectors_per_fat;
    uint32_t fat_start_lba;
    uint32_t data_start_lba;
    uint32_t cluster_size;
    uint32_t root_cluster;
    uint32_t total_clusters;
    char volume_label[12];
    bool valid;
    bool mounted;
} FAT32_MountInfo;

static FAT32_MountInfo g_fat = {0};
static uint8_t fat_sector_buffer[FAT_SECTOR_SIZE];
static uint8_t fat_dir_buffer[FAT_SECTOR_SIZE * 8];

static uint32_t fat_read_sector(uint32_t lba)
{
    if (disk_read_lba_hdd(lba, 1, fat_sector_buffer) != 0)
    {
        return 0xFFFFFFFF;
    }
    return 0;
}

static uint32_t fat_get_fat_entry(uint32_t cluster)
{
    if (!g_fat.valid)
        return 0xFFFFFFFF;

    uint32_t fat_offset = cluster * 4;
    uint32_t sector_offset = fat_offset / FAT_SECTOR_SIZE;
    uint32_t entry_offset = fat_offset % FAT_SECTOR_SIZE;

    uint32_t fat_lba = g_fat.fat_start_lba + sector_offset;
    if (fat_read_sector(fat_lba) != 0)
        return 0xFFFFFFFF;

    uint32_t entry = *(uint32_t *)(fat_sector_buffer + entry_offset);
    return entry & 0x0FFFFFFF;
}

static uint32_t fat_cluster_to_lba(uint32_t cluster)
{
    if (!g_fat.valid || cluster < 2)
        return 0;
    return g_fat.data_start_lba + ((uint32_t)(cluster - 2)) * g_fat.cluster_size;
}

static void fat_to_filename(const char *fat_name, char *out_name, int out_size)
{
    int j = 0;
    for (int i = 0; i < 8 && j < out_size - 1; i++)
    {
        if (fat_name[i] != ' ' && fat_name[i] != 0)
        {
            out_name[j++] = fat_name[i];
        }
    }
    if (fat_name[0] != ' ' && fat_name[0] != 0 && fat_name[8] != ' ' && fat_name[8] != 0)
    {
        out_name[j++] = '.';
    }
    for (int i = 8; i < 11 && j < out_size - 1; i++)
    {
        if (fat_name[i] != ' ' && fat_name[i] != 0)
        {
            out_name[j++] = fat_name[i];
        }
    }
    out_name[j] = '\0';
}

static bool fat_filename_match(const char *entry_name, const char *filename)
{
    bool match = true;
    const char *f = filename;
    const char *e = entry_name;

    while (*f && *e)
    {
        char a = *f;
        char b = *e;
        if (a >= 'A' && a <= 'Z')
            a += 32;
        if (b >= 'A' && b <= 'Z')
            b += 32;
        if (a != b)
        {
            match = false;
            break;
        }
        f++;
        e++;
    }
    if (*f != 0 || *e != 0)
        match = false;

    return match;
}

static bool fat_read_one_cluster(uint32_t cluster, uint8_t *buffer)
{
    uint32_t lba = fat_cluster_to_lba(cluster);
    if (lba == 0)
        return false;

    for (uint32_t s = 0; s < g_fat.cluster_size; s++)
    {
        if (disk_read_lba_hdd(lba + s, 1, buffer + s * FAT_SECTOR_SIZE) != 0)
        {
            return false;
        }
    }
    return true;
}

static bool fat_read_dir_cluster(uint32_t cluster)
{
    return fat_read_one_cluster(cluster, fat_dir_buffer);
}

static FAT32_DirEntry *fat_find_in_buffer(const char *filename, int *out_index)
{
    int entries_per_cluster = (g_fat.cluster_size * FAT_SECTOR_SIZE) / sizeof(FAT32_DirEntry);

    for (int i = 0; i < entries_per_cluster; i++)
    {
        FAT32_DirEntry *entry = (FAT32_DirEntry *)(fat_dir_buffer + i * sizeof(FAT32_DirEntry));

        if (entry->name[0] == 0x00)
        {
            return NULL;
        }
        if (entry->name[0] == 0xE5)
        {
            continue;
        }
        if ((entry->attributes & 0x08) != 0)
        {
            continue;
        }

        char entry_name[13];
        fat_to_filename(entry->name, entry_name, 13);

        if (fat_filename_match(entry_name, filename))
        {
            if (out_index)
                *out_index = i;
            return entry;
        }
    }
    return NULL;
}

static FAT32_DirEntry *fat_search_directory(uint32_t start_cluster, const char *filename, int *out_index)
{
    uint32_t cluster = start_cluster;
    int visited = 0;

    while (cluster < 0x0FFFFFF8 && visited < 65536)
    {
        if (!fat_read_dir_cluster(cluster))
        {
            break;
        }

        FAT32_DirEntry *entry = fat_find_in_buffer(filename, out_index);
        if (entry != NULL)
        {
            return entry;
        }

        cluster = fat_get_fat_entry(cluster);
        visited++;
    }

    return NULL;
}

bool fat32_mount(uint32_t start_lba)
{
    g_fat.valid = false;
    g_fat.mounted = false;

    if (fat_read_sector(start_lba) != 0)
    {
        c_puts("[FAT32] Failed to read boot sector\n");
        return false;
    }

    FAT32_BPB *bpb = (FAT32_BPB *)fat_sector_buffer;

    if (bpb->signature != 0x29 && bpb->signature != 0x28)
    {
        c_puts("[FAT32] Invalid boot sector signature: ");
        fat32_debug_print_hex(bpb->signature);
        c_puts("\n");
        return false;
    }

    if (bpb->bytes_per_sector != 512)
    {
        c_puts("[FAT32] Only 512 bytes/sector supported\n");
        return false;
    }

    g_fat.sectors_per_fat = bpb->sectors_per_fat_32;
    g_fat.fat_start_lba = start_lba + bpb->reserved_sectors;
    g_fat.data_start_lba = g_fat.fat_start_lba + (bpb->num_fats * bpb->sectors_per_fat_32);
    g_fat.cluster_size = bpb->sectors_per_cluster;
    g_fat.root_cluster = bpb->root_cluster;
    g_fat.total_clusters = (bpb->total_sectors_32 - bpb->reserved_sectors - (bpb->num_fats * bpb->sectors_per_fat_32)) / bpb->sectors_per_cluster;

    for (int i = 0; i < 11 && i < 12; i++)
    {
        g_fat.volume_label[i] = bpb->volume_label[i];
    }
    g_fat.volume_label[11] = '\0';

    g_fat.valid = true;
    g_fat.mounted = true;

    c_puts("[FAT32] Mounted successfully\n");
    c_puts("[FAT32] Volume: ");
    c_puts(g_fat.volume_label);
    c_puts("\n[FAT32] Total clusters: ");
    fat32_debug_print_num(g_fat.total_clusters);
    c_puts("\n[FAT32] Cluster size: ");
    fat32_debug_print_num(g_fat.cluster_size);
    c_puts(" sectors\n");

    return true;
}

bool fat32_is_mounted(void)
{
    return g_fat.mounted && g_fat.valid;
}

const char *fat32_get_volume_label(void)
{
    return g_fat.volume_label;
}

bool fat32_find_file(const char *filename, uint32_t *out_cluster, uint32_t *out_size)
{
    if (!g_fat.valid)
        return false;

    FAT32_DirEntry *entry = fat_search_directory(g_fat.root_cluster, filename, NULL);

    if (entry == NULL)
    {
        return false;
    }

    uint32_t first_cluster = ((uint32_t)entry->first_cluster_high << 16) | entry->first_cluster_low;

    if (out_cluster)
        *out_cluster = first_cluster;
    if (out_size)
        *out_size = entry->file_size;

    return true;
}

int fat32_read_file(uint32_t start_cluster, uint32_t offset, uint32_t size, void *buffer)
{
    if (!g_fat.valid)
        return -1;
    if (start_cluster == 0 || start_cluster >= 0x0FFFFFF8)
        return -1;
    if (size == 0)
        return 0;

    uint32_t cluster_bytes = g_fat.cluster_size * FAT_SECTOR_SIZE;
    uint32_t cluster = start_cluster;
    uint32_t bytes_read = 0;
    uint32_t current_offset = 0;
    int visited = 0;

    while (cluster < 0x0FFFFFF8 && bytes_read < size && visited < 65536)
    {
        uint32_t cluster_start = current_offset;
        uint32_t cluster_end = current_offset + cluster_bytes;

        if (offset < cluster_end && current_offset < offset)
        {
            uint32_t skip_in_cluster = offset - current_offset;
            current_offset = offset;
            cluster = fat_get_fat_entry(cluster);
            visited++;
            if (skip_in_cluster > 0 && cluster < 0x0FFFFFF8)
            {
                uint8_t temp[FAT_SECTOR_SIZE * 64];
                uint32_t lba = fat_cluster_to_lba(cluster);
                uint32_t sector_in_cluster = skip_in_cluster / FAT_SECTOR_SIZE;
                uint32_t byte_in_sector = skip_in_cluster % FAT_SECTOR_SIZE;

                if (disk_read_lba_hdd(lba + sector_in_cluster, 1, temp) != 0)
                {
                    break;
                }

                uint32_t to_skip = FAT_SECTOR_SIZE - byte_in_sector;
                if (to_skip > size - bytes_read)
                    to_skip = size - bytes_read;

                for (uint32_t i = 0; i < to_skip; i++)
                {
                    ((uint8_t *)buffer)[bytes_read++] = temp[byte_in_sector + i];
                }

                if (bytes_read >= size)
                    break;
            }
            continue;
        }

        if (current_offset >= offset && bytes_read < size)
        {
            uint32_t lba = fat_cluster_to_lba(cluster);

            for (uint32_t s = 0; s < g_fat.cluster_size && bytes_read < size; s++)
            {
                uint32_t sector_lba = lba + s;
                uint32_t to_read = FAT_SECTOR_SIZE;
                if (bytes_read + to_read > size)
                {
                    to_read = size - bytes_read;
                }

                uint8_t temp[FAT_SECTOR_SIZE];
                if (disk_read_lba_hdd(sector_lba, 1, temp) != 0)
                {
                    return bytes_read > 0 ? (int)bytes_read : -1;
                }

                for (uint32_t i = 0; i < to_read; i++)
                {
                    ((uint8_t *)buffer)[bytes_read++] = temp[i];
                }
            }
        }

        current_offset += cluster_bytes;
        cluster = fat_get_fat_entry(cluster);
        visited++;
    }

    return (int)bytes_read;
}

bool fat32_list_directory(const char *dirname, void (*callback)(const char *name, bool is_dir, uint32_t size))
{
    if (!g_fat.valid)
        return false;

    uint32_t dir_cluster;
    uint32_t dir_size;

    if (dirname == NULL || dirname[0] == '\0' || (dirname[0] == '/' && dirname[1] == '\0'))
    {
        dir_cluster = g_fat.root_cluster;
        dir_size = 0;
    }
    else
    {
        if (!fat32_find_file(dirname, &dir_cluster, &dir_size))
        {
            c_puts("[FAT32] Directory not found: ");
            c_puts(dirname);
            c_puts("\n");
            return false;
        }
    }

    uint32_t cluster = dir_cluster;
    int visited = 0;

    while (cluster < 0x0FFFFFF8 && visited < 65536)
    {
        uint32_t lba = fat_cluster_to_lba(cluster);
        if (lba == 0)
            break;

        for (uint32_t s = 0; s < g_fat.cluster_size; s++)
        {
            if (disk_read_lba_hdd(lba + s, 1, fat_dir_buffer + s * FAT_SECTOR_SIZE) != 0)
            {
                c_puts("[FAT32] Failed to read directory cluster\n");
                return false;
            }
        }

        int entries_per_sector = FAT_SECTOR_SIZE / sizeof(FAT32_DirEntry);
        int entries_per_cluster = entries_per_sector * g_fat.cluster_size;

        for (int i = 0; i < entries_per_cluster; i++)
        {
            FAT32_DirEntry *entry = (FAT32_DirEntry *)(fat_dir_buffer + i * sizeof(FAT32_DirEntry));

            if (entry->name[0] == 0x00)
            {
                return true;
            }
            if (entry->name[0] == 0xE5)
            {
                continue;
            }
            if ((entry->attributes & 0x08) != 0)
            {
                continue;
            }

            char entry_name[13];
            fat_to_filename(entry->name, entry_name, 13);

            if (entry_name[0] == '\0')
                continue;

            bool is_dir = (entry->attributes & 0x10) != 0;
            uint32_t first_cluster = ((uint32_t)entry->first_cluster_high << 16) | entry->first_cluster_low;
            uint32_t file_size = is_dir ? 0 : entry->file_size;

            callback(entry_name, is_dir, file_size);
        }

        cluster = fat_get_fat_entry(cluster);
        visited++;
    }

    return true;
}

bool fat32_read_file_by_name(const char *filename, void *buffer, uint32_t max_size, uint32_t *out_actual_size)
{
    if (!g_fat.valid)
        return false;

    uint32_t cluster;
    uint32_t file_size;

    if (!fat32_find_file(filename, &cluster, &file_size))
    {
        c_puts("[FAT32] File not found: ");
        c_puts(filename);
        c_puts("\n");
        return false;
    }

    if (out_actual_size)
        *out_actual_size = file_size;

    uint32_t to_read = file_size;
    if (to_read > max_size)
        to_read = max_size;

    int result = fat32_read_file(cluster, 0, to_read, buffer);

    return (result >= 0);
}

uint32_t fat32_get_file_size_by_name(const char *filename)
{
    if (!g_fat.valid)
        return 0;

    uint32_t cluster;
    uint32_t file_size;

    if (!fat32_find_file(filename, &cluster, &file_size))
    {
        return 0;
    }

    return file_size;
}

bool fat32_unmount(void)
{
    if (!g_fat.valid)
        return false;

    g_fat.mounted = false;
    g_fat.valid = false;

    c_puts("[FAT32] Unmounted\n");
    return true;
}
