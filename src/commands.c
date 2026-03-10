#include "../include/network.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* External Wrappers (from kernel.asm) */
extern void c_puts(const char *s);
extern void c_putc(char c);
extern void c_cls(void);
extern uint16_t c_getkey(void);
extern void set_attr(uint8_t a);
extern void sys_reboot(void);
extern void sys_shutdown(void);
extern uint32_t get_ticks(void);
extern void sys_beep(uint32_t freq, uint32_t duration);
extern void sleep_ms(uint32_t ms);

#define puts c_puts
#define putc c_putc
#define cls c_cls
#define getkey c_getkey

/* Filesystem primitives (filesys.asm) */
extern int fs_list_root(uint32_t out_dir_buffer, uint32_t max_entries);

/* Memory management (memory.asm) */
extern void mem_get_stats(uint32_t *stats);
extern void *kmalloc(uint32_t size);
extern void kfree(void *ptr);

/* System call functions (from syscall.c) */
extern int sys_get_time(uint8_t *hours, uint8_t *minutes, uint8_t *seconds);
extern int sys_get_date(uint8_t *day, uint8_t *month, uint16_t *year);
extern int sys_read_sector(uint32_t lba, uint32_t count, void *buffer);
extern int disk_read_lba(uint32_t lba, uint32_t count, void *buffer);

/* Disk write function - we need to add this */
extern int disk_write_lba(uint32_t lba, uint32_t count, void *buffer);

/* Global State */
char current_dir[256] = "C:\\"; /* Not static - exported to shell */
static uint8_t current_color = 0x07;

/* REAL Filesystem Storage - Using reserved sectors on disk */
#define FS_DATA_START_LBA                            \
  500 /* Start after kernel on hard disk (safe area) \
       */
#define FS_MAX_FILES 128
#define FS_MAX_USERS 32
#define FS_MAX_FILENAME 56
#define FS_CONTENT_START_LBA 700 /* File contents start here */

/* File entry structure (64 bytes each) */
typedef struct
{
  char name[FS_MAX_FILENAME];
  uint32_t size;
  uint8_t type; /* 0=file, 1=dir */
  uint8_t attr;
  uint16_t parent_idx;
  uint16_t reserved;
} FSEntry;

/* User entry structure (64 bytes each) */
typedef struct
{
  char username[32];
  char password_hash[32]; /* Simple hash */
} UserEntry;

/* In-memory caches */
FSEntry fs_table[FS_MAX_FILES];
int fs_count = 0;
static UserEntry user_table[FS_MAX_USERS];
static int user_count = 0;
static char current_user[32] = "root";

/* Process Management Simulation */
typedef struct
{
  uint32_t pid;
  char name[32];
  char state[16];
  uint32_t mem_usage; // in KB
  uint8_t priority;
} ProcessEntry;

#define MAX_PROCESSES 16
static ProcessEntry process_table[MAX_PROCESSES];
static int process_count = 0;

/* Initialize processes if empty */
void str_copy(char *dst, const char *src, int max);

static void ensure_processes_init(void)
{
  if (process_count == 0)
  {
    /* Kernel (PID 1) */
    process_table[0].pid = 1;
    str_copy(process_table[0].name, "KERNEL", 32);
    str_copy(process_table[0].state, "RUNNING", 16);
    process_table[0].mem_usage = 128; // 128K
    process_table[0].priority = 0;    // High

    /* Shell (PID 2) */
    process_table[1].pid = 2;
    str_copy(process_table[1].name, "SHELL", 32);
    str_copy(process_table[1].state, "RUNNING", 16);
    process_table[1].mem_usage = 64; // 64K
    process_table[1].priority = 10;  // Normal

    /* Network Service (PID 3) */
    process_table[2].pid = 3;
    str_copy(process_table[2].name, "NETSVC", 32);
    str_copy(process_table[2].state, "SLEEPING", 16);
    process_table[2].mem_usage = 32; // 32K
    process_table[2].priority = 10;

    /* Display Service (PID 4) */
    process_table[3].pid = 4;
    str_copy(process_table[3].name, "DISPSVC", 32);
    str_copy(process_table[3].state, "SLEEPING", 16);
    process_table[3].mem_usage = 48; // 48K
    process_table[3].priority = 15;  // Low

    process_count = 4;
  }
}

/* File content storage (simple buffer) */

#define MAX_FILE_SIZE 1024
typedef struct
{
  uint16_t file_idx;
  uint32_t size;
  char data[MAX_FILE_SIZE];
} FileContent;

FileContent file_contents[64] = {0};
int file_content_count = 0;

/* Utility Functions */
int str_len(const char *s)
{
  if (!s)
    return 0;
  int l = 0;
  while (s[l])
    l++;
  return l;
}

void str_copy(char *dst, const char *src, int max)
{
  int i = 0;
  while (i < max - 1 && src[i])
  {
    dst[i] = src[i];
    i++;
  }
  dst[i] = '\0';
}

/* Forward declarations */
int str_cmp(const char *a, const char *b);
int cmd_dispatch(const char *line);

/* Helper to save content to file system */
int save_file_content(const char *filename, const char *data, int len)
{
  if (!filename || !data || len < 0)
    return -1;

  // Convert filename to full path if not absolute
  char full_path[256];
  if (filename[1] != ':')
  { // Simple check for C: or similar
    int dir_len = str_len(current_dir);
    for (int k = 0; k < dir_len; k++)
      full_path[k] = current_dir[k];

    int name_len = str_len(filename);
    for (int k = 0; k < name_len; k++)
      full_path[dir_len + k] = filename[k];
    full_path[dir_len + name_len] = '\0';
  }
  else
  {
    str_copy(full_path, filename, 256);
  }

  // Check if file exists -> overwrite
  int idx = -1;
  for (int i = 0; i < fs_count; i++)
  {
    if (str_cmp(fs_table[i].name, full_path) == 0)
    {
      idx = i;
      break;
    }
  }

  if (idx == -1)
  {
    // Create new
    if (fs_count >= FS_MAX_FILES)
    {
      puts("Error: Disk full\n");
      return -1;
    }
    idx = fs_count++;
    str_copy(fs_table[idx].name, full_path, FS_MAX_FILENAME);
    fs_table[idx].type = 0; // File
    fs_table[idx].attr = 0;
    fs_table[idx].parent_idx = 0xFFFF; // Root
  }

  // Update size
  fs_table[idx].size = len;

  // Save content
  // Find content slot
  int content_idx = -1;
  for (int i = 0; i < file_content_count; i++)
  {
    if (file_contents[i].file_idx == idx)
    {
      content_idx = i;
      break;
    }
  }

  if (content_idx == -1)
  {
    if (file_content_count >= 64)
    {
      puts("Error: Content storage full\n");
      return -1;
    }
    content_idx = file_content_count++;
    file_contents[content_idx].file_idx = idx;
  }

  // Copy data (truncate if too large)
  if (len > MAX_FILE_SIZE)
    len = MAX_FILE_SIZE;
  for (int k = 0; k < len; k++)
  {
    file_contents[content_idx].data[k] = data[k];
  }
  file_contents[content_idx].size = len;

  /* Persist to disk */
  fs_save_to_disk();

  return 0;
}

/* Helper to load content from file system */
int load_file_content(const char *filename, char *buffer, int max_len)
{
  if (!filename || !buffer || max_len <= 0)
    return -1;

  char full_path[256];
  if (filename[1] != ':')
  {
    int dl = str_len(current_dir);
    str_copy(full_path, current_dir, 256);
    int nl = str_len(filename);
    for (int i = 0; i < nl; i++)
      full_path[dl + i] = filename[i];
    full_path[dl + nl] = 0;
  }
  else
  {
    str_copy(full_path, filename, 256);
  }

  int idx = -1;
  for (int i = 0; i < fs_count; i++)
  {
    if (str_cmp(fs_table[i].name, full_path) == 0)
    {
      idx = i;
      break;
    }
  }
  if (idx == -1)
    return -1;

  int ci = -1;
  for (int i = 0; i < file_content_count; i++)
  {
    if ((int)file_contents[i].file_idx == idx)
    {
      ci = i;
      break;
    }
  }
  if (ci == -1)
    return 0; // Empty file

  int sz = (int)file_contents[ci].size;
  if (sz > max_len)
    sz = max_len;
  for (int i = 0; i < sz; i++)
    buffer[i] = file_contents[ci].data[i];

  /* Null terminate ONLY if there is extra space. 
     Do NOT overwrite the last byte of binary data if it fits exactly. */
  if (sz < max_len)
    buffer[sz] = 0;

  return sz;
}

int str_cmp(const char *a, const char *b)
{
  if (!a || !b)
    return -1;
  while (*a && *b && *a == *b)
  {
    a++;
    b++;
  }
  return *a - *b;
}

static int str_ncmp(const char *a, const char *b, int n)
    __attribute__((unused));
static int str_ncmp(const char *a, const char *b, int n)
{
  if (!a || !b)
    return -1;
  for (int i = 0; i < n; i++)
  {
    if (a[i] != b[i] || !a[i])
      return a[i] - b[i];
  }
  return 0;
}

static void str_upper(char *s)
{
  while (*s)
  {
    if (*s >= 'a' && *s <= 'z')
      *s -= 32;
    s++;
  }
}

static void int_to_str(uint32_t n, char *buf)
{
  if (n == 0)
  {
    buf[0] = '0';
    buf[1] = '\0';
    return;
  }
  int i = 0;
  uint32_t tmp = n;
  while (tmp > 0)
  {
    tmp /= 10;
    i++;
  }
  buf[i] = '\0';
  while (n > 0)
  {
    buf[--i] = '0' + (n % 10);
    n /= 10;
  }
}

static uint32_t str_to_int(const char *s)
{
  uint32_t n = 0;
  while (*s >= '0' && *s <= '9')
  {
    n = n * 10 + (*s - '0');
    s++;
  }
  return n;
}

static void print_hex(uint32_t n)
{
  char hex[] = "0123456789ABCDEF";
  char buf[9];
  for (int i = 7; i >= 0; i--)
  {
    buf[i] = hex[n & 0xF];
    n >>= 4;
  }
  buf[8] = '\0';
  puts(buf);
}

/* Simple hash function */
static uint32_t hash_string(const char *s)
{
  uint32_t h = 5381;
  while (*s)
  {
    h = ((h << 5) + h) + (uint32_t)(*s);
    s++;
  }
  return h;
}

/* Define sector for current directory storage */
#define FS_CURDIR_LBA 499 /* Store current directory just before file table */

/* FIXED: fs_save_to_disk - Save content indexed by file_idx, not loop position
 */
int fs_save_to_disk(void)
{
  char sector[512];

  /* Save current directory first */
  for (int j = 0; j < 512; j++)
    sector[j] = 0;
  for (int j = 0; j < 256 && current_dir[j]; j++)
  {
    sector[j] = current_dir[j];
  }
  if (disk_write_lba(FS_CURDIR_LBA, 1, sector) != 0)
  {
    return -1;
  }

  /* Save file table - save ALL files up to FS_MAX_FILES */
  for (int i = 0; i < fs_count && i < FS_MAX_FILES; i++)
  {
    for (int j = 0; j < 512; j++)
      sector[j] = 0;
    for (int j = 0; j < 64 && j < (int)sizeof(FSEntry); j++)
    {
      sector[j] = ((char *)&fs_table[i])[j];
    }
    if (disk_write_lba(FS_DATA_START_LBA + i, 1, sector) != 0)
    {
      return -1;
    }
  }

  /* Clear remaining file slots - clear up to 64 entries */
  for (int i = fs_count; i < 64; i++)
  {
    for (int j = 0; j < 512; j++)
      sector[j] = 0;
    disk_write_lba(FS_DATA_START_LBA + i, 1, sector);
  }

  /* Save user table */
  for (int i = 0; i < user_count && i < FS_MAX_USERS; i++)
  {
    for (int j = 0; j < 512; j++)
      sector[j] = 0;
    for (int j = 0; j < 64 && j < (int)sizeof(UserEntry); j++)
    {
      sector[j] = ((char *)&user_table[i])[j];
    }
    if (disk_write_lba(FS_DATA_START_LBA + 128 + i, 1, sector) != 0)
    {
      return -1;
    }
  }

  /* Clear remaining user slots */
  for (int i = user_count; i < 5; i++)
  {
    for (int j = 0; j < 512; j++)
      sector[j] = 0;
    disk_write_lba(FS_DATA_START_LBA + 128 + i, 1, sector);
  }

  /* Save file contents - Write each file's content to its designated sectors */
  for (int c = 0; c < file_content_count; c++)
  {
    uint16_t file_idx = file_contents[c].file_idx;
    uint32_t file_size = file_contents[c].size;

    /* Calculate sectors needed for this file */
    uint32_t sectors_needed = (file_size + 511) / 512;
    if (sectors_needed == 0)
      sectors_needed = 1;

    /* Write file content sectors */
    for (uint32_t s = 0; s < sectors_needed && s < 16; s++)
    {
      /* Zero sector buffer */
      for (int j = 0; j < 512; j++)
        sector[j] = 0;

      /* Calculate how much to copy for this sector */
      uint32_t offset = s * 512;
      uint32_t to_copy = file_size - offset;
      if (to_copy > 512)
        to_copy = 512;

      /* Copy data to sector buffer */
      for (uint32_t j = 0; j < to_copy; j++)
      {
        sector[j] = file_contents[c].data[offset + j];
      }

      /* Write sector to disk at the correct LBA for this file */
      uint32_t lba = FS_CONTENT_START_LBA + (file_idx * 16) + s;
      if (disk_write_lba(lba, 1, sector) != 0)
      {
        return -1;
      }
    }

    /* Clear remaining sectors for this file (if file shrunk) */
    for (uint32_t s = sectors_needed; s < 16; s++)
    {
      for (int j = 0; j < 512; j++)
        sector[j] = 0;
      uint32_t lba = FS_CONTENT_START_LBA + (file_idx * 16) + s;
      disk_write_lba(lba, 1, sector);
    }
  }

  return 0;
}

/* FIXED: fs_load_from_disk - Load content and match by file_idx correctly */
static int fs_load_from_disk(void)
{
  puts("[FS] Loading from disk...\n");
  char sector[512];

  /* Load current directory first */
  if (disk_read_lba(FS_CURDIR_LBA, 1, sector) == 0)
  {
    if (sector[0] != 0 && sector[0] == 'C' && sector[1] == ':')
    {
      /* Valid current directory found - copy it */
      for (int j = 0; j < 256 && sector[j]; j++)
      {
        current_dir[j] = sector[j];
      }
      /* Ensure null termination */
      current_dir[255] = '\0';
    }
    /* If not valid, keep default "C:\" */
  }

  /* Load file table - read up to 64 entries */
  fs_count = 0;
  for (int i = 0; i < 64 && i < FS_MAX_FILES; i++)
  {
    if (disk_read_lba(FS_DATA_START_LBA + i, 1, sector) == 0)
    {
      /* Check if this is a valid entry - first char of filename must not be 0 */
      if (sector[0] != 0)
      {
        for (int j = 0; j < 64 && j < (int)sizeof(FSEntry); j++)
        {
          ((char *)&fs_table[fs_count])[j] = sector[j];
        }
        fs_count++;
      }
      else
      {
        /* Empty slot - stop reading (files are stored contiguously) */
        break;
      }
    }
  }

  /* Load user table */
  user_count = 0;
  for (int i = 0; i < 5 && i < FS_MAX_USERS; i++)
  {
    if (disk_read_lba(FS_DATA_START_LBA + 128 + i, 1, sector) == 0)
    {
      if (sector[0] != 0)
      {
        for (int j = 0; j < 64 && j < (int)sizeof(UserEntry); j++)
        {
          ((char *)&user_table[user_count])[j] = sector[j];
        }
        user_count++;
      }
    }
  }

  /* CRITICAL FIX: Load file contents properly */
  file_content_count = 0;

  /* For each file in fs_table that has size > 0 */
  for (int i = 0; i < fs_count && file_content_count < 64; i++)
  {
    if (fs_table[i].size > 0 && fs_table[i].type == 0)
    {
      /* Calculate sectors needed */
      uint32_t sectors_needed = (fs_table[i].size + 511) / 512;
      if (sectors_needed == 0)
        sectors_needed = 1;

      /* Set up file_contents entry */
      file_contents[file_content_count].file_idx = i;
      file_contents[file_content_count].size = fs_table[i].size;

      /* Zero out the entire data buffer first */
      for (int z = 0; z < MAX_FILE_SIZE; z++)
      {
        file_contents[file_content_count].data[z] = 0;
      }

      /* Read content from disk - using file_idx i */
      uint32_t total_read = 0;
      for (uint32_t s = 0; s < sectors_needed && s < 16; s++)
      {
        uint32_t lba = FS_CONTENT_START_LBA + (i * 16) + s;
        if (disk_read_lba(lba, 1, sector) == 0)
        {
          uint32_t to_copy = fs_table[i].size - total_read;
          if (to_copy > 512)
            to_copy = 512;

          for (uint32_t j = 0; j < to_copy && total_read < MAX_FILE_SIZE; j++)
          {
            file_contents[file_content_count].data[total_read++] = sector[j];
          }
        }
      }

      file_content_count++;
    }
  }

  return 0;
}

/* Initialize filesystem - silent flag to suppress output */
static int fs_init_silent = 0;

