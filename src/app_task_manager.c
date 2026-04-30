/*
 * app_task_manager.c - Task Manager and Audio Manager Applications
 */

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include "window_manager.h"
#include "../include/fs.h"

extern uint32_t get_ticks(void);
extern int sys_get_time(uint8_t *h, uint8_t *m, uint8_t *s);
extern void mem_get_stats(uint32_t *stats);

#define MAX_PROCESSES 64

typedef struct
{
    int pid;
    char name[48];
    float cpu_percent;
    uint32_t memory_kb;
    bool active;
    uint32_t last_cpu_ticks;
    uint32_t last_idle_ticks;
} ProcessInfo;

typedef struct
{
    Window *win;
    ProcessInfo processes[MAX_PROCESSES];
    int process_count;
    int selected_idx;
    int scroll_offset;
    uint32_t last_update;
    bool show_cpu_graph;
    float cpu_history[60];
    int cpu_history_idx;
} TaskManagerState;

typedef struct
{
    Window *win;
    bool is_playing;
    float volume;
    float position;
    float duration;
    char current_track[128];
    char playlist[32][128];
    int playlist_count;
    int current_track_idx;
    bool playlist_mode;
    bool shuffle_mode;
    bool repeat_mode;
    int selected_idx;
    uint32_t last_update;
} AudioManagerState;

extern Window *wm_create_window(const char *title, int x, int y, int w, int h);
extern void wm_fill_rect(Window *w, int x, int y, int rw, int rh, uint32_t c);
extern void wm_draw_string(Window *w, int x, int y, const char *s, uint32_t fg, uint32_t bg);
extern void wm_draw_char(Window *w, int x, int y, uint8_t ch, uint32_t fg, uint32_t bg);
extern int wm_get_screen_w(void);
extern int wm_get_screen_h(void);
extern void wm_destroy_window(Window *w);

static int str_len(const char *s)
{
    int i = 0;
    while (s && s[i])
        i++;
    return i;
}

static void int_to_str(int val, char *buf, int bufsize)
{
    if (bufsize <= 0)
        return;
    if (val < 0)
    {
        buf[0] = '-';
        val = -val;
        int_to_str(val, buf + 1, bufsize - 1);
        return;
    }
    char tmp[32];
    int i = 0;
    while (val > 0 && i < 31)
    {
        tmp[i++] = '0' + (val % 10);
        val /= 10;
    }
    if (i == 0)
        tmp[i++] = '0';
    tmp[i] = '\0';
    for (int j = 0; j < i; j++)
        buf[j] = tmp[i - 1 - j];
    buf[i] = '\0';
}

static void float_to_str(float val, char *buf, int bufsize, int decimals)
{
    if (bufsize <= 0)
        return;
    if (val < 0)
    {
        buf[0] = '-';
        float_to_str(-val, buf + 1, bufsize - 1, decimals);
        return;
    }
    int int_part = (int)val;
    int_to_str(int_part, buf, bufsize);
    int len = str_len(buf);
    if (decimals > 0 && len < bufsize - 1)
    {
        buf[len++] = '.';
        float frac = val - int_part;
        for (int i = 0; i < decimals && len < bufsize - 1; i++)
        {
            frac *= 10;
            int d = (int)frac;
            buf[len++] = '0' + (d % 10);
            frac -= d;
        }
    }
    buf[len] = '\0';
}

