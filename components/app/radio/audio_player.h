#ifndef __AUDIO_PLAYER_H
#define __AUDIO_PLAYER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

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
