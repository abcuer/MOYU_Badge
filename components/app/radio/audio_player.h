#ifndef __AUDIO_PLAYER_H
#define __AUDIO_PLAYER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "settings.h"

#ifdef __cplusplus
extern "C" {
#endif

#define AUDIO_PLAYER_TAG                    "audio_player"
#define AUDIO_DEFAULT_VOLUME_PERCENT        SETTINGS_DEFAULT_VOLUME
#define AUDIO_MAX_CONSECUTIVE_DECODE_ERRORS 24
#define AUDIO_MAX_CONSECUTIVE_EMPTY_READS   300
#define AUDIO_HTTP_CHUNK_SIZE               1024
#define AUDIO_I2S_WRITE_TIMEOUT_MS          300
#define AUDIO_PCM_BUFFER_SIZE               8192
#define AUDIO_PCM_PREBUFFER_SIZE            (256 * 1024)
#define AUDIO_PCM_READ_CHUNK_SIZE           4096
#define AUDIO_PCM_RESUME_SIZE               (192 * 1024)
#define AUDIO_PCM_RING_SIZE                 (1024 * 1024)
#define AUDIO_PCM_POLL_MS                   10
#define AUDIO_PCM_UNDERRUN_GRACE_MS         800
#define AUDIO_RAW_BUFFER_SIZE               8192
#define AUDIO_RETRY_DELAY_MS                2000
#define AUDIO_TASK_PRIORITY                 5
#define AUDIO_TASK_STACK_SIZE               12288
#define AUDIO_LED_TASK_STACK_SIZE           3072
#define AUDIO_LED_UPDATE_MS                 40
#define AUDIO_FADE_IN_SAMPLES               2048

typedef enum {
    AUDIO_STATE_IDLE = 0,
    AUDIO_STATE_BUFFERING,
    AUDIO_STATE_PLAYING,
    AUDIO_STATE_ERROR,
    AUDIO_STATE_NO_WIFI,
} audio_state_t;

typedef struct {
    const char *name;
    const char *url;
} audio_station_t;

typedef struct {
    int metaint;
    int audio_bytes_left;
    int metadata_bytes_left;
    bool expect_metadata_len;
} audio_icy_filter_t;

void audio_player_init(void);
void audio_player_enter_radio_mode(void);
void audio_player_exit_radio_mode(void);
void audio_player_next_station(void);
audio_state_t audio_player_get_state(void);
const audio_station_t *audio_player_get_station(void);
size_t audio_player_get_station_count(void);
bool audio_player_is_active(void);
void audio_player_set_volume(uint8_t volume_percent);
uint8_t audio_player_get_volume(void);

#ifdef __cplusplus
}
#endif

#endif