static void draw_cpu_graph(TaskManagerState *state, int x, int y, int w, int h)
{
    wm_fill_rect(state->win, x, y, w, h, 0xFF1A1A2E);
    wm_fill_rect(state->win, x, y, w, 1, 0xFF3A3A5E);
    wm_fill_rect(state->win, x, y + h - 1, w, 1, 0xFF3A3A5E);
    wm_fill_rect(state->win, x, y, 1, h, 0xFF3A3A5E);
    wm_fill_rect(state->win, x + w - 1, y, 1, h, 0xFF3A3A5E);

    int bar_w = w / 60;
    if (bar_w < 1)
        bar_w = 1;
    int gap = (w - bar_w * 60) / 2;

    for (int i = 0; i < 60; i++)
    {
        int idx = (state->cpu_history_idx + i) % 60;
        float cpu = state->cpu_history[idx];
        if (cpu < 0)
            cpu = 0;
        if (cpu > 100)
            cpu = 100;
        int bar_h = (int)((cpu / 100.0f) * (h - 4));
        if (bar_h < 1)
            bar_h = 1;

        uint32_t color;
        if (cpu < 30)
            color = 0xFF4ADE80;
        else if (cpu < 70)
            color = 0xFFFBBF24;
        else
            color = 0xFFEF4444;

        int bx = x + gap + i * bar_w;
        int by = y + h - 2 - bar_h;
        wm_fill_rect(state->win, bx, by, bar_w - 1, bar_h, color);
    }
}

static void draw_memory_bar(TaskManagerState *state, int x, int y, int w, int h)
{
    wm_fill_rect(state->win, x, y, w, h, 0xFF1A1A2E);
    wm_fill_rect(state->win, x, y, w, 1, 0xFF3A3A5E);
    wm_fill_rect(state->win, x, y + h - 1, w, 1, 0xFF3A3A5E);
    wm_fill_rect(state->win, x, y, 1, h, 0xFF3A3A5E);
    wm_fill_rect(state->win, x + w - 1, y, 1, h, 0xFF3A3A5E);

    uint32_t mem_stats[4];
    mem_get_stats(mem_stats);
    uint32_t total_kb = mem_stats[0];
    uint32_t used_kb = mem_stats[1];

    if (total_kb == 0)
        total_kb = 1;
    float used_pct = (float)used_kb / (float)total_kb;
    if (used_pct > 1.0f)
        used_pct = 1.0f;

    int bar_w = (int)((float)(w - 4) * used_pct);
    if (bar_w < 1)
        bar_w = 1;

    uint32_t bar_color;
    if (used_pct < 0.5f)
        bar_color = 0xFF4ADE80;
    else if (used_pct < 0.8f)
        bar_color = 0xFFFBBF24;
    else
        bar_color = 0xFFEF4444;

    wm_fill_rect(state->win, x + 2, y + 2, bar_w, h - 4, bar_color);
}