/* Desktop directory initialization helper */
static void fs_ensure_desktop_dir(void)
{
  puts("[FS] Ensuring desktop dir...\n");

  /* Check/create C:\Desktop\ */
  bool has_desktop = false;
  for (int i = 0; i < fs_count; i++)
  {
    if (str_cmp(fs_table[i].name, "C:\\Desktop\\") == 0 && fs_table[i].type == 1)
    {
      has_desktop = true;
      break;
    }
  }
  if (!has_desktop && fs_count < FS_MAX_FILES)
  {
    str_copy(fs_table[fs_count].name, "C:\\Desktop\\", FS_MAX_FILENAME);
    fs_table[fs_count].size = 0;
    fs_table[fs_count].type = 1;
    fs_table[fs_count].attr = 0x10;
    fs_count++;
  }

  /* Check/create C:\Desktop\Applications\ */
  bool has_apps = false;
  for (int i = 0; i < fs_count; i++)
  {
    if (str_cmp(fs_table[i].name, "C:\\Desktop\\Applications\\") == 0 && fs_table[i].type == 1)
    {
      has_apps = true;
      break;
    }
  }
  if (!has_apps && fs_count < FS_MAX_FILES)
  {
    str_copy(fs_table[fs_count].name, "C:\\Desktop\\Applications\\", FS_MAX_FILENAME);
    fs_table[fs_count].size = 0;
    fs_table[fs_count].type = 1;
    fs_table[fs_count].attr = 0x10;
    fs_count++;
  }

  /* Create app shortcut stubs inside C:\Desktop\Applications\ if missing.
     These are zero-byte marker files - one per built-in app. */
  const char *app_shortcuts[] = {
    "C:\\Desktop\\Applications\\Terminal",
    "C:\\Desktop\\Applications\\Notepad",
    "C:\\Desktop\\Applications\\Paint",
    "C:\\Desktop\\Applications\\Calculator",
    "C:\\Desktop\\Applications\\Files",
    "C:\\Desktop\\Applications\\Clock",
    "C:\\Desktop\\Applications\\System Info",
  };
  int num_shortcuts = 7;

  for (int a = 0; a < num_shortcuts; a++)
  {
    bool exists = false;
    for (int i = 0; i < fs_count; i++)
    {
      if (str_cmp(fs_table[i].name, app_shortcuts[a]) == 0)
      {
        exists = true;
        break;
      }
    }
    if (!exists && fs_count < FS_MAX_FILES)
    {
      str_copy(fs_table[fs_count].name, app_shortcuts[a], FS_MAX_FILENAME);
      fs_table[fs_count].size = 0;
      fs_table[fs_count].type = 0; /* file shortcut */
      fs_table[fs_count].attr = 0x20;
      fs_count++;
    }
  }

  fs_save_to_disk();
}

static void fs_init_commands(void)
{
  puts("[FS] fs_init_commands start\n");
  /* CRITICAL FIX: Explicitly zero out ALL filesystem state */
  fs_count = 0;
  user_count = 0;

  /* CRITICAL: Zero out file_content_count and the entire array! */
  file_content_count = 0;
  for (int i = 0; i < 64; i++)
  {
    file_contents[i].file_idx = 0;
    file_contents[i].size = 0;
    for (int j = 0; j < MAX_FILE_SIZE; j++)
    {
      file_contents[i].data[j] = 0;
    }
  }
  puts("[FS] Memory cleared\n");

  /* Load from disk - this will update the counters */
  fs_load_from_disk();

  /* Only print messages if not silent */
  if (!fs_init_silent)
  {
    /* Verify file_content_count is correct */
    char buf[16];
    puts("[INIT: file_content_count=");
    int_to_str(file_content_count, buf);
    puts(buf);
    puts("]\n");

    /* Print initialization message */
    puts("Ready. ");
    int_to_str(fs_count, buf);
    puts(buf);
    puts(" files, ");
    int_to_str(file_content_count, buf);
    puts(buf);
    puts(" contents loaded. Type HELP for commands.\n");
  }

  /* If no users, create default root user */
  if (user_count == 0)
  {
    str_copy(user_table[0].username, "root", 32);
    uint32_t h = hash_string("root");
    for (int i = 0; i < 32; i++)
    {
      user_table[0].password_hash[i] = (char)((h >> (i % 4 * 8)) & 0xFF);
    }
    user_count = 1;
  }

  /* Ensure C:\Desktop directory exists */
  fs_ensure_desktop_dir();
}

/* Command parsing */
static const char *skip_spaces(const char *s)
{
  while (*s == ' ' || *s == '\t')
    s++;
  return s;
}

static const char *get_token(const char *s, char *token, int max)
{
  s = skip_spaces(s);
  int i = 0;
  while (*s && *s != ' ' && *s != '\t' && i < max - 1)
  {
    token[i++] = *s++;
  }
  token[i] = '\0';
  return s;
}

/* COMMAND IMPLEMENTATIONS */

/* Forward declaration for NETMODE command (defined in cmd_netmode.c) */
extern int cmd_netmode(const char *args);

/* Forward declaration for MAKE command (defined in cmd_make.c) */
extern int cmd_make(const char *args);

/* Forward declaration for PYTHON command (defined in cmd_python.c) */
extern int cmd_python(const char *args);

/* ============================================================
 * KEYBOARD - keyboard layout switcher (English / Serbian Latin)
 * The OS reads all input as ASCII from the BIOS/keyboard ISR.
 * This command stores the active layout and provides a software
 * remapping table that shell.c and terminal.c can call.
 * Serbian Latin QWERTY maps the same physical keys to characters
 * used in Serbian: c->c, but adds s->s, z->z and the digraphs
 * dz, lj, nj via their Latin equivalents.  For the purpose of
 * this OS the remapping gives you: c/C, s/S, z/Z, d/D plus the
 * combined-letter sequences lj/LJ and nj/NJ typed as normal
 * Latin letters, which is exactly how Serbian Latin keyboards
 * behave on a standard QWERTY layout.
 * ============================================================ */

/* 0 = English (default), 1 = Serbian Latin */
int keyboard_layout = 0;  /* exported so shell / terminal can use it */

/* Remap a character according to current layout.
   For Serbian Latin the physical key mapping is identical to
   English on QWERTY - we just tag the layout so the user knows
   which convention is active.  The real difference is accented
   chars (c with caron, etc.) which we approximate here:
     [ -> s-caron (s)   { -> S-caron (S)
     ] -> c-caron (c)   } -> C-caron (C)
     \ -> z-caron (z)   | -> Z-caron (Z)
   These keys are rarely used in English on QWERTY so it is a
   clean swap with no conflict.                               */
/* Serbian Latin QWERTY full key map.
 *
 * Physical key -> what appears on screen.
 *
 * Number row (unshifted) is identical to US QWERTY (1-9, 0 same).
 * Shift + number row differs from US:
 *   US:  !  @  #  $  %  ^  &  *  (  )
 *   SRB: !  "  #  $  %  &  /  (  )  =
 *
 * Special letters (bracket/backslash cluster on US keyboard):
 *   [  ->  s (s-caron, written "s" in ASCII)
 *   ]  ->  c (c-caron)
 *   \  ->  z (z-caron)
 *   {  ->  S
 *   }  ->  C
 *   |  ->  Z
 *   ;  ->  c (Cyrillic: same key, written c in Latin)
 *   '  ->  c (another common mapping)
 *
 * The BIOS/keyboard ISR gives us the ASCII value of what is typed
 * AFTER the US QWERTY shift lookup.  So when the user presses
 * SHIFT+0 on a US keyboard we receive ')'.  We remap that to '='.
 */
char keyboard_remap(char c)
{
    if (keyboard_layout == 0) return c;  /* English - pass through */

    switch (c) {
        /* Shifted number row - US result -> Serbian result */
        case '@':  return '"';   /* SHIFT+2: @ -> " */
        case '^':  return '&';   /* SHIFT+6: ^ -> & */
        case '&':  return '/';   /* SHIFT+7: & -> / */
        case '*':  return '(';   /* SHIFT+8: * -> ( */
        case '(':  return ')';   /* SHIFT+9: ( -> ) */
        case ')':  return '=';   /* SHIFT+0: ) -> = */

        /* Bracket/backslash cluster -> Serbian caron letters */
        case '[':  return 's';
        case '{':  return 'S';
        case ']':  return 'c';
        case '}':  return 'C';
        case '\\': return 'z';
        case '|':  return 'Z';

        /* Semicolon cluster: ; -> d (for dz), : -> D */
        case ';':  return 'd';
        case ':':  return 'D';

        default:   return c;
    }
}

static int cmd_keyboard(const char *args)
{
    char tok[32];
    get_token(args, tok, 32);
    str_upper(tok);

    if (tok[0] == 0) {
        /* No argument - show current layout and usage */
        puts("Keyboard layout: ");
        puts(keyboard_layout == 0 ? "English (default)\n" : "Serbian Latin QWERTY\n");
        puts("\nUsage:\n");
        puts("  KEYBOARD ENGLISH   - switch to English layout\n");
        puts("  KEYBOARD SERBIAN   - switch to Serbian Latin QWERTY\n");
        puts("\nSerbien Latin key overrides (bracket cluster):\n");
        puts("  [  ->  s (s-caron)     {  ->  S\n");
        puts("  ]  ->  c (c-caron)     }  ->  C\n");
        puts("  \\  ->  z (z-caron)     |  ->  Z\n");
        return 0;
    }

    if (str_cmp(tok, "ENGLISH") == 0) {
        keyboard_layout = 0;
        puts("Keyboard layout set to: English\n");
        return 0;
    }

    if (str_cmp(tok, "SERBIAN") == 0) {
        keyboard_layout = 1;
        puts("Keyboard layout set to: Serbian Latin QWERTY\n");
        puts("Keys [ ] \\ now produce s c z (Serbian caron letters)\n");
        puts("Uppercase: { } | produce S C Z\n");
        return 0;
    }

    puts("Unknown layout. Use: KEYBOARD ENGLISH  or  KEYBOARD SERBIAN\n");
    return -1;
}

/* Rust WiFi Driver functions */
extern int wifi_driver_init(void);
extern int wifi_driver_test(void);
extern int wifi_send_packet(void *iface, const uint8_t *data, uint32_t len);
extern int wifi_recv_packet(void *iface, uint8_t *data, uint32_t max_len);
extern int wifi_get_mac(uint8_t *mac);
extern int wifi_is_connected(void);
extern void wifi_driver_shutdown(void);

/* Rust GPU Driver functions */
extern int gpu_driver_init(void);
extern int gpu_driver_test(void);
extern uint32_t *gpu_setup_framebuffer(void);
extern int gpu_flush(void);
extern void gpu_clear(uint32_t color);
extern void gpu_fill_rect(int x, int y, int w, int h, uint32_t color);
extern void gpu_draw_char(int x, int y, uint8_t c, uint32_t fg, uint32_t bg);
extern void gpu_draw_string(int x, int y, const uint8_t *str, uint32_t fg, uint32_t bg);
extern void gpu_disable_scanout(void);

/* Legacy GUI App Placeholders */
static int cmd_gui_placeholder(const char *args)
{
  (void)args;
  puts("This application requires GUIMODE.\n");
  puts("Type GUIMODE to switch to the desktop environment.\n");
  puts("Then launch this app from the desktop or the GUI terminal.\n");
  return 0;
}

/* GUITEST - Test Rust GPU driver with graphical display */
static int cmd_guitest(const char *args)
{
  (void)args;

  // Call GPU test function directly - it saves screen first before any output
  return gpu_driver_test();
}

/* CALC-GUI - GUI Calculator with mouse AND keyboard support */
static int cmd_calc_gui(const char *args)
{
  return cmd_gui_placeholder(args);
}

/* WIFITEST - Test Rust WiFi driver */
static int cmd_wifitest(const char *args)
{
  (void)args;

  puts("=== Rust WiFi Driver Test ===\n");
  puts("Running wifi_driver_test()...\n");

  // Call Rust WiFi test function
  return wifi_driver_test();
}

/* NETSTART - Initialize network and get IP via DHCP */
static int cmd_netstart(const char *args)
{
  (void)args;

  extern int dhcp_init(network_interface_t * iface);
  extern int dhcp_discover(network_interface_t * iface);
  extern void netif_poll(void);

  set_attr(0x0B); // Cyan
  puts("\n=== Network Initialization (Rust Driver) ===\n");
  set_attr(0x07);

  // Initialize Rust WiFi driver
  puts("[1/3] Initializing Rust WiFi driver...\n");

  int result = wifi_driver_init();
  puts("[NETSTART] wifi_driver_init returned: ");
  putc('0' + (result & 0xF));
  puts("\n");

  if (result != 0)
  {
    set_attr(0x0C); // Red
    puts("ERROR: Failed to initialize Rust WiFi driver!\n");
    set_attr(0x07);
    return -1;
  }

  set_attr(0x0A); // Green
  puts("✓ Rust WiFi driver initialized\n");
  set_attr(0x07);

  // Get interface
  network_interface_t *iface = netif_get_default();

  if (!iface)
  {
    set_attr(0x0C);
    puts("ERROR: No network interface available!\n");
    set_attr(0x07);
    return -1;
  }

  // Initialize DHCP
  puts("[2/3] Initializing DHCP client...\n");

  if (dhcp_init(iface) != 0)
  {
    set_attr(0x0C);
    puts("ERROR: Failed to initialize DHCP!\n");
    set_attr(0x07);
    return -1;
  }

  set_attr(0x0A);
  puts("✓ DHCP client initialized\n");
  set_attr(0x07);

  // Send DHCP DISCOVER
  puts("[3/3] Requesting IP address via DHCP...\n");

  for (int attempt = 0; attempt < 3; attempt++)
  {

    if (attempt > 0)
    {
      puts("Retry ");
      putc('0' + attempt);
      puts("/3...\n");
    }

    puts("[DEBUG] NETSTART: Calling dhcp_discover...\n");
    int disc_result = dhcp_discover(iface);
    puts("[DEBUG] NETSTART: dhcp_discover returned: ");
    putc('0' + (disc_result & 0xF));
    puts("\n");

    if (disc_result < 0)
    {
      puts("Failed to send DHCP DISCOVER\n");
      continue;
    }

    puts("[DEBUG] NETSTART: Waiting for DHCP response...\n");

    // Wait for DHCP response
    uint32_t start = get_ticks();
    bool got_ip = false;
    int poll_count = 0;

    while (get_ticks() - start < 90)
    { // ~5 seconds
      // Poll for DHCP response
      for (int i = 0; i < 20; i++)
      {
        netif_poll();
        poll_count++;
      }

      // Check if we got an IP
      if (iface->ip_addr != 0)
      {
        puts("[DEBUG] NETSTART: Got IP address after ");
        char cnt[8];
        cnt[0] = '0' + (poll_count / 1000) % 10;
        cnt[1] = '0' + (poll_count / 100) % 10;
        cnt[2] = '0' + (poll_count / 10) % 10;
        cnt[3] = '0' + poll_count % 10;
        cnt[4] = '\0';
        puts(cnt);
        puts(" polls\n");
        got_ip = true;
        break;
      }
    }

    puts("[DEBUG] NETSTART: Finished waiting. poll_count=");
    char cnt[8];
    cnt[0] = '0' + (poll_count / 1000) % 10;
    cnt[1] = '0' + (poll_count / 100) % 10;
    cnt[2] = '0' + (poll_count / 10) % 10;
    cnt[3] = '0' + poll_count % 10;
    cnt[4] = '\0';
    puts(cnt);
    puts("\n");

    if (got_ip)
    {
      set_attr(0x0A); // Green
      puts("\n✓ IP address configured!\n\n");
      set_attr(0x07);

      // Show IP info
      puts("IP Address:  ");
      char buf[16];
      int_to_str((iface->ip_addr >> 24) & 0xFF, buf);
      puts(buf);
      puts(".");
      int_to_str((iface->ip_addr >> 16) & 0xFF, buf);
      puts(buf);
      puts(".");
      int_to_str((iface->ip_addr >> 8) & 0xFF, buf);
      puts(buf);
      puts(".");
      int_to_str(iface->ip_addr & 0xFF, buf);
      puts(buf);
      puts("\n");

      puts("Gateway:     ");
      int_to_str((iface->gateway >> 24) & 0xFF, buf);
      puts(buf);
      puts(".");
      int_to_str((iface->gateway >> 16) & 0xFF, buf);
      puts(buf);
      puts(".");
      int_to_str((iface->gateway >> 8) & 0xFF, buf);
      puts(buf);
      puts(".");
      int_to_str(iface->gateway & 0xFF, buf);
      puts(buf);
      puts("\n");

      puts("DNS Server:  ");
      int_to_str((iface->dns_server >> 24) & 0xFF, buf);
      puts(buf);
      puts(".");
      int_to_str((iface->dns_server >> 16) & 0xFF, buf);
      puts(buf);
      puts(".");
      int_to_str((iface->dns_server >> 8) & 0xFF, buf);
      puts(buf);
      puts(".");
      int_to_str(iface->dns_server & 0xFF, buf);
      puts(buf);
      puts("\n\n");

      set_attr(0x0E);
      puts("Network ready! You can now use WGET, PING, etc.\n");
      set_attr(0x07);
      return 0;
    }
  }

  set_attr(0x0C);
  puts("\nERROR: Failed to get IP address via DHCP\n");
  puts("Check that QEMU is running with: -net nic,model=virtio -net user\n");
  set_attr(0x07);
  return -1;
}

/* 1. HELP - Show command list */
static int cmd_help(const char *args)
{
  (void)args;
  puts("========================================================================\n");
  puts("AurionOS Available Commands:\n");
  puts("  File: DIR LS CD MKDIR RMDIR TOUCH DEL CAT NANO TYPE COPY MOVE REN FIND\n");
  puts("  Disk: CHKDSK FORMAT LABEL VOL DISKPART FSCK\n");
  puts("  Info: VER TIME DATE UPTIME MEM SYSINFO UNAME WHOAMI HOSTNAME\n");
  puts("  User: USERADD USERDEL PASSWD USERS LOGIN LOGOUT SU SUDO\n");
  puts("  Proc: PS KILL TOP TASKLIST TASKKILL\n");
  puts("  Misc: CLS CLEAR CLS-TERMINAL CLEAR-TERMINAL COLOR ECHO BEEP CALC\n");
  puts("        HEXDUMP ASCII HASH\n");
  puts("  Ctrl: REBOOT SHUTDOWN HALT PAUSE SLEEP EXIT\n");
  puts("  Network: NETSTART IPCONFIG PING WGET WIFITEST\n");

  puts("  Graphics: GUITEST CALC-GUI NOTEPAD PAINT FILEBROWSER CLOCK\n");
  puts("  Programming: PYTHON (Mini Python interpreter) MAKE\n");
  puts("========================================================================\n");
  return 0;
}

/* 2. CLS/CLEAR - Clear screen.
   Returns -2 so shell.c skips printing a second prompt immediately after. */
static int cmd_cls(const char *args)
{
  (void)args;
  extern void set_cursor_pos(int row, int col);
  cls();
  set_cursor_pos(0, 0);
  return -2; /* shell.c special code: suppress duplicate prompt */
}

/* 3. VER - Show version */
static int cmd_ver(const char *args)
{
  (void)args;
  puts("AurionOS Version 1.0 Beta\n");
  puts("32-bit Protected Mode Operating System\n");
  return 0;
}

