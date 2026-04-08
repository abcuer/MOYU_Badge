#ifndef __AI_CHAT_H
#define __AI_CHAT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    AI_CHAT_STATE_IDLE = 0,
    AI_CHAT_STATE_LISTENING_WAKEWORD,
    AI_CHAT_STATE_RECORDING_QUERY,
    AI_CHAT_STATE_UPLOADING,
    AI_CHAT_STATE_WAITING_REPLY,
    AI_CHAT_STATE_PLAYING_REPLY,
    AI_CHAT_STATE_ERROR,
} ai_chat_state_t;

void ai_chat_init(void);
void ai_chat_enter_mode(void);
void ai_chat_exit_mode(void);
void ai_chat_handle_short_press(void);
void ai_chat_feed_pcm(const uint8_t *pcm_data, size_t pcm_len);
void ai_chat_handle_ws_event(const char *payload, int payload_len, bool is_binary);
ai_chat_state_t ai_chat_get_state(void);
uint16_t ai_chat_get_peak_level(void);
uint32_t ai_chat_get_last_error_code(void);
bool ai_chat_is_active(void);
bool ai_chat_is_configured(void);

#ifdef __cplusplus
}
#endif

#endif