static void task_manager_on_draw(Window *win)
{
    TaskManagerState *state = (TaskManagerState *)win->app_data;
    if (!state)
        return;

    int cw = win->w;
    int ch = win->h;

    wm_fill_rect(win, 0, 0, cw, ch, 0xFF0F0F1A);

    uint32_t mem_stats[4];
    mem_get_stats(mem_stats);
    uint32_t total_kb = mem_stats[0];
    uint32_t used_kb = mem_stats[1];

    wm_draw_string(win, 10, 10, "Task Manager", 0xFFFFFFFF, 0x00000000);

    char buf[64];
    int_to_str((int)(used_kb / 1024), buf, 64);
    wm_draw_string(win, 10, 28, "Memory: ", 0xFF9CA3AF, 0x00000000);
    wm_draw_string(win, 80, 28, buf, 0xFFFFFFFF, 0x00000000);
    wm_draw_string(win, 80 + str_len(buf) * 8, 28, " MB / ", 0xFF9CA3AF, 0x00000000);
    int_to_str((int)(total_kb / 1024), buf, 64);
    wm_draw_string(win, 80 + str_len(buf) * 8 + 40, 28, buf, 0xFFFFFFFF, 0x00000000);
    wm_draw_string(win, 80 + str_len(buf) * 8 * 2 + 40, 28, " MB", 0xFF9CA3AF, 0x00000000);

    int bar_x = 10;
    int bar_y = 45;
    int bar_w = cw - 20;
    int bar_h = 20;
    draw_memory_bar(state, bar_x, bar_y, bar_w, bar_h);

    wm_draw_string(win, 10, 75, "CPU Usage:", 0xFF9CA3AF, 0x00000000);
    int cpu_graph_y = 95;
    draw_cpu_graph(state, 10, cpu_graph_y, cw - 20, 60);

    float avg_cpu = 0;
    for (int i = 0; i < 60; i++)
    {
        if (state->cpu_history[i] >= 0)
            avg_cpu += state->cpu_history[i];
    }
    int count = 0;
    for (int i = 0; i < 60; i++)
        if (state->cpu_history[i] >= 0)
            count++;
    if (count > 0)
        avg_cpu /= count;
    float_to_str(avg_cpu, buf, 64, 1);
    wm_draw_string(win, cw - 70, 75, buf, 0xFFFFFFFF, 0x00000000);
    wm_draw_string(win, cw - 30, 75, "%", 0xFF9CA3AF, 0x00000000);

    int list_y = 165;
    wm_draw_string(win, 10, list_y, "PID", 0xFF9CA3AF, 0x00000000);
    wm_draw_string(win, 70, list_y, "Name", 0xFF9CA3AF, 0x00000000);
    wm_draw_string(win, 220, list_y, "CPU", 0xFF9CA3AF, 0x00000000);
    wm_draw_string(win, 280, list_y, "Memory", 0xFF9CA3AF, 0x00000000);
    wm_fill_rect(win, 10, list_y + 18, cw - 20, 1, 0xFF3A3A5E);

    int row_y = list_y + 25;
    int visible_rows = (ch - row_y - 10) / 22;
    if (visible_rows < 1)
        visible_rows = 1;

    int start_idx = state->scroll_offset;
    int end_idx = start_idx + visible_rows;
    if (end_idx > state->process_count)
        end_idx = state->process_count;
    if (start_idx >= end_idx)
        start_idx = 0;

    for (int i = start_idx; i < end_idx; i++)
    {
        ProcessInfo *p = &state->processes[i];
        int ry = row_y + (i - start_idx) * 22;

        if (i == state->selected_idx)
        {
            wm_fill_rect(win, 8, ry - 2, cw - 16, 20, 0xFF2A2A4E);
        }

        int_to_str(p->pid, buf, 64);
        uint32_t pid_color = p->active ? 0xFFFFFFFF : 0xFF6B7280;
        wm_draw_string(win, 12, ry, buf, pid_color, 0x00000000);

        uint32_t name_color = p->active ? 0xFFFFFFFF : 0xFF9CA3AF;
        int name_len = str_len(p->name);
        if (name_len > 18)
            name_len = 18;
        char name_buf[20];
        for (int j = 0; j < name_len; j++)
            name_buf[j] = p->name[j];
        name_buf[name_len] = '\0';
        wm_draw_string(win, 72, ry, name_buf, name_color, 0x00000000);

        float_to_str(p->cpu_percent, buf, 64, 1);
        uint32_t cpu_color;
        if (p->cpu_percent < 30)
            cpu_color = 0xFF4ADE80;
        else if (p->cpu_percent < 70)
            cpu_color = 0xFFFBBF24;
        else
            cpu_color = 0xFFEF4444;
        wm_draw_string(win, 222, ry, buf, cpu_color, 0x00000000);
        wm_draw_string(win, 222 + 5 * 8, ry, "%", 0xFF9CA3AF, 0x00000000);

        int mem_mb = p->memory_kb / 1024;
        int_to_str(mem_mb, buf, 64);
        uint32_t mem_color = p->active ? 0xFFFFFFFF : 0xFF9CA3AF;
        wm_draw_string(win, 282, ry, buf, mem_color, 0x00000000);
        wm_draw_string(win, 282 + str_len(buf) * 8, ry, " MB", 0xFF6B7280, 0x00000000);

        ry += 22;
    }

    wm_draw_string(win, 10, ch - 20, "Up/Down: Select | End: Kill Process | F5: Refresh",
                   0xFF6B7280, 0x00000000);
}