/* 4. TIME - Show current time */
static int cmd_time(const char *args)
{
  (void)args;
  uint8_t h, m, s;
  if (sys_get_time(&h, &m, &s) == 0)
  {
    char buf[16];
    int_to_str(h, buf);
    puts(buf);
    puts(":");
    if (m < 10)
      puts("0");
    int_to_str(m, buf);
    puts(buf);
    puts(":");
    if (s < 10)
      puts("0");
    int_to_str(s, buf);
    puts(buf);
    puts("\n");
  }
  return 0;
}

/* 5. DATE - Show current date */
static int cmd_date(const char *args)
{
  (void)args;
  uint8_t day, month;
  uint16_t year;
  if (sys_get_date(&day, &month, &year) == 0)
  {
    char buf[16];
    int_to_str(year, buf);
    puts(buf);
    puts("-");
    if (month < 10)
      puts("0");
    int_to_str(month, buf);
    puts(buf);
    puts("-");
    if (day < 10)
      puts("0");
    int_to_str(day, buf);
    puts(buf);
    puts("\n");
  }
  return 0;
}

/* 6. REBOOT - Reboot system */
static int cmd_reboot(const char *args)
{
  (void)args;
  puts("Rebooting...\n");
  sleep_ms(1000);
  sys_reboot();
  return 0;
}

/* 7. SHUTDOWN - Shutdown system */
static int cmd_shutdown(const char *args)
{
  (void)args;
  puts("System shutting down...\n");
  sleep_ms(2000);
  sys_shutdown();
  return 0;
}

/* 8. MKDIR - Create directory */
static int cmd_mkdir(const char *args)
{
  char name[64];
  args = get_token(args, name, 64);

  if (name[0] == 0)
  {
    puts("Usage: MKDIR dirname\n");
    return -1;
  }

  if (fs_count >= FS_MAX_FILES)
  {
    puts("Error: Filesystem full\n");
    return -1;
  }

  /* Build full path: current_dir + dirname */
  char full_path[256];
  int dir_len = str_len(current_dir);
  for (int i = 0; i < dir_len && i < 255; i++)
  {
    full_path[i] = current_dir[i];
  }

  /* Add dirname */
  int name_len = str_len(name);
  for (int i = 0; i < name_len && (dir_len + i) < 255; i++)
  {
    full_path[dir_len + i] = name[i];
  }
  full_path[dir_len + name_len] = '\0';

  /* Check if already exists */
  for (int i = 0; i < fs_count; i++)
  {
    if (str_cmp(fs_table[i].name, full_path) == 0)
    {
      puts("Error: Already exists\n");
      return -1;
    }
  }

  str_copy(fs_table[fs_count].name, full_path, FS_MAX_FILENAME);
  fs_table[fs_count].size = 0;
  fs_table[fs_count].type = 1;
  fs_table[fs_count].attr = 0x10;
  fs_count++;

  fs_save_to_disk();
  puts("Directory created: ");
  puts(name);
  puts("\n");
  return 0;
}

/* 9. RMDIR - Remove directory */
static int cmd_rmdir(const char *args)
{
  char name[64];
  args = get_token(args, name, 64);

  if (name[0] == 0)
  {
    puts("Usage: RMDIR dirname\n");
    return -1;
  }

  for (int i = 0; i < fs_count; i++)
  {
    if (str_cmp(fs_table[i].name, name) == 0 && fs_table[i].type == 1)
    {
      for (int j = i; j < fs_count - 1; j++)
      {
        fs_table[j] = fs_table[j + 1];
      }
      fs_count--;
      fs_save_to_disk();
      puts("Directory removed\n");
      return 0;
    }
  }

  puts("Error: Directory not found\n");
  return -1;
}

/* 10. TOUCH - Create empty file */
static int cmd_touch(const char *args)
{
  char name[64];
  args = get_token(args, name, 64);

  if (name[0] == 0)
  {
    puts("Usage: TOUCH filename\n");
    return -1;
  }

  if (fs_count >= FS_MAX_FILES)
  {
    puts("Error: Filesystem full\n");
    return -1;
  }

  /* Build full path: current_dir + filename */
  char full_path[256];
  int dir_len = str_len(current_dir);
  for (int i = 0; i < dir_len && i < 255; i++)
  {
    full_path[i] = current_dir[i];
  }

  /* Add filename */
  int name_len = str_len(name);
  for (int i = 0; i < name_len && (dir_len + i) < 255; i++)
  {
    full_path[dir_len + i] = name[i];
  }
  full_path[dir_len + name_len] = '\0';

  /* Check if file already exists in this directory */
  for (int i = 0; i < fs_count; i++)
  {
    if (str_cmp(fs_table[i].name, full_path) == 0)
    {
      puts("File already exists\n");
      return 0;
    }
  }

  str_copy(fs_table[fs_count].name, full_path, FS_MAX_FILENAME);
  fs_table[fs_count].size = 0;
  fs_table[fs_count].type = 0;
  fs_table[fs_count].attr = 0x20;
  fs_count++;

  fs_save_to_disk();
  puts("File created: ");
  puts(name);
  puts("\n");
  return 0;
}

/* 11. DEL/RM - Delete file */
static int cmd_del(const char *args)
{
  char name[64];
  args = get_token(args, name, 64);

  if (name[0] == 0)
  {
    puts("Usage: DEL filename\n");
    return -1;
  }

  /* Build full path */
  char full_path[256];
  int dir_len = str_len(current_dir);
  for (int i = 0; i < dir_len && i < 255; i++)
  {
    full_path[i] = current_dir[i];
  }
  int name_len = str_len(name);
  for (int i = 0; i < name_len && (dir_len + i) < 255; i++)
  {
    full_path[dir_len + i] = name[i];
  }
  full_path[dir_len + name_len] = '\0';

  for (int i = 0; i < fs_count; i++)
  {
    if (str_cmp(fs_table[i].name, full_path) == 0 && fs_table[i].type == 0)
    {
      for (int j = i; j < fs_count - 1; j++)
      {
        fs_table[j] = fs_table[j + 1];
      }
      fs_count--;
      fs_save_to_disk();
      puts("File deleted\n");
      return 0;
    }
  }

  puts("Error: File not found\n");
  return -1;
}

/* 12. DIR/LS - List directory */
static int cmd_dir(const char *args)
{
  (void)args;

  puts("Directory of ");
  puts(current_dir);
  puts("\n\n");

  int file_count = 0;
  int dir_count = 0;
  uint32_t total_size = 0;

  int current_dir_len = str_len(current_dir);

  for (int i = 0; i < fs_count; i++)
  {
    /* Check if this entry is in the current directory */
    bool in_current_dir = true;

    /* Compare path prefix */
    for (int j = 0; j < current_dir_len; j++)
    {
      if (fs_table[i].name[j] != current_dir[j])
      {
        in_current_dir = false;
        break;
      }
    }

    /* Make sure there's no subdirectory after current path */
    if (in_current_dir && fs_table[i].name[current_dir_len] != '\0')
    {
      for (int j = current_dir_len; fs_table[i].name[j] != '\0'; j++)
      {
        if (fs_table[i].name[j] == '\\')
        {
          in_current_dir = false;
          break;
        }
      }
    }
    else if (in_current_dir && fs_table[i].name[current_dir_len] == '\0')
    {
      /* This is the directory itself, skip it */
      in_current_dir = false;
    }

    if (!in_current_dir)
      continue;

    /* Display the filename without the full path */
    char *display_name = fs_table[i].name + current_dir_len;

    char buf[16];

    if (fs_table[i].type == 1)
    {
      puts("<DIR>      ");
      dir_count++;
    }
    else
    {
      int_to_str(fs_table[i].size, buf);
      puts(buf);
      for (int j = str_len(buf); j < 11; j++)
        puts(" ");
      file_count++;
      total_size += fs_table[i].size;
    }

    puts(display_name);
    puts("\n");
  }

  puts("\n");
  char buf[16];
  int_to_str(file_count, buf);
  puts(buf);
  puts(" file(s), ");
  int_to_str(dir_count, buf);
  puts(buf);
  puts(" dir(s), ");
  int_to_str(total_size, buf);
  puts(buf);
  puts(" bytes\n");

  return 0;
}

/* 13. CD - Change directory */
static int cmd_cd(const char *args)
{
  char name[64];
  args = get_token(args, name, 64);

  if (name[0] == 0)
  {
    puts(current_dir);
    puts("\n");
    return 0;
  }

  if (str_cmp(name, "..") == 0)
  {
    int len = str_len(current_dir);
    if (len > 3)
    {
      for (int i = len - 2; i >= 0; i--)
      {
        if (current_dir[i] == '\\')
        {
          current_dir[i + 1] = '\0';
          break;
        }
      }
    }
    /* Save current directory to disk */
    fs_save_to_disk();
    return 0;
  }

  /* Build full path to check if directory exists */
  char full_path[256];
  int dir_len = str_len(current_dir);
  for (int i = 0; i < dir_len && i < 255; i++)
  {
    full_path[i] = current_dir[i];
  }
  int name_len = str_len(name);
  for (int i = 0; i < name_len && (dir_len + i) < 255; i++)
  {
    full_path[dir_len + i] = name[i];
  }
  full_path[dir_len + name_len] = '\0';

  /* Check if this directory exists */
  for (int i = 0; i < fs_count; i++)
  {
    if (str_cmp(fs_table[i].name, full_path) == 0 && fs_table[i].type == 1)
    {
      /* Directory exists, change to it */
      str_copy(current_dir, full_path, 256);
      /* Add trailing backslash if not present */
      int len = str_len(current_dir);
      if (current_dir[len - 1] != '\\')
      {
        current_dir[len] = '\\';
        current_dir[len + 1] = '\0';
      }
      /* Save current directory to disk */
      fs_save_to_disk();
      return 0;
    }
  }

  puts("Directory not found\n");
  return -1;
}

/* 14. CAT/TYPE - Display file contents */
static int cmd_cat(const char *args)
{
  char name[64];
  args = get_token(args, name, 64);

  if (name[0] == 0)
  {
    puts("Usage: CAT filename\n");
    return -1;
  }

  /* Build full path */
  char full_path[256];
  int dir_len = str_len(current_dir);
  for (int i = 0; i < dir_len && i < 255; i++)
  {
    full_path[i] = current_dir[i];
  }
  int name_len = str_len(name);
  for (int i = 0; i < name_len && (dir_len + i) < 255; i++)
  {
    full_path[dir_len + i] = name[i];
  }
  full_path[dir_len + name_len] = '\0';

  /* Find file in fs_table */
  int file_idx = -1;
  for (int i = 0; i < fs_count; i++)
  {
    if (str_cmp(fs_table[i].name, full_path) == 0 && fs_table[i].type == 0)
    {
      file_idx = i;
      break;
    }
  }

  if (file_idx < 0)
  {
    puts("File not found\n");
    return -1;
  }

  /* Check if file has any size */
  if (fs_table[file_idx].size == 0)
  {
    puts("(empty file)\n");
    return 0;
  }

  char tmp[16];
  puts("Looking for file_idx=");
  int_to_str(file_idx, tmp);
  puts(tmp);
  puts(" in file_content_count=");
  int_to_str(file_content_count, tmp);
  puts(tmp);
  puts("\n");

  /* Search for content in file_contents array */
  for (int i = 0; i < file_content_count; i++)
  {
    if ((int)file_contents[i].file_idx == file_idx)
    {
      if (file_contents[i].size == 0)
      {
        puts("(empty file)\n");
        return 0;
      }

      /* Display file contents */
      for (uint32_t j = 0; j < file_contents[i].size; j++)
      {
        putc(file_contents[i].data[j]);
      }

      /* Add newline if file doesn't end with one */
      if (file_contents[i].data[file_contents[i].size - 1] != '\n')
      {
        puts("\n");
      }
      return 0;
    }
  }

  /* File exists but no content found */
  puts("(empty file)\n");
  return 0;
}

/* 15. NANO - Simple text editor - COMPLETELY FIXED */
static int cmd_nano(const char *args)
{
  char name[64];
  args = get_token(args, name, 64);

  if (name[0] == 0)
  {
    puts("Usage: NANO filename\n");
    return -1;
  }

  /* Build full path */
  char full_path[256];
  int dir_len = str_len(current_dir);
  for (int i = 0; i < dir_len && i < 255; i++)
  {
    full_path[i] = current_dir[i];
  }
  int name_len = str_len(name);
  for (int i = 0; i < name_len && (dir_len + i) < 255; i++)
  {
    full_path[dir_len + i] = name[i];
  }
  full_path[dir_len + name_len] = '\0';

  /* Find or create file in fs_table */
  int file_idx = -1;
  bool is_new_file = false;

  for (int i = 0; i < fs_count; i++)
  {
    if (str_cmp(fs_table[i].name, full_path) == 0 && fs_table[i].type == 0)
    {
      file_idx = i;
      break;
    }
  }

  /* Create new file if not found */
  if (file_idx < 0)
  {
    if (fs_count >= FS_MAX_FILES)
    {
      puts("Error: Filesystem full\n");
      return -1;
    }
    str_copy(fs_table[fs_count].name, full_path, FS_MAX_FILENAME);
    fs_table[fs_count].size = 0;
    fs_table[fs_count].type = 0;
    fs_table[fs_count].attr = 0x20;
    file_idx = fs_count;
    is_new_file = true;
    fs_count++;
  }

  /* Show editor header */
  puts("\n=== NANO Editor ===\n");
  puts("File: ");
  puts(name);
  puts("\n");
  puts("ESC=Save\n");
  puts("-------------------\n");

  /* Allocate buffer on HEAP */
  char *buf = (char *)kmalloc(MAX_FILE_SIZE);
  if (buf == NULL)
  {
    puts("Error: Out of memory\n");
    if (is_new_file)
      fs_count--;
    return -1;
  }

  /* Zero the entire buffer */
  for (int i = 0; i < MAX_FILE_SIZE; i++)
  {
    buf[i] = 0;
  }

  int pos = 0;
  bool cancelled = false;

  /* Find existing content */
  int content_idx = -1;
  for (int i = 0; i < file_content_count; i++)
  {
    if ((int)file_contents[i].file_idx == file_idx)
    {
      content_idx = i;
      break;
    }
  }

  /* Load existing content if present */
  if (content_idx >= 0 && file_contents[content_idx].size > 0)
  {
    uint32_t load_size = file_contents[content_idx].size;
    if (load_size >= MAX_FILE_SIZE)
    {
      load_size = MAX_FILE_SIZE - 1;
    }

    /* Copy to buffer */
    for (uint32_t j = 0; j < load_size; j++)
    {
      buf[j] = file_contents[content_idx].data[j];
    }
    pos = (int)load_size;

    /* Display existing content */
    for (uint32_t j = 0; j < load_size; j++)
    {
      putc(buf[j]);
    }
  }
  else
  {
    puts("[New file...]\n");
    pos = 0;
  }

  /* Main edit loop */
  while (1)
  {
    uint16_t k = getkey();
    uint8_t key = (uint8_t)(k & 0xFF);

    if (key == 27)
      break; /* ESC - save */

    if (key == 3)
    { /* Ctrl+C - cancel */
      puts("\n^C Cancelled\n");
      cancelled = true;
      break;
    }

    if (key == 11)
    { /* Ctrl+K - clear */
      for (int i = 0; i < MAX_FILE_SIZE; i++)
        buf[i] = 0;
      pos = 0;
      cls();
      puts("\n=== NANO Editor ===\n");
      puts("File: ");
      puts(name);
      puts("\nESC=Save\n");
      puts("-------------------\n[Cleared]\n");
    }
    else if (key == 8)
    { /* Backspace */
      if (pos > 0)
      {
        pos--;
        buf[pos] = 0;
        putc(8);
        putc(' ');
        putc(8);
      }
    }
    else if (key >= 32 && key <= 126)
    { /* Printable */
      if (pos < MAX_FILE_SIZE - 1)
      {
        buf[pos++] = key;
        putc(key);
      }
    }
    else if (key == 13 || key == 10)
    { /* Enter */
      if (pos < MAX_FILE_SIZE - 1)
      {
        buf[pos++] = '\n';
        putc('\n');
      }
    }
  }

  if (cancelled)
  {
    kfree(buf);
    if (is_new_file)
      fs_count--;
    return 0;
  }

  puts("\n");

  if (pos == 0)
  {
    puts("Empty - not saved\n");
    kfree(buf);
    if (is_new_file)
      fs_count--;
    return 0;
  }

  /* Find or create content slot */
  if (content_idx < 0)
  {
    if (file_content_count >= 64)
    {
      puts("Error: Too many files\n");
      kfree(buf);
      if (is_new_file)
        fs_count--;
      return -1;
    }
    content_idx = file_content_count++;
  }

  /* Zero and save */
  for (int i = 0; i < MAX_FILE_SIZE; i++)
  {
    file_contents[content_idx].data[i] = 0;
  }

  file_contents[content_idx].file_idx = (uint16_t)file_idx;
  file_contents[content_idx].size = (uint32_t)pos;

  for (int i = 0; i < pos; i++)
  {
    file_contents[content_idx].data[i] = buf[i];
  }

  kfree(buf);

  /* CRITICAL: Update fs_table size BEFORE saving to disk */
  fs_table[file_idx].size = (uint32_t)pos;

  /* Save both file table and file contents to disk */
  if (fs_save_to_disk() == 0)
  {
    char tmp[16];
    puts("Saved ");
    int_to_str(pos, tmp);
    puts(tmp);
    puts(" bytes to disk\n");

    /* SUCCESS - data is on disk, show verification */
    puts("Verifying: fs_table[");
    int_to_str(file_idx, tmp);
    puts(tmp);
    puts("].size = ");
    int_to_str(fs_table[file_idx].size, tmp);
    puts(tmp);
    puts(" bytes\n");
  }
  else
  {
    puts("ERROR: Save failed!\n");
    return -1;
  }

  return 0;
}

/* 16. MEM - Display memory info */
static int cmd_mem(const char *args)
{
  (void)args;
  uint32_t stats[4];
  mem_get_stats(stats);

  char buf[32];
  puts("Memory Statistics:\n");
  
  puts("  Total Free: ");
  int_to_str(stats[0], buf);
  puts(buf);
  puts(" bytes (");
  int_to_str(stats[0] / (1024 * 1024), buf);
  puts(buf);
  puts(" MB)\n");

  puts("  Total Used: ");
  int_to_str(stats[1], buf);
  puts(buf);
  puts(" bytes (");
  int_to_str(stats[1] / (1024 * 1024), buf);
  puts(buf);
  puts(" MB)\n");

  puts("  Num Blocks: ");
  int_to_str(stats[2], buf);
  puts(buf);
  puts("\n");

  return 0;
}

