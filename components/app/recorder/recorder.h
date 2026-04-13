#ifndef __RECORDER_H
#define __RECORDER_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define RECORDER_SAMPLE_RATE        24000U
#define RECORDER_BITS_PER_SAMPLE    16U
#define RECORDER_CHANNELS           1U
#define RECORDER_CHUNK_SAMPLES      320U
#define RECORDER_MAX_SECONDS        90U
#define RECORDER_GAIN_SHIFT         1U
#define RECORDER_TASK_STACK         4096U
#define RECORDER_TASK_PRIORITY      5U
#define RECORDER_IO_TIMEOUT_MS      500U
#define RECORDER_SPEAKER_WARMUP_MS  20U

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
void recorder_handle_long_press(void);
void recorder_stop_and_reset(void);
recorder_state_t recorder_get_state(void);
uint16_t recorder_get_peak_level(void);
uint32_t recorder_get_recorded_ms(void);
bool recorder_is_active(void);
bool recorder_has_recording(void);

#ifdef __cplusplus
}
#endif

#endif