static void task_manager_on_key(Window *win, char key, int key_code, bool ctrl, bool shift)
{
    TaskManagerState *state = (TaskManagerState *)win->app_data;
    if (!state)
        return;

    if (key == 0 && key_code == 0x48)
    {
        if (state->selected_idx > 0)
        {
            state->selected_idx--;
            if (state->selected_idx < state->scroll_offset)
                state->scroll_offset = state->selected_idx;
        }
    }
    else if (key == 0 && key_code == 0x50)
    {
        if (state->selected_idx < state->process_count - 1)
        {
            state->selected_idx++;
            int visible_rows = (win->h - 190) / 22;
            if (state->selected_idx >= state->scroll_offset + visible_rows)
            {
                state->scroll_offset = state->selected_idx - visible_rows + 1;
            }
        }
    }
    else if (key == 0 && key_code == 0x4F)
    {
        state->scroll_offset = 0;
        state->selected_idx = 0;
    }
    else if (key == 0 && key_code == 0x4D)
    {
        int visible_rows = (win->h - 190) / 22;
        state->scroll_offset = state->process_count - visible_rows;
        if (state->scroll_offset < 0)
            state->scroll_offset = 0;
        state->selected_idx = state->process_count - 1;
    }
    else if (key == 0 && key_code == 0x3E)
    {
        state->selected_idx = state->scroll_offset;
    }
    else if (key == 0 && key_code == 0x3C)
    {
        state->selected_idx = state->scroll_offset;
        int visible_rows = (win->h - 190) / 22;
        if (state->selected_idx + visible_rows - 1 < state->process_count)
            state->selected_idx = state->scroll_offset + visible_rows - 1;
    }
    else if (key == 0 && key_code == 0x3F)
    {
        state->show_cpu_graph = !state->show_cpu_graph;
    }
    else if (key == 0 && key_code == 0x3E || (ctrl && (key == 'r' || key == 'R')))
    {
        state->selected_idx = 0;
        state->scroll_offset = 0;
    }
}

void app_task_manager_create(void)
{
    int sw = wm_get_screen_w();
    int sh = wm_get_screen_h();
    int w = 420;
    int h = 520;
    int x = (sw - w) / 2;
    int y = (sh - WM_TASKBAR_HEIGHT - h) / 2;

    Window *win = wm_create_window("Task Manager", x, y, w, h);
    if (!win)
        return;

    TaskManagerState *state = (TaskManagerState *)win->app_data;
    win->user_data = state;

    state->win = win;
    state->process_count = 0;
    state->selected_idx = 0;
    state->scroll_offset = 0;
    state->last_update = 0;
    state->show_cpu_graph = true;

    for (int i = 0; i < 60; i++)
        state->cpu_history[i] = -1;
    state->cpu_history_idx = 0;

    for (int i = 0; i < MAX_PROCESSES; i++)
    {
        state->processes[i].pid = i + 1;
        state->processes[i].cpu_percent = 0;
        state->processes[i].memory_kb = 0;
        state->processes[i].active = (i < 5);
        state->processes[i].last_cpu_ticks = get_ticks();
        state->processes[i].last_idle_ticks = get_ticks();
    }

    strcpy(state->processes[0].name, "System Idle");
    state->processes[0].memory_kb = 4096;
    state->processes[0].cpu_percent = 15.0f;
    state->processes[0].active = true;

    strcpy(state->processes[1].name, "AurionOS Kernel");
    state->processes[1].memory_kb = 8192;
    state->processes[1].cpu_percent = 5.0f;
    state->processes[1].active = true;

    strcpy(state->processes[2].name, "Window Manager");
    state->processes[2].memory_kb = 12288;
    state->processes[2].cpu_percent = 8.5f;
    state->processes[2].active = true;

    strcpy(state->processes[3].name, "Desktop");
    state->processes[3].memory_kb = 16384;
    state->processes[3].cpu_percent = 3.2f;
    state->processes[3].active = true;

    strcpy(state->processes[4].name, "Audio Service");
    state->processes[4].memory_kb = 6144;
    state->processes[4].cpu_percent = 1.0f;
    state->processes[4].active = true;

    strcpy(state->processes[5].name, "Network Stack");
    state->processes[5].memory_kb = 10240;
    state->processes[5].cpu_percent = 2.1f;
    state->processes[5].active = false;

    strcpy(state->processes[6].name, "Blaze Browser");
    state->processes[6].memory_kb = 32768;
    state->processes[6].cpu_percent = 12.5f;
    state->processes[6].active = false;

    strcpy(state->processes[7].name, "Terminal");
    state->processes[7].memory_kb = 8192;
    state->processes[7].cpu_percent = 0.5f;
    state->processes[7].active = false;

    state->process_count = 8;

    win->on_draw = task_manager_on_draw;
    win->on_key = task_manager_on_key;
}