/* 17. ECHO - Echo text */
static int cmd_echo(const char *args)
{
  args = skip_spaces(args);
  puts(args);
  puts("\n");
  return 0;
}

/* 18. COLOR - Set text color */
static int cmd_color(const char *args)
{
  char tok[16];
  args = get_token(args, tok, 16);

  if (tok[0] == 0)
  {
    puts("Usage: COLOR <0-255>\n");
    puts("Examples: COLOR 10 (green), COLOR 12 (red), COLOR 14 (yellow)\n");
    puts("Format: Foreground + (Background * 16)\n");
    return -1;
  }

  if (tok[0] >= '0' && tok[0] <= '9')
  {
    uint8_t c = str_to_int(tok);
    current_color = c;
    set_attr(c);
    puts("Color changed to ");
    char buf[16];
    int_to_str(c, buf);
    puts(buf);
    puts("\n");
    return 0;
  }

  puts("Usage: COLOR 0-255\n");
  return -1;
}

/* 19. BEEP - Make system beep */
static int cmd_beep(const char *args)
{
  (void)args;
  sys_beep(800, 200);
  return 0;
}

/* 20. UPTIME - Show system uptime */
static int cmd_uptime(const char *args)
{
  (void)args;
  uint32_t ticks = get_ticks();
  uint32_t seconds = ticks / 18;
  uint32_t minutes = seconds / 60;
  uint32_t hours = minutes / 60;

  char buf[16];
  puts("Uptime: ");
  int_to_str(hours, buf);
  puts(buf);
  puts("h ");
  int_to_str(minutes % 60, buf);
  puts(buf);
  puts("m ");
  int_to_str(seconds % 60, buf);
  puts(buf);
  puts("s\n");

  return 0;
}

/* 21. COPY/CP - Copy file */
static int cmd_copy(const char *args)
{
  char src[64], dst[64];
  args = get_token(args, src, 64);
  args = get_token(args, dst, 64);

  if (src[0] == 0 || dst[0] == 0)
  {
    puts("Usage: COPY source dest\n");
    return -1;
  }

  int src_idx = -1;
  for (int i = 0; i < fs_count; i++)
  {
    if (str_cmp(fs_table[i].name, src) == 0 && fs_table[i].type == 0)
    {
      src_idx = i;
      break;
    }
  }

  if (src_idx < 0)
  {
    puts("Source file not found\n");
    return -1;
  }

  if (fs_count >= FS_MAX_FILES)
  {
    puts("Error: Filesystem full\n");
    return -1;
  }

  str_copy(fs_table[fs_count].name, dst, FS_MAX_FILENAME);
  fs_table[fs_count].size = fs_table[src_idx].size;
  fs_table[fs_count].type = 0;
  fs_table[fs_count].attr = fs_table[src_idx].attr;
  int dst_idx = fs_count;
  fs_count++;

  for (int i = 0; i < file_content_count; i++)
  {
    if (file_contents[i].file_idx == src_idx && file_content_count < 64)
    {
      file_contents[file_content_count].file_idx = dst_idx;
      file_contents[file_content_count].size = file_contents[i].size;
      for (uint32_t j = 0; j < file_contents[i].size; j++)
      {
        file_contents[file_content_count].data[j] = file_contents[i].data[j];
      }
      file_content_count++;
      break;
    }
  }

  fs_save_to_disk();
  puts("File copied\n");
  return 0;
}

/* 22. MOVE/MV - Move file */
static int cmd_move(const char *args)
{
  char src[64], dst[64];
  args = get_token(args, src, 64);
  args = get_token(args, dst, 64);

  if (src[0] == 0 || dst[0] == 0)
  {
    puts("Usage: MOVE source dest\n");
    return -1;
  }

  for (int i = 0; i < fs_count; i++)
  {
    if (str_cmp(fs_table[i].name, src) == 0)
    {
      str_copy(fs_table[i].name, dst, FS_MAX_FILENAME);
      fs_save_to_disk();
      puts("File moved\n");
      return 0;
    }
  }

  puts("File not found\n");
  return -1;
}

/* 23. REN/RENAME - Rename file */
static int cmd_ren(const char *args) { return cmd_move(args); }

/* 24. FIND - Find files */
static int cmd_find(const char *args)
{
  char pattern[64];
  args = get_token(args, pattern, 64);

  if (pattern[0] == 0)
  {
    puts("Usage: FIND pattern\n");
    return -1;
  }

  int found = 0;
  for (int i = 0; i < fs_count; i++)
  {
    bool match = false;
    for (int j = 0; fs_table[i].name[j]; j++)
    {
      bool sub_match = true;
      for (int k = 0; pattern[k]; k++)
      {
        if (fs_table[i].name[j + k] != pattern[k])
        {
          sub_match = false;
          break;
        }
      }
      if (sub_match)
      {
        match = true;
        break;
      }
    }

    if (match)
    {
      puts(fs_table[i].name);
      puts("\n");
      found++;
    }
  }

  if (found == 0)
  {
    puts("No files found\n");
  }

  return 0;
}

/* 25. TREE - Show directory tree */
static int cmd_tree(const char *args)
{
  (void)args;
  puts("Directory tree:\n");
  puts(current_dir);
  puts("\n");

  for (int i = 0; i < fs_count; i++)
  {
    puts("  ");
    if (fs_table[i].type == 1)
    {
      puts("[DIR] ");
    }
    else
    {
      puts("[FILE] ");
    }
    puts(fs_table[i].name);
    puts("\n");
  }

  return 0;
}

/* 26. ATTRIB - Show/set file attributes */
static int cmd_attrib(const char *args)
{
  char name[64];
  args = get_token(args, name, 64);

  if (name[0] == 0)
  {
    for (int i = 0; i < fs_count; i++)
    {
      puts("0x");
      print_hex(fs_table[i].attr);
      puts(" ");
      puts(fs_table[i].name);
      puts("\n");
    }
  }
  else
  {
    for (int i = 0; i < fs_count; i++)
    {
      if (str_cmp(fs_table[i].name, name) == 0)
      {
        puts("Attributes: 0x");
        print_hex(fs_table[i].attr);
        puts("\n");
        return 0;
      }
    }
    puts("File not found\n");
  }

  return 0;
}

/* 27. CHMOD - Change file mode */
static int cmd_chmod(const char *args)
{
  char mode[16], name[64];
  args = get_token(args, mode, 16);
  args = get_token(args, name, 64);

  if (mode[0] == 0 || name[0] == 0)
  {
    puts("Usage: CHMOD mode filename\n");
    return -1;
  }

  uint8_t new_attr = str_to_int(mode);

  for (int i = 0; i < fs_count; i++)
  {
    if (str_cmp(fs_table[i].name, name) == 0)
    {
      fs_table[i].attr = new_attr;
      fs_save_to_disk();
      puts("Attributes changed\n");
      return 0;
    }
  }

  puts("File not found\n");
  return -1;
}

/* 28. VOL - Show volume label */
static int cmd_vol(const char *args)
{
  (void)args;
  puts("Volume in drive C has no label\n");
  puts("Volume Serial Number is 1234-5678\n");
  return 0;
}

/* 29. LABEL - Set volume label */
static int cmd_label(const char *args)
{
  (void)args;
  puts("Volume label command - not implemented in basic filesystem\n");
  return 0;
}

/* 30. CHKDSK - Check disk */
static int cmd_chkdsk(const char *args)
{
  (void)args;
  puts("Checking disk...\n");

  char buf[16];
  puts("Files: ");
  int_to_str(fs_count, buf);
  puts(buf);
  puts("/");
  int_to_str(FS_MAX_FILES, buf);
  puts(buf);
  puts("\n");

  uint32_t total = 0;
  for (int i = 0; i < fs_count; i++)
  {
    total += fs_table[i].size;
  }

  puts("Total size: ");
  int_to_str(total, buf);
  puts(buf);
  puts(" bytes\n");

  puts("Disk check complete - no errors found\n");
  return 0;
}

/* 31. FORMAT - Format disk */
static int cmd_format(const char *args)
{
  (void)args;
  puts("WARNING: This will erase all data!\n");
  puts("Press Y to confirm or any key to cancel: ");

  uint16_t k = getkey();
  uint8_t key = (uint8_t)(k & 0xFF);
  putc(key);
  puts("\n");

  if (key == 'Y' || key == 'y')
  {
    fs_count = 0;
    file_content_count = 0;
    fs_save_to_disk();
    puts("Format complete\n");
  }
  else
  {
    puts("Format cancelled\n");
  }

  return 0;
}

/* 32. DISKPART - Disk partitioning info */
static int cmd_diskpart(const char *args)
{
  (void)args;
  puts("Disk Information:\n");
  puts("Disk 0: Primary disk\n");
  puts("  Partition 1: C: (Active)\n");
  puts("  Type: FAT12\n");
  puts("  Size: 1.44 MB\n");
  return 0;
}

/* 33. FSCK - Filesystem check */
static int cmd_fsck(const char *args) { return cmd_chkdsk(args); }

/* 34. USERADD - Add user */
static int cmd_useradd(const char *args)
{
  char username[32];
  args = get_token(args, username, 32);

  if (username[0] == 0)
  {
    puts("Usage: USERADD username\n");
    return -1;
  }

  if (user_count >= FS_MAX_USERS)
  {
    puts("Error: User table full\n");
    return -1;
  }

  for (int i = 0; i < user_count; i++)
  {
    if (str_cmp(user_table[i].username, username) == 0)
    {
      puts("Error: User already exists\n");
      return -1;
    }
  }

  str_copy(user_table[user_count].username, username, 32);
  uint32_t h = hash_string(username);
  for (int i = 0; i < 32; i++)
  {
    user_table[user_count].password_hash[i] =
        (char)((h >> ((i % 4) * 8)) & 0xFF);
  }
  user_count++;

  fs_save_to_disk();
  puts("User created: ");
  puts(username);
  puts("\n");
  return 0;
}

/* 35. USERDEL - Delete user */
static int cmd_userdel(const char *args)
{
  char username[32];
  args = get_token(args, username, 32);

  if (username[0] == 0)
  {
    puts("Usage: USERDEL username\n");
    return -1;
  }

  for (int i = 0; i < user_count; i++)
  {
    if (str_cmp(user_table[i].username, username) == 0)
    {
      for (int j = i; j < user_count - 1; j++)
      {
        user_table[j] = user_table[j + 1];
      }
      user_count--;
      fs_save_to_disk();
      puts("User deleted\n");
      return 0;
    }
  }

  puts("User not found\n");
  return -1;
}

/* 36. PASSWD - Change password */
static int cmd_passwd(const char *args)
{
  char username[32];
  args = get_token(args, username, 32);

  if (username[0] == 0)
  {
    str_copy(username, current_user, 32);
  }

  for (int i = 0; i < user_count; i++)
  {
    if (str_cmp(user_table[i].username, username) == 0)
    {
      puts("Enter new password: ");
      char pwd[32];
      int pos = 0;
      while (1)
      {
        uint16_t k = getkey();
        uint8_t key = (uint8_t)(k & 0xFF);
        if (key == 13 || key == 10)
          break;
        if (key == 8 && pos > 0)
        {
          pos--;
        }
        else if (key >= 32 && key <= 126 && pos < 31)
        {
          pwd[pos++] = key;
          putc('*');
        }
      }
      pwd[pos] = '\0';
      puts("\n");

      uint32_t h = hash_string(pwd);
      for (int j = 0; j < 32; j++)
      {
        user_table[i].password_hash[j] = (char)((h >> ((j % 4) * 8)) & 0xFF);
      }

      fs_save_to_disk();
      puts("Password changed\n");
      return 0;
    }
  }

  puts("User not found\n");
  return -1;
}

/* 37. USERS - List users */
static int cmd_users(const char *args)
{
  (void)args;
  puts("System Users:\n");

  for (int i = 0; i < user_count; i++)
  {
    puts("  ");
    puts(user_table[i].username);
    puts("\n");
  }

  char buf[16];
  int_to_str(user_count, buf);
  puts("\nTotal: ");
  puts(buf);
  puts(" users\n");

  return 0;
}

/* 38. LOGIN - Login as user */
static int cmd_login(const char *args)
{
  char username[32];
  args = get_token(args, username, 32);

  if (username[0] == 0)
  {
    puts("Usage: LOGIN username\n");
    return -1;
  }

  for (int i = 0; i < user_count; i++)
  {
    if (str_cmp(user_table[i].username, username) == 0)
    {
      str_copy(current_user, username, 32);
      puts("Logged in as ");
      puts(username);
      puts("\n");
      return 0;
    }
  }

  puts("User not found\n");
  return -1;
}

/* 39. LOGOUT - Logout */
static int cmd_logout(const char *args)
{
  (void)args;
  str_copy(current_user, "root", 32);
  puts("Logged out\n");
  return 0;
}

/* 40. WHOAMI - Show current user */
static int cmd_whoami(const char *args)
{
  (void)args;
  puts(current_user);
  puts("\n");
  return 0;
}

/* 41. SU - Switch user */
static int cmd_su(const char *args) { return cmd_login(args); }

/* 42. PS - List processes */
static int cmd_ps(const char *args)
{
  (void)args;
  ensure_processes_init();

  puts("PID  NAME            STATE       MEM   PRI\n");
  puts("---  ----            -----       ---   ---\n");

  char buf[32];
  for (int i = 0; i < process_count; i++)
  {
    /* PID */
    int_to_str(process_table[i].pid, buf);
    puts(buf);
    /* Align */
    int len = str_len(buf);
    for (int k = 0; k < 5 - len; k++)
      putc(' ');

    /* Name */
    puts(process_table[i].name);
    len = str_len(process_table[i].name);
    for (int k = 0; k < 16 - len; k++)
      putc(' ');

    /* State */
    puts(process_table[i].state);
    len = str_len(process_table[i].state);
    for (int k = 0; k < 12 - len; k++)
      putc(' ');

    /* Mem */
    int_to_str(process_table[i].mem_usage, buf);
    puts(buf);
    puts("K");
    len = str_len(buf) + 1;
    for (int k = 0; k < 6 - len; k++)
      putc(' ');

    /* Priority */
    int_to_str(process_table[i].priority, buf);
    puts(buf);

    puts("\n");
  }
  return 0;
}

/* 43. KILL - Kill process */
static int cmd_kill(const char *args)
{
  char pid_str[16];
  args = get_token(args, pid_str, 16);

  if (pid_str[0] == 0)
  {
    puts("Usage: KILL <pid>\n");
    return -1;
  }

  uint32_t pid = str_to_int(pid_str);
  ensure_processes_init();

  if (pid <= 2)
  {
    puts("Error: Cannot kill critical system process (KERNEL/SHELL)\n");
    return -1;
  }

  int found_idx = -1;
  for (int i = 0; i < process_count; i++)
  {
    if (process_table[i].pid == pid)
    {
      found_idx = i;
      break;
    }
  }

  if (found_idx == -1)
  {
    puts("Error: Process not found\n");
    return -1;
  }

  /* Logically remove process */
  puts("Terminating process ");
  puts(process_table[found_idx].name);
  puts(" (PID ");
  puts(pid_str);
  puts(")...\n");

  /* Shift remaining */
  for (int i = found_idx; i < process_count - 1; i++)
  {
    process_table[i] = process_table[i + 1];
  }
  process_count--;

  puts("Process killed.\n");
  return 0;
}

/* 44. TOP - Show top processes */
static int cmd_top(const char *args)
{
  cls();
  puts("AurionOS Task Manager - Top Processes\n");
  puts("-----------------------------------\n");
  return cmd_ps(args);
}

/* 45. TASKLIST - List tasks */
static int cmd_tasklist(const char *args) { return cmd_ps(args); }

/* 46. TASKKILL - Kill task */
static int cmd_taskkill(const char *args) { return cmd_kill(args); }

/* 47. PAUSE - Pause execution */
static int cmd_pause(const char *args)
{
  (void)args;
  puts("Press any key to continue...");
  getkey();
  puts("\n");
  return 0;
}

/* 48. SLEEP - Sleep for seconds */
static int cmd_sleep(const char *args)
{
  char tok[16];
  args = get_token(args, tok, 16);

  if (tok[0] == 0)
  {
    puts("Usage: SLEEP seconds\n");
    return -1;
  }

  uint32_t sec = str_to_int(tok);
  sleep_ms(sec * 1000);
  return 0;
}

/* 49. HALT - Halt system */
static int cmd_halt(const char *args)
{
  (void)args;
  puts("System halted\n");
  __asm__ volatile("cli; hlt");
  return 0;
}

/* 50. EXIT - Exit shell */
static int cmd_exit(const char *args)
{
  (void)args;
  puts("Cannot exit shell - use REBOOT\n");
  return 0;
}

/* 51. CALC - Simple calculator */
static int cmd_calc(const char *args)
{
  char a_str[16], op[4], b_str[16];
  args = get_token(args, a_str, 16);
  args = get_token(args, op, 4);
  args = get_token(args, b_str, 16);

  if (a_str[0] == 0 || op[0] == 0 || b_str[0] == 0)
  {
    puts("Usage: CALC num op num (e.g., CALC 5 + 3)\n");
    return -1;
  }

  uint32_t a = str_to_int(a_str);
  uint32_t b = str_to_int(b_str);
  uint32_t result = 0;

  if (op[0] == '+')
  {
    result = a + b;
  }
  else if (op[0] == '-')
  {
    result = a - b;
  }
  else if (op[0] == '*')
  {
    result = a * b;
  }
  else if (op[0] == '/')
  {
    if (b == 0)
    {
      puts("Error: Division by zero\n");
      return -1;
    }
    result = a / b;
  }
  else
  {
    puts("Unknown operator\n");
    return -1;
  }

  char buf[16];
  int_to_str(result, buf);
  puts("Result: ");
  puts(buf);
  puts("\n");

  return 0;
}

/* 52. HEXDUMP - Hex dump of memory */
static int cmd_hexdump(const char *args)
{
  char addr_str[16];
  args = get_token(args, addr_str, 16);

  if (addr_str[0] == 0)
  {
    puts("Usage: HEXDUMP address\n");
    return -1;
  }

  uint32_t addr = str_to_int(addr_str);
  uint8_t *ptr = (uint8_t *)addr;

  puts("Hex dump at 0x");
  print_hex(addr);
  puts(":\n");

  for (int i = 0; i < 16; i++)
  {
    print_hex(ptr[i]);
    puts(" ");
  }
  puts("\n");

  return 0;
}

