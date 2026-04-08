#ifndef __RECORDER_H
#define __RECORDER_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    RECORDER_STATE_IDLE = 0,
    RECORDER_STATE_RECORDING,
    RECORDER_STATE_PLAYING,
    RECORDER_STATE_ERROR,
} recorder_state_t;

void recorder_init(void);
void recorder_enter_mode(void);
void recorder_exit_mode(void);
void recorder_handle_short_press(void);
void recorder_stop_and_reset(void);
recorder_state_t recorder_get_state(void);
uint16_t recorder_get_peak_level(void);
uint32_t recorder_get_recorded_ms(void);
bool recorder_is_active(void);
const char *recorder_get_play_variant_name(void);

#ifdef __cplusplus
}
#endif

#endif