static void audio_manager_on_draw(Window *win)
{
    AudioManagerState *state = (AudioManagerState *)win->app_data;
    if (!state)
        return;

    int cw = win->w;
    int ch = win->h;

    wm_fill_rect(win, 0, 0, cw, ch, 0xFF0F0F1A);

    wm_draw_string(win, 10, 10, "Audio Manager", 0xFFFFFFFF, 0x00000000);

    wm_fill_rect(win, 10, 38, cw - 20, 1, 0xFF3A3A5E);

    if (state->current_track_idx >= 0 && state->current_track_idx < state->playlist_count)
    {
        wm_draw_string(win, 10, 48, "Now Playing:", 0xFF9CA3AF, 0x00000000);
        const char *track = state->playlist[state->current_track_idx];
        int track_len = str_len(track);
        if (track_len > 30)
            track_len = 30;
        char buf[32];
        for (int i = 0; i < track_len; i++)
            buf[i] = track[i];
        buf[track_len] = '\0';
        wm_draw_string(win, 10, 66, buf, 0xFFFFFFFF, 0x00000000);

        float_to_str(state->position / 60.0f, buf, 64, 1);
        wm_draw_string(win, 10, 84, buf, 0xFF9CA3AF, 0x00000000);
        wm_draw_string(win, 50, 84, ":", 0xFF6B7280, 0x00000000);
        float_to_str(state->duration / 60.0f, buf, 64, 1);
        wm_draw_string(win, 58, 84, buf, 0xFF9CA3AF, 0x00000000);

        int bar_x = 10;
        int bar_y = 102;
        int bar_w = cw - 20;
        int bar_h = 16;
        wm_fill_rect(win, bar_x, bar_y, bar_w, bar_h, 0xFF1A1A2E);
        wm_fill_rect(win, bar_x, bar_y, bar_w, 1, 0xFF3A3A5E);
        wm_fill_rect(win, bar_x, bar_y + bar_h - 1, bar_w, 1, 0xFF3A3A5E);
        wm_fill_rect(win, bar_x, bar_y, 1, bar_h, 0xFF3A3A5E);
        wm_fill_rect(win, bar_x + bar_w - 1, bar_y, 1, bar_h, 0xFF3A3A5E);

        if (state->duration > 0)
        {
            int play_pos = (int)((float)(bar_w - 4) * (state->position / state->duration));
            wm_fill_rect(win, bar_x + 2, bar_y + 2, play_pos, bar_h - 4, 0xFF0A84FF);
        }
    }
    else
    {
        wm_draw_string(win, 10, 48, "No track loaded", 0xFF6B7280, 0x00000000);
    }

    int vol_x = 10;
    int vol_y = 130;
    wm_draw_string(win, vol_x, vol_y, "Volume:", 0xFF9CA3AF, 0x00000000);
    int vol_bar_x = 80;
    int vol_bar_y = vol_y;
    int vol_bar_w = 120;
    int vol_bar_h = 16;
    wm_fill_rect(win, vol_bar_x, vol_bar_y, vol_bar_w, vol_bar_h, 0xFF1A1A2E);
    wm_fill_rect(win, vol_bar_x, vol_bar_y, vol_bar_w, 1, 0xFF3A3A5E);
    wm_fill_rect(win, vol_bar_x, vol_bar_y + vol_bar_h - 1, vol_bar_w, 1, 0xFF3A3A5E);
    wm_fill_rect(win, vol_bar_x, vol_bar_y, 1, vol_bar_h, 0xFF3A3A5E);
    wm_fill_rect(win, vol_bar_x + vol_bar_w - 1, vol_bar_y, 1, vol_bar_h, 0xFF3A3A5E);
    int vol_pos = (int)((float)(vol_bar_w - 4) * state->volume);
    wm_fill_rect(win, vol_bar_x + 2, vol_bar_y + 2, vol_pos, vol_bar_h - 4, 0xFF4ADE80);
    char vol_buf[8];
    int_to_str((int)(state->volume * 100), vol_buf, 8);
    wm_draw_string(win, vol_bar_x + vol_bar_w + 8, vol_y, vol_buf, 0xFFFFFFFF, 0x00000000);
    wm_draw_string(win, vol_bar_x + vol_bar_w + 30, vol_y, "%", 0xFF9CA3AF, 0x00000000);

    int btn_y = 160;
    int btn_spacing = 85;
    uint32_t btn_bg = 0xFF2A2A4E;
    uint32_t btn_fg = 0xFFFFFFFF;
    uint32_t btn_hov = 0xFF3A3A5E;

    int prev_x = 10;
    wm_fill_rect(win, prev_x, btn_y, 70, 28, state->playlist_count > 0 ? btn_bg : 0xFF1A1A2E);
    wm_draw_string(win, prev_x + 10, btn_y + 9, "Prev", state->playlist_count > 0 ? btn_fg : 0xFF6B7280, 0x00000000);

    int play_x = prev_x + btn_spacing;
    uint32_t play_bg = state->is_playing ? 0xFF0A84FF : 0xFF2A2A4E;
    wm_fill_rect(win, play_x, btn_y, 70, 28, play_bg);
    wm_draw_string(win, play_x + 10, btn_y + 9, state->is_playing ? "Pause" : "Play", btn_fg, 0x00000000);

    int next_x = play_x + btn_spacing;
    wm_fill_rect(win, next_x, btn_y, 70, 28, state->playlist_count > 0 ? btn_bg : 0xFF1A1A2E);
    wm_draw_string(win, next_x + 10, btn_y + 9, "Next", state->playlist_count > 0 ? btn_fg : 0xFF6B7280, 0x00000000);

    int stop_x = next_x + btn_spacing;
    wm_fill_rect(win, stop_x, btn_y, 70, 28, state->is_playing ? btn_bg : 0xFF1A1A2E);
    wm_draw_string(win, stop_x + 10, btn_y + 9, "Stop", state->is_playing ? btn_fg : 0xFF6B7280, 0x00000000);

    wm_fill_rect(win, 10, 200, cw - 20, 1, 0xFF3A3A5E);

    wm_draw_string(win, 10, 210, "Playlist:", 0xFF9CA3AF, 0x00000000);
    char count_buf[32];
    int_to_str(state->playlist_count, count_buf, 32);
    wm_draw_string(win, 80, 210, count_buf, 0xFFFFFFFF, 0x00000000);
    wm_draw_string(win, 80 + str_len(count_buf) * 8, 210, " tracks", 0xFF6B7280, 0x00000000);

    int list_y = 228;
    int row_h = 20;
    int visible_rows = (ch - list_y - 10) / row_h;
    if (visible_rows < 1)
        visible_rows = 1;

    int start_idx = 0;
    int end_idx = start_idx + visible_rows;
    if (end_idx > state->playlist_count)
        end_idx = state->playlist_count;

    for (int i = start_idx; i < end_idx; i++)
    {
        int ry = list_y + (i - start_idx) * row_h;

        if (i == state->selected_idx)
        {
            wm_fill_rect(win, 8, ry - 1, cw - 16, row_h, 0xFF2A2A4E);
        }
        if (i == state->current_track_idx && state->is_playing)
        {
            uint32_t indicator_color = 0xFF0A84FF;
            wm_fill_rect(win, 10, ry + 6, 4, 8, indicator_color);
        }

        const char *track = state->playlist[i];
        int track_len = str_len(track);
        if (track_len > 38)
            track_len = 38;
        char buf[40];
        for (int j = 0; j < track_len; j++)
            buf[j] = track[j];
        buf[track_len] = '\0';

        uint32_t track_color = (i == state->current_track_idx) ? 0xFF0A84FF : 0xFFFFFFFF;
        wm_draw_string(win, 20, ry, buf, track_color, 0x00000000);
    }

    int mode_y = ch - 28;
    uint32_t mode_color = 0xFF6B7280;
    if (state->shuffle_mode)
        mode_color = 0xFF0A84FF;
    wm_draw_string(win, 10, mode_y, "[S] Shuffle", mode_color, 0x00000000);
    if (state->repeat_mode)
        mode_color = 0xFF0A84FF;
    else
        mode_color = 0xFF6B7280;
    wm_draw_string(win, 120, mode_y, "[R] Repeat", mode_color, 0x00000000);

    wm_draw_string(win, cw - 200, mode_y, "Up/Down: Select | Double: Play",
                   0xFF4B5563, 0x00000000);
}

