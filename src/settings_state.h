#ifndef SETTINGS_STATE_H
#define SETTINGS_STATE_H

#include <stdint.h>

/* Global settings for Aurion OS */
typedef struct {
    uint32_t bg_color;         /* 0=Blue (default), 1=Green, 2=Red, 3=White, 4=Black */
    int window_style;          /* 0=MacOS (default), 1=Windows */
    int startup_app_idx;       /* -1=None; else index into desktop_apps[] */
    int dock_magnification;    /* 1=Enabled (default), 0=Disabled */
    int dock_transparent;      /* 1=Transparent (default), 0=Opaque */
    int resolution;            /* 0=800x600, 1=1024x768 (def), 2=1280x720, 3=1280x1024, 4=1440x900, 5=1600x900, 6=1920x1080, 7=2560x1440 */
} OSSettings;

/* The global settings instance */
extern OSSettings g_settings;

/* Predefined colors for background */
static const uint32_t g_bg_colors[] = {
    0xFF002D5A, /* Deep Midnight Blue (Default) */
    0xFF0A160A, /* Green */
    0xFF160A0A, /* Red */
    0xFFF0F0F0, /* White */
    0xFF000000  /* Pure Black */
};

#endif