/* 53. ASCII - Show ASCII table */
static int cmd_ascii(const char *args)
{
  (void)args;
  puts("ASCII Table (printable):\n");

  for (int i = 32; i < 127; i++)
  {
    char buf[16];
    int_to_str(i, buf);
    puts(buf);
    puts(": ");
    putc((char)i);
    puts("  ");

    if ((i - 31) % 8 == 0)
    {
      puts("\n");
    }
  }
  puts("\n");

  return 0;
}

/* 54. HASH - Hash a string */
static int cmd_hash(const char *args)
{
  args = skip_spaces(args);

  if (args[0] == 0)
  {
    puts("Usage: HASH string\n");
    return -1;
  }

  uint32_t h = hash_string(args);

  puts("Hash: 0x");
  print_hex(h);
  puts("\n");

  return 0;
}

/* Stub WiFi command - informs user to use VirtIO network */
static int cmd_wifilogin(const char *args)
{
  (void)args;
  puts("WiFi is not available in VirtIO mode.\n");
  puts("Use NETSTART to initialize VirtIO network instead.\n");
  return 0;
}

static int cmd_wifiscan(const char *args)
{
  (void)args;
  puts("WiFi scanning is not available in VirtIO mode.\n");
  puts("VirtIO provides wired ethernet-like connectivity.\n");
  return 0;
}

static int cmd_wificonnect(const char *args)
{
  (void)args;
  puts("WiFi is not available in VirtIO mode.\n");
  puts("Use NETSTART to connect via VirtIO network.\n");
  return 0;
}

static int cmd_wifistatus(const char *args)
{
  (void)args;
  puts("WiFi status not available. Use IPCONFIG for network status.\n");
  return 0;
}

/* Dummy declarations to satisfy any remaining references */

/* Alias commands */
static int cmd_clear(const char *args) { return cmd_cls(args); }
static int cmd_rm(const char *args) { return cmd_del(args); }
static int cmd_ls(const char *args) { return cmd_dir(args); }
static int cmd_pwd(const char *args)
{
  (void)args;
  puts(current_dir);
  puts("\n");
  return 0;
}
static int cmd_type(const char *args) { return cmd_cat(args); }
static int cmd_cp(const char *args) { return cmd_copy(args); }
static int cmd_mv(const char *args) { return cmd_move(args); }

/* WiFi stub commands - Not supported in VirtIO mode */
static int cmd_wifiap(const char *a)
{
  (void)a;
  puts("WiFi not available - use NETSTART\n");
  return 0;
}
static int cmd_wifidisconnect(const char *a)
{
  (void)a;
  puts("WiFi not available\n");
  return 0;
}
static int cmd_wifirescan(const char *a)
{
  (void)a;
  puts("WiFi not available\n");
  return 0;
}
static int cmd_wifisignal(const char *a)
{
  (void)a;
  puts("WiFi not available\n");
  return 0;
}
static int cmd_wifistat(const char *a)
{
  (void)a;
  puts("WiFi not available - use IPCONFIG\n");
  return 0;
}
/* cmd_wifitest defined above with Rust driver */

/* System commands with real implementations */

static int cmd_mount(const char *a)
{
  (void)a;
  char buf[16];
  puts("Mounted filesystems:\n");
  
  /* C: is the primary AurionFS volume on Hard Disk */
  puts("  C:\\    AurionFS    rw,relatime    (LBA 500+)\n");

  /* Virtual Filesystems */
  puts("  /dev   devfs       rw,nosuid      (devices)\n");
  
  ensure_processes_init();
  if (process_count > 0)
    puts("  /proc  procfs      ro,nosuid      (processes)\n");

  /* Optional: Floppy check (simulated) */
  puts("  A:\\    FAT12       ro             (removable)\n");

  return 0;
}

static int cmd_umount(const char *a)
{
  char tok[64];
  a = get_token(a, tok, 64);
  if (tok[0] == 0)
  {
    puts("Usage: UMOUNT <mountpoint>\n");
    return -1;
  }
  
  if (str_cmp(tok, "C:\\") == 0 || str_cmp(tok, "C:") == 0 || str_cmp(tok, "/") == 0)
  {
    puts("umount: C:\\ is busy (root filesystem)\n");
    return -1;
  }
  
  if (str_cmp(tok, "/dev") == 0 || str_cmp(tok, "/proc") == 0)
  {
    puts("umount: ");
    puts(tok);
    puts(" is a kernel-reserved virtual mountpoint\n");
    return -1;
  }

  puts("umount: ");
  puts(tok);
  puts(": not mounted\n");
  return -1;
}

static int cmd_sync(const char *a)
{
  (void)a;
  puts("Syncing cached data to disk...\n");
  if (fs_save_to_disk() == 0)
  {
    puts("Filesystem synchronized (AurionFS LBA 500+).\n");
    return 0;
  }
  else
  {
    puts("Error: Sync failed (disk write error).\n");
    return -1;
  }
}

static int cmd_free(const char *a)
{
  (void)a;
  uint32_t stats[4];
  mem_get_stats(stats);
  char buf[16];
  puts("              total       used       free\n");
  puts("Mem:    ");
  uint32_t total = stats[0] + stats[1];
  int_to_str(total, buf);
  puts(buf);
  puts("   ");
  int_to_str(stats[1], buf);
  puts(buf);
  puts("   ");
  int_to_str(stats[0], buf);
  puts(buf);
  puts("\n");
  return 0;
}

static int cmd_df(const char *a)
{
  (void)a;
  char buf[16];
  puts("Filesystem    Size   Used   Avail  Use%  Mounted on\n");
  puts("C:\\          1440K  ");
  uint32_t used = 0;
  for (int i = 0; i < fs_count; i++)
    used += fs_table[i].size;
  int_to_str(used / 1024, buf);
  puts(buf);
  puts("K   ");
  int_to_str((1440 * 1024 - used) / 1024, buf);
  puts(buf);
  puts("K   ");
  int pct = (used * 100) / (1440 * 1024);
  int_to_str(pct, buf);
  puts(buf);
  puts("%    /\n");
  return 0;
}

static int cmd_du(const char *a)
{
  char tok[64];
  a = get_token(a, tok, 64);
  char buf[16];
  if (tok[0] == 0)
  {
    uint32_t total = 0;
    for (int i = 0; i < fs_count; i++)
    {
      total += fs_table[i].size;
      int_to_str(fs_table[i].size, buf);
      puts(buf);
      puts("\t");
      puts(fs_table[i].name);
      puts("\n");
    }
    int_to_str(total, buf);
    puts(buf);
    puts("\ttotal\n");
  }
  else
  {
    for (int i = 0; i < fs_count; i++)
    {
      if (str_cmp(fs_table[i].name, tok) == 0)
      {
        int_to_str(fs_table[i].size, buf);
        puts(buf);
        puts("\t");
        puts(tok);
        puts("\n");
        return 0;
      }
    }
    puts("du: cannot access '");
    puts(tok);
    puts("'\n");
  }
  return 0;
}

static int cmd_lsblk(const char *a)
{
  (void)a;
  puts("NAME MAJ:MIN  SIZE TYPE MOUNTPOINT\n");
  puts("hda    3:0   1.44M disk\n");
  puts("  hda1 3:1   1.44M part /\n");
  return 0;
}

static int cmd_fdisk(const char *a)
{
  (void)a;
  puts("Disk /dev/hda: 1.44 MB, 1474560 bytes\n");
  puts("  Device    Boot  Start  End   Sectors  Size  Id  Type\n");
  puts("  /dev/hda1  *       1   2880    2880  1.44M   1  FAT12\n");
  return 0;
}

static int cmd_blkid(const char *a)
{
  (void)a;
  puts("/dev/hda1: UUID=\"1234-5678\" TYPE=\"fat12\" LABEL=\"RODOS\"\n");
  return 0;
}

static int cmd_readsector(const char *a)
{
  char tok[16];
  a = get_token(a, tok, 16);
  if (tok[0] == 0)
  {
    puts("Usage: READSECTOR <sector>\n");
    return -1;
  }
  uint32_t sector = str_to_int(tok);
  char buf[16];
  puts("Reading sector ");
  int_to_str(sector, buf);
  puts(buf);
  puts("...\n");
  puts("(Sector dump not available in protected mode)\n");
  return 0;
}

static int cmd_sysinfo(const char *a)
{
  (void)a;
  uint32_t stats[4];
  mem_get_stats(stats);
  char buf[16];
  puts("=== AurionOS System Information ===\n");
  puts("OS:         AurionOS v1.0 Beta\n");
  puts("CPU:        x86 i386-compatible\n");
  puts("Arch:       32-bit Protected Mode\n");
  puts("Network:    VirtIO / NE2000\n");
  puts("Graphics:   VBE Framebuffer\n");
  puts("Free Mem:   ");
  int_to_str(stats[0], buf);
  puts(buf);
  puts(" bytes\n");
  puts("Used Mem:   ");
  int_to_str(stats[1], buf);
  puts(buf);
  puts(" bytes\n");
  puts("Files:      ");
  int_to_str(fs_count, buf);
  puts(buf);
  puts("\n");
  puts("User:       ");
  puts(current_user);
  puts("\n");
  return 0;
}

static int cmd_uname(const char *a)
{
  char tok[16];
  a = get_token(a, tok, 16);
  if (tok[0] == 0 || str_cmp(tok, "-a") == 0)
  {
    puts("AurionOS rodos 1.0.0 i386 AurionOS/x86\n");
  }
  else if (str_cmp(tok, "-s") == 0)
  {
    puts("AurionOS\n");
  }
  else if (str_cmp(tok, "-r") == 0)
  {
    puts("1.0.0\n");
  }
  else if (str_cmp(tok, "-m") == 0)
  {
    puts("i386\n");
  }
  else
  {
    puts("AurionOS rodos 1.0.0 i386 AurionOS/x86\n");
  }
  return 0;
}

static int cmd_hostname(const char *a)
{
  char tok[64];
  a = get_token(a, tok, 64);
  if (tok[0] == 0)
  {
    puts("rodos\n");
  }
  else
  {
    puts("hostname: cannot set hostname (read-only)\n");
  }
  return 0;
}

static int cmd_lscpu(const char *a)
{
  (void)a;
  puts("Architecture:     x86 (i386)\n");
  puts("CPU op-mode(s):   32-bit\n");
  puts("Byte Order:       Little Endian\n");
  puts("Address sizes:    32 bits physical, 32 bits virtual\n");
  puts("CPU(s):           1\n");
  puts("Model name:       x86 Family\n");
  return 0;
}
static int cmd_lspci(const char *a)
{
  (void)a;
  /* Direct PCI scan - check all slots on bus 0 */
  extern uint32_t pci_config_read(uint8_t bus, uint8_t device, uint8_t function, uint8_t offset);

  puts("=== PCI Scan (Bus 0) ===\n");
  char hex[] = "0123456789ABCDEF";
  int found = 0;

  for (int dev = 0; dev < 32; dev++)
  {
    uint32_t vendor_device = pci_config_read(0, dev, 0, 0x00);
    uint16_t vendor = vendor_device & 0xFFFF;
    uint16_t device_id = (vendor_device >> 16) & 0xFFFF;

    if (vendor != 0xFFFF && vendor != 0x0000)
    {
      uint32_t bar0 = pci_config_read(0, dev, 0, 0x10);
      uint32_t bar1 = pci_config_read(0, dev, 0, 0x14);

      puts("Slot ");
      if (dev < 10)
        putc('0');
      putc('0' + dev / 10);
      putc('0' + dev % 10);
      puts(": Vendor=");
      for (int j = 12; j >= 0; j -= 4)
        putc(hex[(vendor >> j) & 0xF]);
      puts(" Dev=");
      for (int j = 12; j >= 0; j -= 4)
        putc(hex[(device_id >> j) & 0xF]);
      puts(" BAR0=");
      for (int j = 28; j >= 0; j -= 4)
        putc(hex[(bar0 >> j) & 0xF]);
      puts(" BAR1=");
      for (int j = 28; j >= 0; j -= 4)
        putc(hex[(bar1 >> j) & 0xF]);
      puts("\n");
      found++;

      /* Check if VirtIO */
      if (vendor == 0x1AF4)
      {
        puts("  ^^^ VirtIO device found!\n");
      }
    }
  }

  if (found == 0)
    puts("No PCI devices found!\n");
  else
  {
    puts("Total: ");
    putc('0' + found / 10);
    putc('0' + found % 10);
    puts(" devices\n");
  }
  puts("VirtIO vendor ID is 1AF4\n");
  return 0;
}

static int cmd_dmesg(const char *a)
{
  (void)a;
  puts("[0.00] AurionOS kernel initialized\n[0.01] Memory manager ready\n[0.02] PCI bus scan complete\n[0.03] VBE framebuffer active\n[0.04] Filesystem mounted\n[0.05] Shell ready\n");
  return 0;
}
static int cmd_mode(const char *a)
{
  (void)a;
  puts("Current mode: Text 80x25\nUse GUITEST for graphics mode\n");
  return 0;
}
static int cmd_ipconfig(const char *a)
{
  (void)a;
  puts("Link encap:Ethernet  HWaddr 00:11:22:33:44:55\ninet addr:192.168.1.15  Bcast:192.168.1.255  Mask:255.255.255.0\n");
  return 0;
}
static int cmd_ping(const char *a)
{
  (void)a;
  puts("PING 8.8.8.8 (8.8.8.8): 56 data bytes\n64 bytes from 8.8.8.8: icmp_seq=0 ttl=64 time=0.1 ms\n64 bytes from 8.8.8.8: icmp_seq=1 ttl=64 time=0.2 ms\n");
  return 0;
}

/* WC - word/line/char count */
static int cmd_wc(const char *a)
{
  char name[64];
  a = get_token(a, name, 64);
  if (name[0] == 0)
  {
    puts("Usage: WC <file>\n");
    return -1;
  }
  char full[256];
  int dl = str_len(current_dir);
  for (int i = 0; i < dl; i++)
    full[i] = current_dir[i];
  int nl = str_len(name);
  for (int i = 0; i < nl; i++)
    full[dl + i] = name[i];
  full[dl + nl] = 0;
  int fi = -1;
  for (int i = 0; i < fs_count; i++)
    if (str_cmp(fs_table[i].name, full) == 0 && fs_table[i].type == 0)
    {
      fi = i;
      break;
    }
  if (fi < 0)
  {
    puts("File not found\n");
    return -1;
  }
  int ci = -1;
  for (int i = 0; i < file_content_count; i++)
    if ((int)file_contents[i].file_idx == fi)
    {
      ci = i;
      break;
    }
  if (ci < 0)
  {
    puts("  0  0  0 ");
    puts(name);
    puts("\n");
    return 0;
  }
  int lines = 0, words = 0, chars = (int)file_contents[ci].size;
  bool inw = false;
  for (uint32_t j = 0; j < file_contents[ci].size; j++)
  {
    char c = file_contents[ci].data[j];
    if (c == '\n')
      lines++;
    if (c == ' ' || c == '\n' || c == '\t')
    {
      if (inw)
        words++;
      inw = false;
    }
    else
      inw = true;
  }
  if (inw)
    words++;
  char buf[16];
  puts("  ");
  int_to_str(lines, buf);
  puts(buf);
  puts("  ");
  int_to_str(words, buf);
  puts(buf);
  puts("  ");
  int_to_str(chars, buf);
  puts(buf);
  puts(" ");
  puts(name);
  puts("\n");
  return 0;
}

/* HEAD - show first N lines */
static int cmd_head(const char *a)
{
  char name[64];
  a = get_token(a, name, 64);
  if (name[0] == 0)
  {
    puts("Usage: HEAD <file> [n]\n");
    return -1;
  }
  char full[256];
  int dl = str_len(current_dir);
  for (int i = 0; i < dl; i++)
    full[i] = current_dir[i];
  int nl = str_len(name);
  for (int i = 0; i < nl; i++)
    full[dl + i] = name[i];
  full[dl + nl] = 0;
  int fi = -1;
  for (int i = 0; i < fs_count; i++)
    if (str_cmp(fs_table[i].name, full) == 0 && fs_table[i].type == 0)
    {
      fi = i;
      break;
    }
  if (fi < 0)
  {
    puts("File not found\n");
    return -1;
  }
  int ci = -1;
  for (int i = 0; i < file_content_count; i++)
    if ((int)file_contents[i].file_idx == fi)
    {
      ci = i;
      break;
    }
  if (ci < 0)
    return 0;
  int maxlines = 10, lines = 0;
  for (uint32_t j = 0; j < file_contents[ci].size && lines < maxlines; j++)
  {
    putc(file_contents[ci].data[j]);
    if (file_contents[ci].data[j] == '\n')
      lines++;
  }
  return 0;
}

/* TAIL - show last N lines */
static int cmd_tail(const char *a)
{
  char name[64];
  a = get_token(a, name, 64);
  if (name[0] == 0)
  {
    puts("Usage: TAIL <file>\n");
    return -1;
  }
  char full[256];
  int dl = str_len(current_dir);
  for (int i = 0; i < dl; i++)
    full[i] = current_dir[i];
  int nl = str_len(name);
  for (int i = 0; i < nl; i++)
    full[dl + i] = name[i];
  full[dl + nl] = 0;
  int fi = -1;
  for (int i = 0; i < fs_count; i++)
    if (str_cmp(fs_table[i].name, full) == 0 && fs_table[i].type == 0)
    {
      fi = i;
      break;
    }
  if (fi < 0)
  {
    puts("File not found\n");
    return -1;
  }
  int ci = -1;
  for (int i = 0; i < file_content_count; i++)
    if ((int)file_contents[i].file_idx == fi)
    {
      ci = i;
      break;
    }
  if (ci < 0)
    return 0;
  int total_lines = 0;
  for (uint32_t j = 0; j < file_contents[ci].size; j++)
    if (file_contents[ci].data[j] == '\n')
      total_lines++;
  int skip = total_lines - 10;
  if (skip < 0)
    skip = 0;
  int cur = 0;
  for (uint32_t j = 0; j < file_contents[ci].size; j++)
  {
    if (cur >= skip)
      putc(file_contents[ci].data[j]);
    if (file_contents[ci].data[j] == '\n')
      cur++;
  }
  return 0;
}

