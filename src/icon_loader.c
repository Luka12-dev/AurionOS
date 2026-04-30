/*
 * icon_loader.c - Load 64x64 BMP icons from filesystem
 *
 * Icons are stored as high-res BMP files in /icons/ directory.
 * Loaded once at boot and cached in memory.
 */

#include "icon_loader.h"
#include <stdint.h>
#include <stdbool.h>

/* GPU pixel draw function provided by vbe_graphics.c */
extern void gpu_draw_pixel(int x, int y, uint32_t color);
extern int load_file_content(const char *filename, char *buffer, int max_len);
extern int iso9660_read_file(const char *filename, void *buffer, uint32_t max_size);

#define ICON_SIZE 64
#define ICON_PIXELS (ICON_SIZE * ICON_SIZE)

/* Static pixel buffers - one per icon, 64x64 ARGB */
static unsigned int buf_terminal[ICON_PIXELS];
static unsigned int buf_browser[ICON_PIXELS];
static unsigned int buf_notepad[ICON_PIXELS];
static unsigned int buf_calculator[ICON_PIXELS];
static unsigned int buf_files[ICON_PIXELS];
static unsigned int buf_clock[ICON_PIXELS];
static unsigned int buf_paint[ICON_PIXELS];
static unsigned int buf_sysinfo[ICON_PIXELS];
static unsigned int buf_settings[ICON_PIXELS];
static unsigned int buf_folder[ICON_PIXELS];
static unsigned int buf_file[ICON_PIXELS];
static unsigned int buf_snake[ICON_PIXELS];
static unsigned int buf_3d_demo[ICON_PIXELS];
static unsigned int buf_taskmgr[ICON_PIXELS];
static unsigned int buf_audiomgr[ICON_PIXELS];

/* Buffer for largest possible icon BMP (512x512 32bpp is ~1MB) */
static unsigned char bmp_loader_data[1200000];

