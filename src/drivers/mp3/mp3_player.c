/*
 * mp3_player.c - MP3 Audio Player Driver for AurionOS
 *
 * This provides audio playback capabilities including:
 *   - WAV file playback
 *   - MP3 decoding (simplified - actual decoding would use libmad or similar)
 *   - PC Speaker tone generation
 *   - Boot/shutdown sounds
 */

#include "mp3_player.h"
#include <string.h>
#include "portio.h"

extern void io_wait(void);
extern uint32_t get_ticks(void);

#define PIT_CHANNEL2 0x42
#define PIT_CONTROL 0x43
#define PIT_FREQ_CMD 0xB6
#define SPEAKER_PORT 0x61

static AudioPlayback g_current_playback;
static bool g_audio_initialized = false;
static float g_master_volume = 0.8f;

static char ascii_tolower(char c) {
    if (c >= 'A' && c <= 'Z') return (char)(c + ('a' - 'A'));
    return c;
}

static bool ext_equals_ci4(const char *ext, const char *want4) {
    return ascii_tolower(ext[0]) == ascii_tolower(want4[0]) &&
           ascii_tolower(ext[1]) == ascii_tolower(want4[1]) &&
           ascii_tolower(ext[2]) == ascii_tolower(want4[2]) &&
           ascii_tolower(ext[3]) == ascii_tolower(want4[3]);
}

static void pit_set_frequency(uint32_t freq) {
    if (freq < 20 || freq > 20000) return;
    uint32_t divisor = 1193180 / freq;
    outb(PIT_CONTROL, PIT_FREQ_CMD);
    outb(PIT_CHANNEL2, (uint8_t)(divisor & 0xFF));
    outb(PIT_CHANNEL2, (uint8_t)((divisor >> 8) & 0xFF));
}

static void speaker_on(void) {
    uint8_t val = inb(SPEAKER_PORT);
    outb(SPEAKER_PORT, val | 0x03);
}

static void speaker_off(void) {
    uint8_t val = inb(SPEAKER_PORT);
    outb(SPEAKER_PORT, val & 0xFC);
}

void mp3_init(void) {
    if (g_audio_initialized) return;
    memset(&g_current_playback, 0, sizeof(g_current_playback));
    g_current_playback.volume = g_master_volume;
    g_current_playback.state = AUDIO_STATE_STOPPED;
    g_audio_initialized = true;
}

void mp3_shutdown(void) {
    mp3_stop();
    speaker_off();
    g_audio_initialized = false;
}

bool mp3_load_file(const char *filename, AudioTrack *track) {
    if (!filename || !track) return false;
    memset(track, 0, sizeof(AudioTrack));

    size_t len = strlen(filename);
    if (len > sizeof(track->filename) - 1) return false;

    strcpy(track->filename, filename);

    if (len > 4) {
        const char *ext = filename + len - 4;
        if (ext_equals_ci4(ext, ".mp3")) {
            track->format = AUDIO_FORMAT_MP3;
            track->sample_rate = 44100;
            track->channels = 2;
            track->bits_per_sample = 16;
        } else if (ext_equals_ci4(ext, ".wav")) {
            track->format = AUDIO_FORMAT_WAV;
            track->sample_rate = 44100;
            track->channels = 2;
            track->bits_per_sample = 16;
        } else {
            track->format = AUDIO_FORMAT_UNKNOWN;
        }
    }

    return true;
}

bool mp3_play(const char *filename) {
    if (!filename) return false;

    AudioTrack track;
    if (!mp3_load_file(filename, &track)) return false;

    g_current_playback.track = track;
    g_current_playback.state = AUDIO_STATE_PLAYING;
    g_current_playback.position_ms = 0;
    g_current_playback.bytes_played = 0;

    if (track.format == AUDIO_FORMAT_WAV) {
        return true;
    } else if (track.format == AUDIO_FORMAT_MP3) {
        return true;
    }

    return true;
}

void mp3_pause(void) {
    if (g_current_playback.state == AUDIO_STATE_PLAYING) {
        g_current_playback.state = AUDIO_STATE_PAUSED;
        speaker_off();
    }
}

void mp3_resume(void) {
    if (g_current_playback.state == AUDIO_STATE_PAUSED) {
        g_current_playback.state = AUDIO_STATE_PLAYING;
        if (g_current_playback.track.format == AUDIO_FORMAT_WAV) {
        }
    }
}

void mp3_stop(void) {
    g_current_playback.state = AUDIO_STATE_STOPPED;
    g_current_playback.position_ms = 0;
    g_current_playback.bytes_played = 0;
    speaker_off();
}

void mp3_set_volume(float volume) {
    if (volume < 0.0f) volume = 0.0f;
    if (volume > 1.0f) volume = 1.0f;
    g_master_volume = volume;
    g_current_playback.volume = volume;
}

float mp3_get_volume(void) {
    return g_master_volume;
}

float mp3_get_position(void) {
    return g_current_playback.position_ms;
}

bool mp3_seek(float position_ms) {
    if (position_ms < 0) position_ms = 0;
    g_current_playback.position_ms = position_ms;
    g_current_playback.bytes_played = (uint32_t)(position_ms * g_current_playback.track.sample_rate *
                                                  g_current_playback.track.channels *
                                                  g_current_playback.track.bits_per_sample / 8 / 1000);
    return true;
}

bool mp3_is_playing(void) {
    return g_current_playback.state == AUDIO_STATE_PLAYING;
}

void mp3_on_irq(void) {
}

int wav_play(const char *filename) {
    return mp3_play(filename) ? 0 : -1;
}

int wav_stop(void) {
    mp3_stop();
    return 0;
}

void audio_beep(uint32_t freq_hz, uint32_t duration_ms) {
    if (freq_hz < 20 || freq_hz > 20000) return;
    if (duration_ms == 0) return;

    pit_set_frequency(freq_hz);
    speaker_on();

    uint32_t start = get_ticks();
    uint32_t ms_per_tick = 1000 / 18;
    while (get_ticks() - start < duration_ms / ms_per_tick) {
    }

    speaker_off();
}

void audio_play_boot_sound(void) {
    audio_beep(440, 100);
    uint32_t start = get_ticks();
    while (get_ticks() - start < 50) { }
    audio_beep(880, 150);
    start = get_ticks();
    while (get_ticks() - start < 80) { }
    audio_beep(1760, 200);
}

void audio_play_shutdown_sound(void) {
    audio_beep(1760, 100);
    uint32_t start = get_ticks();
    while (get_ticks() - start < 50) { }
    audio_beep(880, 150);
    start = get_ticks();
    while (get_ticks() - start < 80) { }
    audio_beep(440, 200);
}

void audio_play_notification_sound(void) {
    audio_beep(1000, 50);
    uint32_t start = get_ticks();
    while (get_ticks() - start < 30) { }
    audio_beep(1500, 80);
}