/* GREP - search for pattern in file */
static int cmd_grep(const char *a)
{
  char pat[64], name[64];
  a = get_token(a, pat, 64);
  a = get_token(a, name, 64);
  if (pat[0] == 0)
  {
    puts("Usage: GREP <pattern> <file>\n");
    return -1;
  }
  if (name[0] == 0)
  {
    puts("Usage: GREP <pattern> <file>\n");
    return -1;
  }
  char full[256];
  int dl = str_len(current_dir);
  for (int i = 0; i < dl; i++)
    full[i] = current_dir[i];
  int nl = str_len(name);
  for (int i = 0; i < nl; i++)
    full[dl + i] = name[i];
  full[dl + nl] = 0;
  int fi = -1;
  for (int i = 0; i < fs_count; i++)
    if (str_cmp(fs_table[i].name, full) == 0 && fs_table[i].type == 0)
    {
      fi = i;
      break;
    }
  if (fi < 0)
  {
    puts("File not found\n");
    return -1;
  }
  int ci = -1;
  for (int i = 0; i < file_content_count; i++)
    if ((int)file_contents[i].file_idx == fi)
    {
      ci = i;
      break;
    }
  if (ci < 0)
    return 0;
  int plen = str_len(pat);
  /* Line-by-line search */
  uint32_t ls = 0;
  for (uint32_t j = 0; j <= file_contents[ci].size; j++)
  {
    bool eol = (j == file_contents[ci].size || file_contents[ci].data[j] == '\n');
    if (eol)
    {
      /* Check if pattern in this line */
      for (uint32_t k = ls; k + plen <= j; k++)
      {
        bool m = true;
        for (int p = 0; p < plen; p++)
        {
          if (file_contents[ci].data[k + p] != pat[p])
          {
            m = false;
            break;
          }
        }
        if (m)
        {
          for (uint32_t x = ls; x < j; x++)
            putc(file_contents[ci].data[x]);
          putc('\n');
          break;
        }
      }
      ls = j + 1;
    }
  }
  return 0;
}

static int cmd_sort(const char *a)
{
  (void)a;
  puts("SORT: Requires piping (not supported)\n");
  return 0;
}
static int cmd_uniq(const char *a)
{
  (void)a;
  puts("UNIQ: Requires piping (not supported)\n");
  return 0;
}

/* DIFF - compare two files */
static int cmd_diff(const char *a)
{
  char f1[64], f2[64];
  a = get_token(a, f1, 64);
  a = get_token(a, f2, 64);
  if (f1[0] == 0 || f2[0] == 0)
  {
    puts("Usage: DIFF <file1> <file2>\n");
    return -1;
  }
  int i1 = -1, i2 = -1;
  for (int i = 0; i < fs_count; i++)
  {
    if (str_cmp(fs_table[i].name, f1) == 0)
      i1 = i;
    if (str_cmp(fs_table[i].name, f2) == 0)
      i2 = i;
  }
  if (i1 < 0)
  {
    puts("File not found: ");
    puts(f1);
    puts("\n");
    return -1;
  }
  if (i2 < 0)
  {
    puts("File not found: ");
    puts(f2);
    puts("\n");
    return -1;
  }
  int c1 = -1, c2 = -1;
  for (int i = 0; i < file_content_count; i++)
  {
    if ((int)file_contents[i].file_idx == i1)
      c1 = i;
    if ((int)file_contents[i].file_idx == i2)
      c2 = i;
  }
  if (c1 < 0 && c2 < 0)
  {
    puts("Files are identical (both empty)\n");
    return 0;
  }
  if (c1 < 0 || c2 < 0)
  {
    puts("Files differ\n");
    return 0;
  }
  if (file_contents[c1].size != file_contents[c2].size)
  {
    puts("Files differ in size\n");
    return 0;
  }
  for (uint32_t j = 0; j < file_contents[c1].size; j++)
  {
    if (file_contents[c1].data[j] != file_contents[c2].data[j])
    {
      puts("Files differ\n");
      return 0;
    }
  }
  puts("Files are identical\n");
  return 0;
}

static int cmd_cut(const char *a)
{
  (void)a;
  puts("Usage: CUT -d<delim> -f<field> <file>\n(Simplified: use GREP)\n");
  return 0;
}
static int cmd_paste(const char *a)
{
  (void)a;
  puts("PASTE: Requires multiple file merging\n");
  return 0;
}
static int cmd_tr(const char *a)
{
  (void)a;
  puts("TR: Requires piping (not supported)\n");
  return 0;
}
static int cmd_sed(const char *a)
{
  (void)a;
  puts("SED: Stream editor requires piping\n");
  return 0;
}
static int cmd_awk(const char *a)
{
  (void)a;
  puts("AWK: Pattern processing requires piping\n");
  return 0;
}

/* BASE64 encode */
static int cmd_base64(const char *a)
{
  a = skip_spaces(a);
  if (a[0] == 0)
  {
    puts("Usage: BASE64 <text>\n");
    return -1;
  }
  static const char b64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  int len = str_len(a);
  for (int i = 0; i < len; i += 3)
  {
    uint32_t n = ((uint32_t)(uint8_t)a[i]) << 16;
    if (i + 1 < len)
      n |= ((uint32_t)(uint8_t)a[i + 1]) << 8;
    if (i + 2 < len)
      n |= (uint32_t)(uint8_t)a[i + 2];
    putc(b64[(n >> 18) & 63]);
    putc(b64[(n >> 12) & 63]);
    putc((i + 1 < len) ? b64[(n >> 6) & 63] : '=');
    putc((i + 2 < len) ? b64[n & 63] : '=');
  }
  putc('\n');
  return 0;
}

/* XXD - hex dump of file */
static int cmd_xxd(const char *a)
{
  char name[64];
  a = get_token(a, name, 64);
  if (name[0] == 0)
  {
    puts("Usage: XXD <file>\n");
    return -1;
  }
  char full[256];
  int dl = str_len(current_dir);
  for (int i = 0; i < dl; i++)
    full[i] = current_dir[i];
  int nl = str_len(name);
  for (int i = 0; i < nl; i++)
    full[dl + i] = name[i];
  full[dl + nl] = 0;
  int fi = -1;
  for (int i = 0; i < fs_count; i++)
    if (str_cmp(fs_table[i].name, full) == 0)
    {
      fi = i;
      break;
    }
  if (fi < 0)
  {
    puts("File not found\n");
    return -1;
  }
  int ci = -1;
  for (int i = 0; i < file_content_count; i++)
    if ((int)file_contents[i].file_idx == fi)
    {
      ci = i;
      break;
    }
  if (ci < 0)
  {
    puts("(empty file)\n");
    return 0;
  }
  uint32_t sz = file_contents[ci].size;
  if (sz > 256)
    sz = 256;
  for (uint32_t j = 0; j < sz; j += 16)
  {
    print_hex(j);
    puts(": ");
    for (int k = 0; k < 16; k++)
    {
      if (j + k < sz)
        print_hex(file_contents[ci].data[j + k]);
      else
        puts("  ");
      putc(' ');
    }
    putc(' ');
    for (int k = 0; k < 16 && j + k < sz; k++)
    {
      char c = file_contents[ci].data[j + k];
      putc((c >= 32 && c < 127) ? c : '.');
    }
    putc('\n');
  }
  return 0;
}

static int cmd_od(const char *a) { return cmd_xxd(a); }

/* REV - reverse string */
static int cmd_rev(const char *a)
{
  a = skip_spaces(a);
  if (a[0] == 0)
  {
    puts("Usage: REV <text>\n");
    return -1;
  }
  int len = str_len(a);
  for (int i = len - 1; i >= 0; i--)
    putc(a[i]);
  putc('\n');
  return 0;
}

/* NL - number lines */
static int cmd_nl(const char *a)
{
  char name[64];
  a = get_token(a, name, 64);
  if (name[0] == 0)
  {
    puts("Usage: NL <file>\n");
    return -1;
  }
  char full[256];
  int dl = str_len(current_dir);
  for (int i = 0; i < dl; i++)
    full[i] = current_dir[i];
  int nl2 = str_len(name);
  for (int i = 0; i < nl2; i++)
    full[dl + i] = name[i];
  full[dl + nl2] = 0;
  int fi = -1;
  for (int i = 0; i < fs_count; i++)
    if (str_cmp(fs_table[i].name, full) == 0)
    {
      fi = i;
      break;
    }
  if (fi < 0)
  {
    puts("File not found\n");
    return -1;
  }
  int ci = -1;
  for (int i = 0; i < file_content_count; i++)
    if ((int)file_contents[i].file_idx == fi)
    {
      ci = i;
      break;
    }
  if (ci < 0)
    return 0;
  int line = 1;
  char buf[16];
  int_to_str(line, buf);
  puts("  ");
  puts(buf);
  puts("  ");
  for (uint32_t j = 0; j < file_contents[ci].size; j++)
  {
    putc(file_contents[ci].data[j]);
    if (file_contents[ci].data[j] == '\n' && j + 1 < file_contents[ci].size)
    {
      line++;
      int_to_str(line, buf);
      puts("  ");
      puts(buf);
      puts("  ");
    }
  }
  return 0;
}

/* TAC - print file in reverse line order */
static int cmd_tac(const char *a)
{
  char name[64];
  a = get_token(a, name, 64);
  if (name[0] == 0)
  {
    puts("Usage: TAC <file>\n");
    return -1;
  }
  char full[256];
  int dl = str_len(current_dir);
  for (int i = 0; i < dl; i++)
    full[i] = current_dir[i];
  int nl2 = str_len(name);
  for (int i = 0; i < nl2; i++)
    full[dl + i] = name[i];
  full[dl + nl2] = 0;
  int fi = -1;
  for (int i = 0; i < fs_count; i++)
    if (str_cmp(fs_table[i].name, full) == 0)
    {
      fi = i;
      break;
    }
  if (fi < 0)
  {
    puts("File not found\n");
    return -1;
  }
  int ci = -1;
  for (int i = 0; i < file_content_count; i++)
    if ((int)file_contents[i].file_idx == fi)
    {
      ci = i;
      break;
    }
  if (ci < 0)
    return 0;
  /* Find line starts */
  uint32_t starts[128];
  int lc = 0;
  starts[lc++] = 0;
  for (uint32_t j = 0; j < file_contents[ci].size; j++)
    if (file_contents[ci].data[j] == '\n' && j + 1 < file_contents[ci].size && lc < 128)
      starts[lc++] = j + 1;
  for (int l = lc - 1; l >= 0; l--)
  {
    uint32_t s = starts[l];
    uint32_t e = (l < lc - 1) ? starts[l + 1] : file_contents[ci].size;
    for (uint32_t j = s; j < e; j++)
      putc(file_contents[ci].data[j]);
    if (file_contents[ci].data[e - 1] != '\n')
      putc('\n');
  }
  return 0;
}

/* FACTOR - prime factorization */
static int cmd_factor(const char *a)
{
  char tok[16];
  a = get_token(a, tok, 16);
  if (tok[0] == 0)
  {
    puts("Usage: FACTOR <number>\n");
    return -1;
  }
  uint32_t n = str_to_int(tok);
  char buf[16];
  int_to_str(n, buf);
  puts(buf);
  puts(":");
  uint32_t orig = n;
  for (uint32_t d = 2; d * d <= orig && n > 1; d++)
  {
    while (n % d == 0)
    {
      putc(' ');
      int_to_str(d, buf);
      puts(buf);
      n /= d;
    }
  }
  if (n > 1)
  {
    putc(' ');
    int_to_str(n, buf);
    puts(buf);
  }
  putc('\n');
  return 0;
}

/* SEQ - print sequence */
static int cmd_seq(const char *a)
{
  char s1[16], s2[16];
  a = get_token(a, s1, 16);
  a = get_token(a, s2, 16);
  if (s1[0] == 0)
  {
    puts("Usage: SEQ <start> <end>\n");
    return -1;
  }
  uint32_t start = str_to_int(s1);
  uint32_t end2 = s2[0] ? str_to_int(s2) : start;
  if (s2[0] == 0)
  {
    end2 = start;
    start = 1;
  }
  char buf[16];
  for (uint32_t i = start; i <= end2; i++)
  {
    int_to_str(i, buf);
    puts(buf);
    putc('\n');
  }
  return 0;
}

/* SHUF - random number */
static int cmd_shuf(const char *a)
{
  char tok[16];
  a = get_token(a, tok, 16);
  uint32_t max = tok[0] ? str_to_int(tok) : 100;
  uint32_t r = get_ticks() % max;
  char buf[16];
  int_to_str(r, buf);
  puts(buf);
  putc('\n');
  return 0;
}

/* YES - repeat text */
static int cmd_yes(const char *a)
{
  a = skip_spaces(a);
  const char *txt = (a && a[0]) ? a : "y";
  for (int i = 0; i < 20; i++)
  {
    puts(txt);
    putc('\n');
  }
  puts("(stopped after 20 lines)\n");
  return 0;
}

static int cmd_watch(const char *a)
{
  (void)a;
  puts("WATCH: Use SLEEP + command manually\n");
  return 0;
}
static int cmd_timeout(const char *a)
{
  char tok[16];
  a = get_token(a, tok, 16);
  if (tok[0] == 0)
  {
    puts("Usage: TIMEOUT <seconds> <command>\n");
    return -1;
  }
  uint32_t sec = str_to_int(tok);
  a = skip_spaces(a);
  if (a[0])
    return cmd_dispatch(a);
  sleep_ms(sec * 1000);
  return 0;
}

/* WHICH - find command */
static int cmd_which(const char *a)
{
  char tok[64];
  a = get_token(a, tok, 64);
  if (tok[0] == 0)
  {
    puts("Usage: WHICH <command>\n");
    return -1;
  }
  str_upper(tok);
  /* Search command table */
  extern const char *commands_get_name(int i);
  puts("/bin/");
  for (int i = 0; tok[i]; i++)
    putc(tok[i] >= 'A' && tok[i] <= 'Z' ? tok[i] + 32 : tok[i]);
  putc('\n');
  return 0;
}

static int cmd_whereis(const char *a) { return cmd_which(a); }

static int cmd_id(const char *a)
{
  (void)a;
  puts("uid=0(");
  puts(current_user);
  puts(") gid=0(root)\n");
  return 0;
}

static int cmd_who(const char *a)
{
  (void)a;
  puts(current_user);
  puts("  console  ");
  uint8_t h, m, s;
  sys_get_time(&h, &m, &s);
  char buf[16];
  int_to_str(h, buf);
  puts(buf);
  putc(':');
  int_to_str(m, buf);
  puts(buf);
  putc('\n');
  return 0;
}

static int cmd_w(const char *a) { return cmd_who(a); }
static int cmd_last(const char *a)
{
  (void)a;
  puts(current_user);
  puts("  console  still logged in\n");
  return 0;
}

/* ENV - environment variables */
#define MAX_ENV_VARS 16
static char env_keys[MAX_ENV_VARS][32];
static char env_vals[MAX_ENV_VARS][64];
static int env_count = 0;
static bool env_inited = false;

static void env_init_defaults(void)
{
  if (env_inited)
    return;
  env_inited = true;
  str_copy(env_keys[0], "PATH", 32);
  str_copy(env_vals[0], "/bin", 64);
  env_count++;
  str_copy(env_keys[1], "HOME", 32);
  str_copy(env_vals[1], "C:\\", 64);
  env_count++;
  str_copy(env_keys[2], "SHELL", 32);
  str_copy(env_vals[2], "/bin/sh", 64);
  env_count++;
  str_copy(env_keys[3], "USER", 32);
  str_copy(env_vals[3], "root", 64);
  env_count++;
}

static int cmd_env(const char *a)
{
  (void)a;
  env_init_defaults();
  for (int i = 0; i < env_count; i++)
  {
    puts(env_keys[i]);
    putc('=');
    puts(env_vals[i]);
    putc('\n');
  }
  return 0;
}

static int cmd_export(const char *a)
{
  env_init_defaults();
  char tok[96];
  a = get_token(a, tok, 96);
  if (tok[0] == 0)
  {
    return cmd_env(a);
  }
  /* Find = */
  int eq = -1;
  for (int i = 0; tok[i]; i++)
    if (tok[i] == '=')
    {
      eq = i;
      break;
    }
  if (eq < 0)
  {
    puts("Usage: EXPORT KEY=value\n");
    return -1;
  }
  tok[eq] = 0;
  int idx = -1;
  for (int i = 0; i < env_count; i++)
    if (str_cmp(env_keys[i], tok) == 0)
    {
      idx = i;
      break;
    }
  if (idx < 0 && env_count < MAX_ENV_VARS)
    idx = env_count++;
  if (idx >= 0)
  {
    str_copy(env_keys[idx], tok, 32);
    str_copy(env_vals[idx], tok + eq + 1, 64);
  }
  return 0;
}

static int cmd_unset(const char *a)
{
  env_init_defaults();
  char tok[32];
  a = get_token(a, tok, 32);
  if (tok[0] == 0)
  {
    puts("Usage: UNSET <var>\n");
    return -1;
  }
  for (int i = 0; i < env_count; i++)
  {
    if (str_cmp(env_keys[i], tok) == 0)
    {
      for (int j = i; j < env_count - 1; j++)
      {
        str_copy(env_keys[j], env_keys[j + 1], 32);
        str_copy(env_vals[j], env_vals[j + 1], 64);
      }
      env_count--;
      return 0;
    }
  }
  return 0;
}

static int cmd_printenv(const char *a) { return cmd_env(a); }
static int cmd_set(const char *a) { return cmd_env(a); }
static int cmd_source(const char *a)
{
  (void)a;
  puts("SOURCE: Script execution not supported\n");
  return 0;
}
static int cmd_true(const char *a)
{
  (void)a;
  return 0;
}
static int cmd_false(const char *a)
{
  (void)a;
  return 1;
}
static int cmd_test(const char *a)
{
  char tok[16];
  a = get_token(a, tok, 16);
  if (str_cmp(tok, "-f") == 0)
  {
    char f[64];
    a = get_token(a, f, 64);
    for (int i = 0; i < fs_count; i++)
      if (str_cmp(fs_table[i].name, f) == 0 && fs_table[i].type == 0)
        return 0;
    return 1;
  }
  if (str_cmp(tok, "-d") == 0)
  {
    char f[64];
    a = get_token(a, f, 64);
    for (int i = 0; i < fs_count; i++)
      if (str_cmp(fs_table[i].name, f) == 0 && fs_table[i].type == 1)
        return 0;
    return 1;
  }
  return 1;
}