/* Load a BMP file and decode it into a 64x64 ARGB buffer */
static int load_bmp_icon(const char *path, unsigned int *dst, int index)
{
    /* Try to load from RAM FS first (at boot, this usually fails/truncates) */
    int bytes = load_file_content(path, (char *)bmp_loader_data, sizeof(bmp_loader_data));

    /* If RAM FS failed, try ISO 9660 filesystem */
    if (bytes <= 0)
    {
        bytes = iso9660_read_file(path, bmp_loader_data, sizeof(bmp_loader_data));

        /* Fallback: try root folder on CD if /icons/ failed */
        if (bytes <= 0)
        {
            const char *filename = path;
            for (const char *p = path; *p; p++)
                if (*p == '/' || *p == '\\')
                    filename = p + 1;
            bytes = iso9660_read_file(filename, bmp_loader_data, sizeof(bmp_loader_data));
        }
    }

    /* CRITICAL FALLBACK: Try raw disk sectors (starts at LBA 60000, 1MB spacing) */
    if (bytes <= 0 && index >= 0)
    {
        uint32_t raw_lba = 60000 + (index * 2000);
        extern int disk_read_lba_hdd(uint32_t lba, uint32_t count, void *buffer);
        extern int disk_read_lba_cdrom(uint32_t lba, uint32_t count, void *buffer);

        /* Try both HDD and CDROM raw addresses */
        if (disk_read_lba_hdd(raw_lba, 1, bmp_loader_data) == 0 && bmp_loader_data[0] == 'B' && bmp_loader_data[1] == 'M')
        {
            uint32_t s = *(uint32_t *)(bmp_loader_data + 2);
            if (s > 0 && s < sizeof(bmp_loader_data))
            {
                disk_read_lba_hdd(raw_lba, (s + 511) / 512, bmp_loader_data);
                bytes = s;
            }
        }
        else if (disk_read_lba_cdrom(raw_lba, 1, bmp_loader_data) == 0 && bmp_loader_data[0] == 'B' && bmp_loader_data[1] == 'M')
        {
            uint32_t s = *(uint32_t *)(bmp_loader_data + 2);
            if (s > 0 && s < sizeof(bmp_loader_data))
            {
                disk_read_lba_cdrom(raw_lba, (s + 511) / 512, bmp_loader_data);
                bytes = s;
            }
        }
    }

    if (bytes <= 0)
    {
        extern bool ata_cdrom_available(void);
        if (!ata_cdrom_available())
            return -5; /* CD drive not even found */
        return -1;     /* File not found */
    }
    if (bytes < 54)
        return -2; /* Too small */

    /* Verify BMP signature */
    if (bmp_loader_data[0] != 'B' || bmp_loader_data[1] != 'M')
        return -3; /* Bad signature */

    /* Parse BMP header */
    int32_t width = *(int32_t *)(bmp_loader_data + 18);
    int32_t height = *(int32_t *)(bmp_loader_data + 22);
    uint16_t bpp = *(uint16_t *)(bmp_loader_data + 28);
    uint32_t offset = *(uint32_t *)(bmp_loader_data + 10);

    /* Only support 24-bit or 32-bit BMPs */
    if ((bpp != 24 && bpp != 32) || width <= 0 || height <= 0)
        return -4; /* Bad format */

    /* Calculate row stride (BMP rows are padded to 4-byte boundary) */
    int row_stride = ((width * (bpp / 8)) + 3) & ~3;
    uint8_t *pixel_data = bmp_loader_data + offset;

    /* Decode and scale to 64x64 with high-quality averaging */
    for (int dst_row = 0; dst_row < ICON_SIZE; dst_row++)
    {
        for (int dst_col = 0; dst_col < ICON_SIZE; dst_col++)
        {
            /* Area to sample in source (fixed-point math 16.16) */
            uint32_t src_y_start = (dst_row * height << 16) / ICON_SIZE;
            uint32_t src_y_end = ((dst_row + 1) * height << 16) / ICON_SIZE;
            uint32_t src_x_start = (dst_col * width << 16) / ICON_SIZE;
            uint32_t src_x_end = ((dst_col + 1) * width << 16) / ICON_SIZE;

            uint32_t r_sum = 0, g_sum = 0, b_sum = 0, a_sum = 0;
            int count = 0;

            /* Sample 4x4 grid within the source area for best 64px quality */
            for (int sy = 0; sy < 4; sy++)
            {
                uint32_t sy_off = src_y_start + (src_y_end - src_y_start) * sy / 4;
                int bmp_row = height - 1 - (int)(sy_off >> 16);
                uint8_t *row_ptr = pixel_data + bmp_row * row_stride;
                for (int sx = 0; sx < 4; sx++)
                {
                    uint32_t sx_off = src_x_start + (src_x_end - src_x_start) * sx / 4;
                    int px = (int)(sx_off >> 16);
                    b_sum += row_ptr[px * (bpp / 8) + 0];
                    g_sum += row_ptr[px * (bpp / 8) + 1];
                    r_sum += row_ptr[px * (bpp / 8) + 2];
                    a_sum += (bpp == 32) ? row_ptr[px * (bpp / 8) + 3] : 0xFF;
                    count++;
                }
            }

            dst[dst_row * ICON_SIZE + dst_col] =
                ((uint32_t)(a_sum / count) << 24) |
                ((uint32_t)(r_sum / count) << 16) |
                ((uint32_t)(g_sum / count) << 8) |
                (uint32_t)(b_sum / count);
        }
    }

    return 1;
}

/* Fallback: create a simple colored square icon */
static void create_fallback_icon(unsigned int *dst, uint32_t color)
{
    for (int i = 0; i < ICON_PIXELS; i++)
    {
        dst[i] = color;
    }
}

static void try_load_icon(const char *path, unsigned int *dst, uint32_t def_color, int index)
{
    int res = load_bmp_icon(path, dst, index);
    if (res == 1)
        return;

    /* Diagnostic colors if failed */
    if (res == -5)
        create_fallback_icon(dst, 0xFFAA00AA); /* Light Purple: Driver/ATA issue */
    else if (res == -1)
        create_fallback_icon(dst, 0xFFFF00FF); /* Magenta: Path resolution issue */
    else if (res == -2)
        create_fallback_icon(dst, 0xFFFFFF00); /* Yellow: Too small */
    else if (res == -3)
        create_fallback_icon(dst, 0xFF00FFFF); /* Cyan: Bad signature */
    else if (res == -4)
        create_fallback_icon(dst, 0xFFFF8800); /* Orange: Format issue */
    else
        create_fallback_icon(dst, def_color); /* Default color */
}