static void audio_manager_on_key(Window *win, char key, int key_code, bool ctrl, bool shift)
{
    AudioManagerState *state = (AudioManagerState *)win->app_data;
    if (!state)
        return;

    if (key == 0 && key_code == 0x48)
    {
        if (state->selected_idx > 0)
            state->selected_idx--;
    }
    else if (key == 0 && key_code == 0x50)
    {
        if (state->selected_idx < state->playlist_count - 1)
            state->selected_idx++;
    }
    else if (key == 's' || key == 'S')
    {
        state->shuffle_mode = !state->shuffle_mode;
    }
    else if (key == 'r' || key == 'R')
    {
        state->repeat_mode = !state->repeat_mode;
    }
}

void app_audio_manager_create(void)
{
    int sw = wm_get_screen_w();
    int sh = wm_get_screen_h();
    int w = 400;
    int h = 480;
    int x = (sw - w) / 2;
    int y = (sh - WM_TASKBAR_HEIGHT - h) / 2;

    Window *win = wm_create_window("Audio Manager", x, y, w, h);
    if (!win)
        return;

    AudioManagerState *state = (AudioManagerState *)win->app_data;
    win->user_data = state;

    state->win = win;
    state->is_playing = false;
    state->volume = 0.7f;
    state->position = 0;
    state->duration = 210;
    state->playlist_count = 0;
    state->current_track_idx = -1;
    state->selected_idx = 0;
    state->last_update = 0;
    state->shuffle_mode = false;
    state->repeat_mode = false;
    state->playlist_mode = true;

    strcpy(state->playlist[state->playlist_count++], "Startup Sound.wav");
    strcpy(state->playlist[state->playlist_count++], "System Notification.wav");
    strcpy(state->playlist[state->playlist_count++], "boot_jingle.wav");
    strcpy(state->playlist[state->playlist_count++], "logout_chime.wav");
    strcpy(state->playlist[state->playlist_count++], "error_beep.wav");
    strcpy(state->playlist[state->playlist_count++], "click.wav");
    strcpy(state->playlist[state->playlist_count++], "music/demo_track.mp3");

    win->on_draw = audio_manager_on_draw;
    win->on_key = audio_manager_on_key;
}