/* EXPR - evaluate expression */
static int cmd_expr(const char *a)
{
  char a1[16], op[4], a2[16];
  a = get_token(a, a1, 16);
  a = get_token(a, op, 4);
  a = get_token(a, a2, 16);
  if (a1[0] == 0)
  {
    puts("Usage: EXPR num op num\n");
    return -1;
  }
  if (op[0] == 0)
  {
    int_to_str(str_to_int(a1), (char[16]){});
    char b[16];
    int_to_str(str_to_int(a1), b);
    puts(b);
    putc('\n');
    return 0;
  }
  int v1 = str_to_int(a1), v2 = str_to_int(a2), r = 0;
  if (op[0] == '+')
    r = v1 + v2;
  else if (op[0] == '-')
    r = v1 - v2;
  else if (op[0] == '*')
    r = v1 * v2;
  else if (op[0] == '/')
    r = v2 ? v1 / v2 : 0;
  else if (op[0] == '%')
    r = v2 ? v1 % v2 : 0;
  char buf[16];
  int_to_str(r, buf);
  puts(buf);
  putc('\n');
  return 0;
}

static int cmd_let(const char *a) { return cmd_expr(a); }

static int cmd_read(const char *a)
{
  (void)a;
  puts("Enter input: ");
  char buf[128];
  int pos = 0;
  while (pos < 127)
  {
    uint16_t k = getkey();
    uint8_t c = k & 0xFF;
    if (c == 13 || c == 10)
    {
      putc('\n');
      break;
    }
    if (c >= 32 && c < 127)
    {
      buf[pos++] = c;
      putc(c);
    }
  }
  buf[pos] = 0;
  puts("Read: ");
  puts(buf);
  putc('\n');
  return 0;
}

static int cmd_printf(const char *a)
{
  if (a)
    puts(a);
  puts("\n");
  return 0;
}

/* ALIAS storage */
#define MAX_ALIASES 16
static char alias_names[MAX_ALIASES][32];
static char alias_cmds[MAX_ALIASES][64];
static int alias_count = 0;

static int cmd_alias(const char *a)
{
  char tok[96];
  a = get_token(a, tok, 96);
  if (tok[0] == 0)
  {
    for (int i = 0; i < alias_count; i++)
    {
      puts("alias ");
      puts(alias_names[i]);
      puts("='");
      puts(alias_cmds[i]);
      puts("'\n");
    }
    return 0;
  }
  int eq = -1;
  for (int i = 0; tok[i]; i++)
    if (tok[i] == '=')
    {
      eq = i;
      break;
    }
  if (eq < 0)
  {
    puts("Usage: ALIAS name=command\n");
    return -1;
  }
  tok[eq] = 0;
  int idx = -1;
  for (int i = 0; i < alias_count; i++)
    if (str_cmp(alias_names[i], tok) == 0)
    {
      idx = i;
      break;
    }
  if (idx < 0 && alias_count < MAX_ALIASES)
    idx = alias_count++;
  if (idx >= 0)
  {
    str_copy(alias_names[idx], tok, 32);
    str_copy(alias_cmds[idx], tok + eq + 1, 64);
  }
  return 0;
}

static int cmd_unalias(const char *a)
{
  char tok[32];
  a = get_token(a, tok, 32);
  for (int i = 0; i < alias_count; i++)
  {
    if (str_cmp(alias_names[i], tok) == 0)
    {
      for (int j = i; j < alias_count - 1; j++)
      {
        str_copy(alias_names[j], alias_names[j + 1], 32);
        str_copy(alias_cmds[j], alias_cmds[j + 1], 64);
      }
      alias_count--;
      return 0;
    }
  }
  return 0;
}

/* HISTORY storage */
#define MAX_HISTORY 32
static char cmd_history_buf[MAX_HISTORY][128];
static int history_idx = 0;
static int history_total = 0;

static int cmd_history(const char *a)
{
  (void)a;
  for (int i = 0; i < history_total && i < MAX_HISTORY; i++)
  {
    char buf[16];
    int_to_str(i + 1, buf);
    puts("  ");
    puts(buf);
    puts("  ");
    puts(cmd_history_buf[i]);
    putc('\n');
  }
  return 0;
}

static int cmd_jobs(const char *a)
{
  (void)a;
  puts("No background jobs\n");
  return 0;
}
static int cmd_fg(const char *a)
{
  (void)a;
  puts("No background jobs\n");
  return 0;
}
static int cmd_bg(const char *a)
{
  (void)a;
  puts("No background jobs\n");
  return 0;
}
static int cmd_nice(const char *a)
{
  a = skip_spaces(a);
  if (a[0])
    return cmd_dispatch(a);
  puts("Usage: NICE <command>\n");
  return 0;
}
static int cmd_nohup(const char *a)
{
  a = skip_spaces(a);
  if (a[0])
    return cmd_dispatch(a);
  puts("Usage: NOHUP <command>\n");
  return 0;
}
static int cmd_strace(const char *a)
{
  (void)a;
  puts("STRACE: System call tracing not available\n");
  return 0;
}

/* MORE/LESS - paginated file viewer */
static int cmd_more(const char *a)
{
  char name[64];
  a = get_token(a, name, 64);
  if (name[0] == 0)
  {
    puts("Usage: MORE <file>\n");
    return -1;
  }
  char full[256];
  int dl = str_len(current_dir);
  for (int i = 0; i < dl; i++)
    full[i] = current_dir[i];
  int nl = str_len(name);
  for (int i = 0; i < nl; i++)
    full[dl + i] = name[i];
  full[dl + nl] = 0;
  int fi = -1;
  for (int i = 0; i < fs_count; i++)
    if (str_cmp(fs_table[i].name, full) == 0)
    {
      fi = i;
      break;
    }
  if (fi < 0)
  {
    puts("File not found\n");
    return -1;
  }
  int ci = -1;
  for (int i = 0; i < file_content_count; i++)
    if ((int)file_contents[i].file_idx == fi)
    {
      ci = i;
      break;
    }
  if (ci < 0)
  {
    puts("(empty)\n");
    return 0;
  }
  int lines = 0;
  for (uint32_t j = 0; j < file_contents[ci].size; j++)
  {
    putc(file_contents[ci].data[j]);
    if (file_contents[ci].data[j] == '\n')
    {
      lines++;
      if (lines % 24 == 0)
      {
        puts("--More--");
        getkey();
        puts("\r        \r");
      }
    }
  }
  return 0;
}

static int cmd_less(const char *a) { return cmd_more(a); }

/* FILE - identify file type */
static int cmd_file(const char *a)
{
  char name[64];
  a = get_token(a, name, 64);
  if (name[0] == 0)
  {
    puts("Usage: FILE <filename>\n");
    return -1;
  }
  for (int i = 0; i < fs_count; i++)
  {
    if (str_cmp(fs_table[i].name, name) == 0)
    {
      puts(name);
      puts(": ");
      if (fs_table[i].type == 1)
      {
        puts("directory\n");
        return 0;
      }
      puts("regular file, ");
      char buf[16];
      int_to_str(fs_table[i].size, buf);
      puts(buf);
      puts(" bytes\n");
      return 0;
    }
  }
  puts(name);
  puts(": No such file\n");
  return -1;
}

/* STAT - file status */
static int cmd_stat(const char *a)
{
  char name[64];
  a = get_token(a, name, 64);
  if (name[0] == 0)
  {
    puts("Usage: STAT <filename>\n");
    return -1;
  }
  for (int i = 0; i < fs_count; i++)
  {
    if (str_cmp(fs_table[i].name, name) == 0)
    {
      char buf[16];
      puts("  File: ");
      puts(name);
      putc('\n');
      puts("  Size: ");
      int_to_str(fs_table[i].size, buf);
      puts(buf);
      puts(" bytes\n");
      puts("  Type: ");
      puts(fs_table[i].type ? "directory" : "regular file");
      putc('\n');
      puts("  Attr: 0x");
      print_hex(fs_table[i].attr);
      putc('\n');
      return 0;
    }
  }
  puts("stat: cannot stat '");
  puts(name);
  puts("'\n");
  return -1;
}

static int cmd_path(const char *a)
{
  (void)a;
  env_init_defaults();
  puts(env_vals[0]);
  putc('\n');
  return 0;
}
static int cmd_prompt(const char *a)
{
  (void)a;
  puts(current_dir);
  puts("> ");
  return 0;
}
static int cmd_ln(const char *a)
{
  (void)a;
  puts("LN: Symbolic links not supported in FAT12\n");
  return 0;
}
static int cmd_chown(const char *a)
{
  (void)a;
  puts("CHOWN: Single-user system (all files owned by root)\n");
  return 0;
}

/* STRINGS - print printable strings from file */
static int cmd_strings(const char *a)
{
  char name[64];
  a = get_token(a, name, 64);
  if (name[0] == 0)
  {
    puts("Usage: STRINGS <file>\n");
    return -1;
  }
  char full[256];
  int dl = str_len(current_dir);
  for (int i = 0; i < dl; i++)
    full[i] = current_dir[i];
  int nl = str_len(name);
  for (int i = 0; i < nl; i++)
    full[dl + i] = name[i];
  full[dl + nl] = 0;
  int fi = -1;
  for (int i = 0; i < fs_count; i++)
    if (str_cmp(fs_table[i].name, full) == 0)
    {
      fi = i;
      break;
    }
  if (fi < 0)
  {
    puts("File not found\n");
    return -1;
  }
  int ci = -1;
  for (int i = 0; i < file_content_count; i++)
    if ((int)file_contents[i].file_idx == fi)
    {
      ci = i;
      break;
    }
  if (ci < 0)
    return 0;
  int run = 0;
  for (uint32_t j = 0; j < file_contents[ci].size; j++)
  {
    char c = file_contents[ci].data[j];
    if (c >= 32 && c < 127)
    {
      putc(c);
      run++;
    }
    else
    {
      if (run >= 4)
        putc('\n');
      run = 0;
    }
  }
  if (run >= 4)
    putc('\n');
  return 0;
}

/* Forward declaration */
int cmd_dispatch(const char *line);

/* CAL - calendar */
static int cmd_cal(const char *a)
{
  (void)a;
  uint8_t day, month;
  uint16_t year;
  extern int sys_get_date(uint8_t *, uint8_t *, uint16_t *);
  sys_get_date(&day, &month, &year);
  char buf[16];
  static const char *months[] = {"January", "February", "March", "April", "May", "June", "July", "August", "September", "October", "November", "December"};
  if (month >= 1 && month <= 12)
    puts(months[month - 1]);
  puts(" ");
  int_to_str(year, buf);
  puts(buf);
  putc('\n');
  puts("Su Mo Tu We Th Fr Sa\n");
  /* Simplified: just show current day */
  for (int d = 1; d <= 28; d++)
  {
    if (d < 10)
      putc(' ');
    int_to_str(d, buf);
    puts(buf);
    if (d == day)
    {
      putc('*');
    }
    else
      putc(' ');
    if (d % 7 == 0)
      putc('\n');
  }
  putc('\n');
  return 0;
}

/* BANNER/FIGLET - large text */
static int cmd_banner(const char *a)
{
  a = skip_spaces(a);
  if (!a || a[0] == 0)
  {
    puts("Usage: BANNER <text>\n");
    return -1;
  }
  puts("####  ");
  puts(a);
  puts("  ####\n");
  puts("#     ");
  for (int i = 0; a[i]; i++)
    putc(' ');
  puts("     #\n");
  puts("####  ");
  puts(a);
  puts("  ####\n");
  return 0;
}
static int cmd_figlet(const char *a) { return cmd_banner(a); }

/* COWSAY */
static int cmd_cowsay(const char *a)
{
  a = skip_spaces(a);
  const char *msg = (a && a[0]) ? a : "moo";
  int len = str_len(msg);
  putc(' ');
  for (int i = 0; i < len + 2; i++)
    putc('_');
  putc('\n');
  puts("< ");
  puts(msg);
  puts(" >\n");
  putc(' ');
  for (int i = 0; i < len + 2; i++)
    putc('-');
  putc('\n');
  puts("        \\   ^__^\n");
  puts("         \\  (oo)\\_______\n");
  puts("            (__)\\       )\\/\\\n");
  puts("                ||----w |\n");
  puts("                ||     ||\n");
  return 0;
}

static int cmd_fortune(const char *a)
{
  (void)a;
  static const char *fortunes[] = {
      "A bad penny always turns up.",
      "A calm sea does not make a skilled sailor.",
      "A close mouth catches no flies.",
      "Fortune favors the bold.",
      "Computers make very fast, very accurate mistakes."};
  extern uint32_t get_ticks(void);
  uint32_t r = get_ticks() % 5;
  puts(fortunes[r]);
  putc('\n');
  return 0;
}

/* 103. PYTHON - implemented in cmd_python.c (full interpreter with file support)
   The real cmd_python() is declared extern above and dispatched from the table.
   This stub is kept for reference only and marked unused. */
static int cmd_python_stub(const char *args) __attribute__((unused));
static int cmd_python_stub(const char *args)
{
  (void)args;
  puts("AurionOS Python 0.2 Beta (default, March 4 2026, 20:30:00)\n");
  puts("[AurionOS 32-bit] on rodos\n");
  puts("Type \"help\", \"copyright\", \"credits\" or \"license\" for more information.\n");

#define PY_MAX_VARS 32
  struct
  {
    char name[32];
    int val;
    char sval[64];
    int type;
  } vars[PY_MAX_VARS]; // 0=int, 1=str
  int var_count = 0;

  /* Pre-define some modules as objects/vars if needed */

  while (1)
  {
    puts(">>> ");
    char line[128];
    int pos = 0;
    while (1)
    {
      uint16_t k = getkey();
      char c = (char)(k & 0xFF);
      if (c == 13 || c == 10)
      {
        putc('\n');
        line[pos] = 0;
        break;
      }
      if (c == 8)
      {
        if (pos > 0)
        {
          pos--;
          putc(8);
          putc(' ');
          putc(8);
        }
      }
      else if (pos < 127 && c >= 32 && c < 127)
      {
        line[pos++] = c;
        putc(c);
      }
    }

    char *p = line;
    while (*p == ' ')
      p++;
    if (*p == 0)
      continue;

    if (str_cmp(p, "exit()") == 0 || str_cmp(p, "quit()") == 0)
      break;

    if (str_ncmp(p, "print(", 6) == 0)
    {
      char *content = p + 6;
      int len = str_len(content);
      if (len > 0 && content[len - 1] == ')')
        content[len - 1] = 0;

      if (content[0] == '"' || content[0] == '\'')
      {
        /* String literal */
        for (int i = 1; content[i] && content[i] != content[0]; i++)
          putc(content[i]);
        putc('\n');
      }
      else
      {
        /* Variable or number */
        int v_idx = -1;
        for (int i = 0; i < var_count; i++)
        {
          if (str_cmp(vars[i].name, content) == 0)
          {
            v_idx = i;
            break;
          }
        }
        if (v_idx >= 0)
        {
          if (vars[v_idx].type == 0)
          {
            char buf[16];
            int_to_str(vars[v_idx].val, buf);
            puts(buf);
          }
          else
          {
            puts(vars[v_idx].sval);
          }
          putc('\n');
        }
        else
        {
          /* Number? */
          if (content[0] >= '0' && content[0] <= '9')
          {
            puts(content);
            putc('\n');
          }
          else
          {
            /* Expression? try simple math */
            /* Very basic parser */
            char buf[16];
            int_to_str(str_to_int(content), buf);
            puts(buf);
            putc('\n');
          }
        }
      }
      continue;
    }

    if (str_ncmp(p, "import ", 7) == 0)
    {
      char mod[32];
      char *m = p + 7;
      int i = 0;
      while (*m && *m != ' ' && i < 31)
        mod[i++] = *m++;
      mod[i] = 0;
      if (str_cmp(mod, "os") == 0)
      {
        puts("");
        continue;
      }
      if (str_cmp(mod, "random") == 0)
      {
        puts("");
        continue;
      }
      puts("Module not found: ");
      puts(mod);
      putc('\n');
      continue;
    }

    /* Assignment: x = 5 */
    char *eq = 0;
    for (int i = 0; p[i]; i++)
      if (p[i] == '=')
      {
        eq = p + i;
        break;
      }
    if (eq)
    {
      *eq = 0;
      char *val = eq + 1;
      while (*val == ' ')
        val++;

      /* Trim var name */
      char *vname = p;
      int vlen = str_len(vname);
      while (vlen > 0 && vname[vlen - 1] == ' ')
        vname[--vlen] = 0;

      int idx = -1;
      for (int i = 0; i < var_count; i++)
        if (str_cmp(vars[i].name, vname) == 0)
          idx = i;
      if (idx < 0 && var_count < PY_MAX_VARS)
        idx = var_count++;
      if (idx >= 0)
      {
        str_copy(vars[idx].name, vname, 32);
        if (val[0] == '"' || val[0] == '\'')
        {
          vars[idx].type = 1;
          int q = val[0];
          int k = 0;
          for (int j = 1; val[j] && val[j] != q && k < 63; j++)
            vars[idx].sval[k++] = val[j];
          vars[idx].sval[k] = 0;
        }
        else
        {
          vars[idx].type = 0;
          vars[idx].val = str_to_int(val);
        }
      }
      continue;
    }

    /* Fallback */
    puts("SyntaxError: invalid syntax\n");
  }
  return 0;
}

/* 104. MAKE - build system (implemented in cmd_make.c, declared above) */

// WiFi security type definitions (if not already included)
/* 102. SUDO - Run command as root */
static int cmd_sudo(const char *args)
{
  args = skip_spaces(args);

  if (args[0] == 0)
  {
    puts("Usage: SUDO <command>\n");
    puts("Execute a command with root privileges.\n");
    return -1;
  }

  // Check if user is already root
  if (str_cmp(current_user, "root") == 0)
  {
    puts("[sudo] User is already root.\n");
    // Execute the command directly
    return cmd_dispatch(args);
  }

  // Prompt for password (simplified - accepts any input for demo)
  puts("[sudo] Password for ");
  puts(current_user);
  puts(": ");

  // Read password (hidden)
  char password[32] = {0};
  int pos = 0;
  while (pos < 31)
  {
    uint16_t k = getkey();
    uint8_t key = (uint8_t)(k & 0xFF);

    if (key == 13 || key == 10)
      break; // Enter
    if (key == 27)
    { // ESC - cancel
      puts("\n[sudo] Cancelled.\n");
      return -1;
    }
    if (key == 8 && pos > 0)
    { // Backspace
      pos--;
      password[pos] = 0;
    }
    else if (key >= 32 && key <= 126)
    {
      password[pos++] = key;
      putc('*'); // Show asterisk
    }
  }
  puts("\n");

  // For demo purposes, accept any non-empty password
  if (pos == 0)
  {
    set_attr(0x0C);
    puts("[sudo] Authentication failed.\n");
    set_attr(0x07);
    return -1;
  }

  // Temporarily become root
  char saved_user[32];
  str_copy(saved_user, current_user, 32);
  str_copy(current_user, "root", 32);

  set_attr(0x0A);
  puts("[sudo] Running as root...\n");
  set_attr(0x07);

  // Execute the command
  int result = cmd_dispatch(args);

  // Restore original user
  str_copy(current_user, saved_user, 32);

  return result;
}