/* Call once at boot - load all BMP icons from filesystem/ISO or raw sectors */
void icons_load_all(void)
{
    try_load_icon("/icons/terminal.bmp", buf_terminal, 0xFF333333, 0);
    try_load_icon("/icons/Browser.bmp", buf_browser, 0xFF4488FF, 1);
    try_load_icon("/icons/notepad.bmp", buf_notepad, 0xFFFFFFFF, 2);
    try_load_icon("/icons/calculator.bmp", buf_calculator, 0xFF444444, 3);
    try_load_icon("/icons/files.bmp", buf_files, 0xFF888888, 4);
    try_load_icon("/icons/clock.bmp", buf_clock, 0xFFFFFFFF, 5);
    try_load_icon("/icons/paint.bmp", buf_paint, 0xFFFF8844, 6);
    try_load_icon("/icons/sys-info.bmp", buf_sysinfo, 0xFF666666, 7);
    try_load_icon("/icons/settings.bmp", buf_settings, 0xFF888888, 8);
    try_load_icon("/icons/folder.bmp", buf_folder, 0xFFFFCC66, 9);
    try_load_icon("/icons/snake.bmp", buf_snake, 0xFF44DD44, 10);
    try_load_icon("/icons/3d_demo.bmp", buf_3d_demo, 0xFF8888FF, 11);
    try_load_icon("/icons/task_manager.bmp", buf_taskmgr, 0xFF22CC88, 12);
    try_load_icon("/icons/audio_manager.bmp", buf_audiomgr, 0xFFFF6B35, 13);
}

/* Draw icon buffer at (x, y) - respects alpha, skips fully transparent pixels */
static void draw_buf(const unsigned int *buf, int x, int y)
{
    for (int i = 0; i < ICON_SIZE; i++)
    {
        for (int j = 0; j < ICON_SIZE; j++)
        {
            uint32_t color = buf[i * ICON_SIZE + j];
            if ((color >> 24) == 0)
                continue;
            gpu_draw_pixel(x + j, y + i, color);
        }
    }
}

void icon_draw_png_terminal(int x, int y) { draw_buf(buf_terminal, x, y); }
void icon_draw_png_browser(int x, int y) { draw_buf(buf_browser, x, y); }
void icon_draw_png_notepad(int x, int y) { draw_buf(buf_notepad, x, y); }
void icon_draw_png_calculator(int x, int y) { draw_buf(buf_calculator, x, y); }
void icon_draw_png_files(int x, int y) { draw_buf(buf_files, x, y); }
void icon_draw_png_clock(int x, int y) { draw_buf(buf_clock, x, y); }
void icon_draw_png_paint(int x, int y) { draw_buf(buf_paint, x, y); }
void icon_draw_png_sysinfo(int x, int y) { draw_buf(buf_sysinfo, x, y); }
void icon_draw_png_settings(int x, int y) { draw_buf(buf_settings, x, y); }
void icon_draw_png_folder(int x, int y) { draw_buf(buf_folder, x, y); }
void icon_draw_png_file(int x, int y) { draw_buf(buf_file, x, y); }
void icon_draw_png_snake(int x, int y) { draw_buf(buf_snake, x, y); }
void icon_draw_png_3d_demo(int x, int y) { draw_buf(buf_3d_demo, x, y); }

void icon_draw_png_cube(int x, int y)
{
    icon_draw_png_3d_demo(x, y);
}
void icon_draw_png_taskmgr(int x, int y) { draw_buf(buf_taskmgr, x, y); }
void icon_draw_png_audiomgr(int x, int y) { draw_buf(buf_audiomgr, x, y); }