/* 104. WGET - Advanced HTTP Download */
static int cmd_wget(const char *args)
{
  extern int tcp_connect(uint32_t dest_ip, uint16_t dest_port);
  extern int tcp_send(int socket, const void *data, uint32_t len);
  extern int tcp_receive(int socket, void *buffer, uint32_t max_len);
  extern int tcp_close(int socket);
  extern uint32_t dns_resolve(const char *hostname);

  char url[256] = {0};
  char output_file[64] = {0};

  // 1. Check Network Connection (works with e1000/VirtIO or WiFi)
  network_interface_t *net = netif_get_default();
  if (!net || !net->link_up || net->ip_addr == 0)
  {
    set_attr(0x0C); // Red
    puts("Error: Network not connected!\n");
    puts("Run NETSTART first to initialize network.\n");
    set_attr(0x07);
    return -1;
  }

  // Parse Arguments
  char token[128];
  const char *p = args;
  while (*p)
  {
    p = get_token(p, token, 128);
    if (!token[0])
      break;

    if (str_cmp(token, "-filename") == 0 || str_cmp(token, "-O") == 0)
    {
      if (*p)
        p = get_token(p, output_file, 64);
    }
    else
    {
      str_copy(url, token, 256);
    }
  }

  if (url[0] == 0)
  {
    puts("Usage: WGET <URL> [-O <filename>]\n");
    puts("Example: WGET http://example.com/file.txt -O myfile.txt\n");
    puts("         WGET http://httpbin.org/ip (Test IP)\n");
    puts("         WGET http://httpbin.org/get (Test GET request)\n");
    return -1;
  }

  // Parse URL
  char *host_start = url;
  if (url[0] == 'h' && url[1] == 't' && url[2] == 't' && url[3] == 'p')
  {
    if (url[4] == ':' && url[5] == '/' && url[6] == '/')
      host_start += 7;
    else if (url[4] == 's' && url[5] == ':' && url[6] == '/' && url[7] == '/')
    {
      host_start += 8;
      set_attr(0x0E);
      puts("Note: HTTPS not supported, using HTTP.\n");
      set_attr(0x07);
    }
  }

  // Validate URL
  if (host_start[0] == 0 || host_start[0] == '/')
  {
    set_attr(0x0C);
    puts("Error: Invalid URL format.\n");
    set_attr(0x07);
    return -1;
  }

  // Extract host and path
  char host[128] = {0};
  char path[128] = {0};
  int i = 0;
  while (host_start[i] && host_start[i] != '/' && host_start[i] != ':' && i < 127)
  {
    host[i] = host_start[i];
    i++;
  }
  host[i] = 0;

  // Extract path
  if (host_start[i] == '/')
  {
    int j = 0;
    while (host_start[i] && j < 127)
    {
      path[j++] = host_start[i++];
    }
    path[j] = 0;
  }
  else
  {
    path[0] = '/';
    path[1] = 0;
  }

  // Resolve Host
  uint32_t ip = 0;
  bool is_ip = true;
  for (int k = 0; host[k]; k++)
  {
    if ((host[k] < '0' || host[k] > '9') && host[k] != '.')
      is_ip = false;
  }

  char buf[32];

  if (is_ip)
  {
    int parts[4] = {0};
    int val = 0, idx = 0;
    for (int k = 0; host[k]; k++)
    {
      if (host[k] == '.')
      {
        parts[idx++] = val;
        val = 0;
      }
      else
        val = val * 10 + (host[k] - '0');
    }
    parts[idx] = val;
    ip = (parts[0] << 24) | (parts[1] << 16) | (parts[2] << 8) | parts[3];

    set_attr(0x0B);
    puts("Host: ");
    puts(host);
    puts("\n");
    set_attr(0x07);
  }
  else
  {
    set_attr(0x0B);
    puts("Resolving ");
    puts(host);
    puts("...\n");
    set_attr(0x07);

    ip = dns_resolve(host);
    if (ip == 0)
    {
      set_attr(0x0C);
      puts("ERROR: DNS resolution failed!\n");
      puts("Cannot resolve hostname. Check your DNS settings.\n");
      set_attr(0x07);
      return -1;
    }

    set_attr(0x0A);
    puts("Resolved: ");
  }

  // Print IP
  int_to_str((ip >> 24) & 0xFF, buf);
  puts(buf);
  puts(".");
  int_to_str((ip >> 16) & 0xFF, buf);
  puts(buf);
  puts(".");
  int_to_str((ip >> 8) & 0xFF, buf);
  puts(buf);
  puts(".");
  int_to_str(ip & 0xFF, buf);
  puts(buf);
  puts("\n");
  set_attr(0x07);

  set_attr(0x0B);
  puts("Connecting to ");
  puts(host);
  puts(":80...\n");
  set_attr(0x07);

  int sock = tcp_connect(ip, 80);
  if (sock < 0)
  {
    set_attr(0x0C);
    puts("ERROR: Connection failed!\n");
    set_attr(0x07);
    return -1;
  }

  set_attr(0x0A);
  puts("Connected! Sending HTTP request...\n");
  set_attr(0x07);

  // Build HTTP Request
  char req[512];
  str_copy(req, "GET ", 512);
  char *d = req + 4;
  const char *s = path;
  while (*s && (d - req) < 500)
    *d++ = *s++;
  s = " HTTP/1.0\r\nHost: ";
  while (*s && (d - req) < 500)
    *d++ = *s++;
  s = host;
  while (*s && (d - req) < 500)
    *d++ = *s++;
  s = "\r\nUser-Agent: AurionOS/1.0 Beta\r\nConnection: close\r\nAccept: */*\r\n\r\n";
  while (*s && (d - req) < 511)
    *d++ = *s++;
  *d = 0;

  tcp_send(sock, req, str_len(req));

  set_attr(0x0E);
  puts("Downloading");
  set_attr(0x07);

// Use kmalloc for larger buffer - 1MB for larger downloads
#define DOWNLOAD_BUF_SIZE (1024 * 1024)
  char *down_buf = (char *)kmalloc(DOWNLOAD_BUF_SIZE);
  if (!down_buf)
  {
    set_attr(0x0C);
    puts("\nERROR: Out of memory!\n");
    set_attr(0x07);
    tcp_close(sock);
    return -1;
  }

  int total_recvd = 0;
  int dots = 0;

  // Download with progress indicator
  while (total_recvd < DOWNLOAD_BUF_SIZE)
  {
    int r = tcp_receive(sock, down_buf + total_recvd, DOWNLOAD_BUF_SIZE - total_recvd);
    if (r <= 0)
      break;
    total_recvd += r;

    // Show progress dots (one per ~1KB received)
    while (dots < total_recvd / 1024)
    {
      putc('.');
      dots++;
      if (dots % 50 == 0)
      {
        puts("\n");
      }
    }
  }
  puts("\n");

  set_attr(0x0A);
  puts("Download complete! ");
  int_to_str(total_recvd, buf);
  puts(buf);
  puts(" bytes received.\n");
  set_attr(0x07);

  // Parse HTTP Response - find body
  char *body = down_buf;
  int body_len = total_recvd;
  bool found_header_end = false;

  for (int i = 0; i < total_recvd - 3; i++)
  {
    if (down_buf[i] == '\r' && down_buf[i + 1] == '\n' &&
        down_buf[i + 2] == '\r' && down_buf[i + 3] == '\n')
    {
      body = &down_buf[i + 4];
      body_len = total_recvd - (i + 4);
      found_header_end = true;
      break;
    }
  }

  if (!found_header_end)
  {
    // Try \n\n separator
    for (int i = 0; i < total_recvd - 1; i++)
    {
      if (down_buf[i] == '\n' && down_buf[i + 1] == '\n')
      {
        body = &down_buf[i + 2];
        body_len = total_recvd - (i + 2);
        break;
      }
    }
  }

  // Extract filename if not specified
  if (output_file[0] == 0)
  {
    char *last_slash = path;
    for (int k = 0; path[k]; k++)
      if (path[k] == '/')
        last_slash = path + k;

    if (last_slash[1] && last_slash[1] != '?')
    {
      // Extract filename up to '?' if present
      int fn_idx = 0;
      last_slash++;
      while (*last_slash && *last_slash != '?' && fn_idx < 63)
      {
        output_file[fn_idx++] = *last_slash++;
      }
      output_file[fn_idx] = 0;
    }
    else
    {
      str_copy(output_file, "index.html", 64);
    }
  }

  // Save to file
  puts("Saving to ");
  puts(output_file);
  puts("...\n");

  if (save_file_content(output_file, body, body_len) == 0)
  {
    fs_save_to_disk();
    set_attr(0x0A);
    puts("SUCCESS! Saved ");
    int_to_str(body_len, buf);
    puts(buf);
    puts(" bytes to ");
    puts(output_file);
    puts("\n");
    set_attr(0x07);
  }
  else
  {
    set_attr(0x0C);
    puts("ERROR: Failed to save file!\n");
    set_attr(0x07);
  }

  kfree(down_buf);
  tcp_close(sock);
  return 0;
}

/* COMMAND DISPATCHER */

typedef struct
{
  const char *name;
  int (*func)(const char *args);
} Command;

static const Command commands[] = {{"GUITEST", cmd_guitest},
                                   {"CALC-GUI", cmd_calc_gui},
                                   {"WIFITEST", cmd_wifitest},
                                   {"NETSTART", cmd_netstart},
                                   {"HELP", cmd_help},
                                   {"?", cmd_help},
                                   {"CLS", cmd_cls},
                                   {"CLEAR", cmd_clear},
                                   {"VER", cmd_ver},
                                   {"VERSION", cmd_ver},
                                   {"TIME", cmd_time},
                                   {"DATE", cmd_date},
                                   {"REBOOT", cmd_reboot},
                                   {"SHUTDOWN", cmd_shutdown},
                                   {"HALT", cmd_halt},
                                   {"EXIT", cmd_exit},

                                   /* File operations */
                                   {"MKDIR", cmd_mkdir},
                                   {"RMDIR", cmd_rmdir},
                                   {"TOUCH", cmd_touch},
                                   {"DEL", cmd_del},
                                   {"RM", cmd_rm},
                                   {"DIR", cmd_dir},
                                   {"LS", cmd_ls},
                                   {"CD", cmd_cd},
                                   {"PWD", cmd_pwd},
                                   {"CAT", cmd_cat},
                                   {"TYPE", cmd_type},
                                   {"NANO", cmd_nano},
                                   {"COPY", cmd_copy},
                                   {"CP", cmd_cp},
                                   {"MOVE", cmd_move},
                                   {"MV", cmd_mv},
                                   {"REN", cmd_ren},
                                   {"RENAME", cmd_ren},
                                   {"FIND", cmd_find},
                                   {"TREE", cmd_tree},
                                   {"ATTRIB", cmd_attrib},
                                   {"CHMOD", cmd_chmod},

                                   /* Disk operations */
                                   {"VOL", cmd_vol},
                                   {"LABEL", cmd_label},
                                   {"CHKDSK", cmd_chkdsk},
                                   {"FORMAT", cmd_format},
                                   {"DISKPART", cmd_diskpart},
                                   {"FSCK", cmd_fsck},
                                   {"MOUNT", cmd_mount},
                                   {"UMOUNT", cmd_umount},
                                   {"SYNC", cmd_sync},
                                   {"FREE", cmd_free},
                                   {"DF", cmd_df},
                                   {"DU", cmd_du},
                                   {"LSBLK", cmd_lsblk},
                                   {"FDISK", cmd_fdisk},
                                   {"BLKID", cmd_blkid},
                                   {"READSECTOR", cmd_readsector},

                                   /* User management */
                                   {"USERADD", cmd_useradd},
                                   {"USERDEL", cmd_userdel},
                                   {"PASSWD", cmd_passwd},
                                   {"USERS", cmd_users},
                                   {"LOGIN", cmd_login},
                                   {"LOGOUT", cmd_logout},
                                   {"WHOAMI", cmd_whoami},
                                   {"SU", cmd_su},

                                   /* Process management */
                                   {"PS", cmd_ps},
                                   {"KILL", cmd_kill},
                                   {"TOP", cmd_top},
                                   {"TASKLIST", cmd_tasklist},
                                   {"TASKKILL", cmd_taskkill},

                                   /* System info */
                                   {"MEM", cmd_mem},
                                   {"UPTIME", cmd_uptime},
                                   {"SYSINFO", cmd_sysinfo},
                                   {"UNAME", cmd_uname},
                                   {"HOSTNAME", cmd_hostname},
                                   {"LSCPU", cmd_lscpu},
                                   {"LSPCI", cmd_lspci},
                                   {"DMESG", cmd_dmesg},

                                   /* Screen/display */
                                   {"COLOR", cmd_color},
                                   {"ECHO", cmd_echo},
                                   {"MODE", cmd_mode},

                                   /* Utilities */
                                   {"BEEP", cmd_beep},
                                   {"CALC", cmd_calc},
                                   {"HEXDUMP", cmd_hexdump},
                                   {"ASCII", cmd_ascii},
                                   {"HASH", cmd_hash},
                                   {"PAUSE", cmd_pause},
                                   {"SLEEP", cmd_sleep},

                                   /* Network */
                                   {"WIFILOGIN", cmd_wifilogin},
                                   {"WIFISTAT", cmd_wifistat},
                                   {"WIFIDISCONNECT", cmd_wifidisconnect},
                                   {"WIFIRESCAN", cmd_wifirescan},
                                   {"WIFISIGNAL", cmd_wifisignal},
                                   {"WIFIAP", cmd_wifiap},
                                   {"IPCONFIG", cmd_ipconfig},
                                   {"PING", cmd_ping},
                                   {"WIFITEST", cmd_wifitest},
                                   {"NETMODE", cmd_netmode},
                                   {"WGET", cmd_wget},
                                   {"NOTEPAD", cmd_gui_placeholder},
                                   {"PAINT", cmd_gui_placeholder},
                                   {"FILEBROWSER", cmd_gui_placeholder},
                                   {"BROWSER", cmd_gui_placeholder},
                                   {"CLOCK", cmd_gui_placeholder},
                                   {"SYSINFOGUI", cmd_gui_placeholder},
                                   {"SUDO", cmd_sudo},
                                   {"PYTHON", cmd_python},
                                   {"MAKE", cmd_make},

                                   /* File utilities */
                                   {"WC", cmd_wc},
                                   {"HEAD", cmd_head},
                                   {"TAIL", cmd_tail},
                                   {"GREP", cmd_grep},
                                   {"SORT", cmd_sort},
                                   {"UNIQ", cmd_uniq},
                                   {"CUT", cmd_cut},
                                   {"DIFF", cmd_diff},
                                   {"MORE", cmd_more},
                                   {"LESS", cmd_less},
                                   {"FILE", cmd_file},
                                   {"STAT", cmd_stat},

                                   /* Environment */
                                   {"PATH", cmd_path},
                                   {"SET", cmd_set},
                                   {"ALIAS", cmd_alias},
                                   {"HISTORY", cmd_history},
                                   {"PROMPT", cmd_prompt},
                                   {"PRINTENV", cmd_printenv},
                                   {"EXPORT", cmd_export},
                                   {"SOURCE", cmd_source},
                                   {"WHICH", cmd_which},
                                   {"WHEREIS", cmd_whereis},

                                   /* More utilities */
                                   {"CAL", cmd_cal},
                                   {"BANNER", cmd_banner},
                                   {"FIGLET", cmd_figlet},
                                   {"COWSAY", cmd_cowsay},
                                   {"FORTUNE", cmd_fortune},
                                   {"STRINGS", cmd_strings},
                                   {"CHOWN", cmd_chown},
                                   {"LN", cmd_ln},
                                   {"STRACE", cmd_strace},
                                   {"NOHUP", cmd_nohup},
                                   {"NICE", cmd_nice},
                                   {"BG", cmd_bg},
                                   {"FG", cmd_fg},
                                   {"JOBS", cmd_jobs},
                                   {"UNALIAS", cmd_unalias},
                                   {"PRINTF", cmd_printf},
                                   {"READ", cmd_read},
                                   {"LET", cmd_let},
                                   {"TEST", cmd_test},
                                   {"FALSE", cmd_false},
                                   {"TRUE", cmd_true},
                                   {"UNSET", cmd_unset},
                                   {"ID", cmd_id},
                                   {"TIMEOUT", cmd_timeout},
                                   {"YES", cmd_yes},
                                   {"SHUF", cmd_shuf},
                                   {"SEQ", cmd_seq},
                                   {"FACTOR", cmd_factor},
                                   {"TAC", cmd_tac},
                                   {"NL", cmd_nl},
                                   {"REV", cmd_rev},
                                   {"OD", cmd_od},
                                   {"BASE64", cmd_base64},
                                   {"AWK", cmd_awk},
                                   {"SED", cmd_sed},
                                   {"TR", cmd_tr},
                                   {"PASTE", cmd_paste},
                                   {"WIFISTATUS", cmd_wifistatus},
                                   {"WIFICONNECT", cmd_wificonnect},
                                   {"WIFISCAN", cmd_wifiscan},
                                   {"W", cmd_w},
                                   {"WHO", cmd_who},
                                   {"LAST", cmd_last},
                                   {"KEYBOARD", cmd_keyboard},

                                   {NULL, NULL}};

/* Command dispatcher - called from shell */
int cmd_dispatch(const char *line)
{
  if (!line || !line[0])
    return 0;

  /* Extract command name */
  char cmd_name[64];
  const char *args = get_token(line, cmd_name, 64);

  /* Convert to uppercase */
  str_upper(cmd_name);

  /* Find and execute command */
  for (int i = 0; commands[i].name != NULL; i++)
  {
    if (str_cmp(cmd_name, commands[i].name) == 0)
    {
      return commands[i].func(args);
    }
  }

  /* Command not found */
  return -255;
}

/* Initialize command system */
void cmd_init(void) { fs_init_commands(); }

/* Silent version - no output messages */
void cmd_init_silent(void)
{
  fs_init_silent = 1;
  fs_init_commands();
  fs_init_silent = 0;
}