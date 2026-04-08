#include "ai_chat.h"

#include <stdio.h>
#include <string.h>

#include "cJSON.h"
#include "esp_crt_bundle.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_websocket_client.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "inmp441.h"
#include "max98357.h"
#include "mbedtls/base64.h"
#include "miniz.h"
#include "settings.h"
#include "wifi_manager.h"

#define AI_CHAT_TAG                       "ai_chat"
#define AI_CHAT_SAMPLE_RATE               16000
#define AI_CHAT_BITS_PER_SAMPLE           16
#define AI_CHAT_CHANNELS                  1
#define AI_CHAT_CHUNK_SAMPLES             320
#define AI_CHAT_MIN_TRIGGER_LEVEL         3500
#define AI_CHAT_TRIGGER_FRAMES            5
#define AI_CHAT_SILENCE_LEVEL             700
#define AI_CHAT_SILENCE_END_FRAMES        20
#define AI_CHAT_MAX_RECORD_MS             7000
#define AI_CHAT_LISTEN_SETTLE_MS          600
#define AI_CHAT_REPLY_RING_SIZE           (96 * 1024)
#define AI_CHAT_TASK_STACK                8192
#define AI_CHAT_TASK_PRIORITY             5
#define AI_CHAT_SEND_TIMEOUT_MS           2000
#define AI_CHAT_PLAY_TIMEOUT_MS           200
#define AI_CHAT_WAIT_REPLY_TIMEOUT_MS     15000
#define AI_CHAT_ERROR_RETRY_MS            1200
#define AI_CHAT_CONNECT_POLL_MS           100
#define AI_CHAT_CONNECT_RETRY_COUNT       50
#define AI_CHAT_VOLC_TTS_HOST             "openspeech.bytedance.com"
#define AI_CHAT_VOLC_DIALOG_PATH          "/api/v3/realtime/dialogue"
#define AI_CHAT_WS_BUFFER_SIZE            16384
#define AI_CHAT_DIALOG_WAIT_MS            8000

#define AI_CHAT_PROTO_VER                 0x1
#define AI_CHAT_PROTO_HDR_WORDS           0x1
#define AI_CHAT_MSG_CLIENT_FULL_REQ       0x1
#define AI_CHAT_MSG_CLIENT_AUDIO_REQ      0x2
#define AI_CHAT_MSG_SERVER_FULL_RESP      0x9
#define AI_CHAT_MSG_SERVER_ACK            0xB
#define AI_CHAT_MSG_SERVER_ERR            0xF
#define AI_CHAT_FLAG_HAS_EVENT            0x4
#define AI_CHAT_FLAG_HAS_SEQ              0x2
#define AI_CHAT_FLAG_TAIL_PACKET          0x2
#define AI_CHAT_SERIAL_NONE               0x0
#define AI_CHAT_SERIAL_JSON               0x1
#define AI_CHAT_COMPRESS_NONE             0x0
#define AI_CHAT_COMPRESS_GZIP             0x1

#define AI_CHAT_GZIP_HEADER_SIZE          10
#define AI_CHAT_GZIP_TRAILER_SIZE         8
#define AI_CHAT_WS_RX_MAX_SIZE            (192 * 1024)
#define AI_CHAT_WS_DECODE_MAX_SIZE        (192 * 1024)

#define AI_CHAT_EVT_START_CONNECTION      1
#define AI_CHAT_EVT_FINISH_CONNECTION     2
#define AI_CHAT_EVT_START_SESSION         100
#define AI_CHAT_EVT_FINISH_SESSION        102
#define AI_CHAT_EVT_TASK_REQUEST          200
#define AI_CHAT_EVT_CHAT_TTS_TEXT         500

#define AI_CHAT_EVT_CONNECTION_STARTED    50
#define AI_CHAT_EVT_CONNECTION_FAILED     51
#define AI_CHAT_EVT_SESSION_STARTED       150
#define AI_CHAT_EVT_SESSION_FINISHED      152
#define AI_CHAT_EVT_SESSION_FAILED        153
#define AI_CHAT_EVT_TTS_SENTENCE_START    350
#define AI_CHAT_EVT_TTS_RESPONSE          352
#define AI_CHAT_EVT_TTS_ENDED             359
#define AI_CHAT_EVT_ASR_RESPONSE          451
#define AI_CHAT_EVT_ASR_ENDED             459
#define AI_CHAT_EVT_CHAT_RESPONSE         550
#define AI_CHAT_EVT_CHAT_ENDED            559
#define AI_CHAT_EVT_AUDIO_IDLE_TIMEOUT    599

static const char *TAG = AI_CHAT_TAG;

static TaskHandle_t s_ai_task = NULL;
static volatile bool s_active = false;
static volatile bool s_ws_connected = false;
static volatile ai_chat_state_t s_state = AI_CHAT_STATE_IDLE;
static volatile uint16_t s_peak_level = 0;
static volatile uint32_t s_last_error = 0;
static volatile bool s_reply_end_received = false;
static volatile uint32_t s_session_id = 0;
static esp_websocket_client_handle_t s_ws = NULL;
static SemaphoreHandle_t s_reply_mutex = NULL;
static uint8_t *s_reply_ring = NULL;
static uint8_t *s_b64_decode_buf = NULL;
static size_t s_reply_head = 0;
static size_t s_reply_tail = 0;
static size_t s_reply_size = 0;
static uint32_t s_reply_sample_rate = AI_CHAT_SAMPLE_RATE;
static uint8_t s_reply_channels = AI_CHAT_CHANNELS;
static bool s_waiting_for_reply = false;
static uint32_t s_state_enter_ms = 0;
static bool s_dialog_connection_ready = false;
static bool s_dialog_session_ready = false;
static char s_dialog_connect_id[40] = {0};
static char s_dialog_session_id[40] = {0};
static uint8_t *s_ws_rx_buf = NULL;
static size_t s_ws_rx_buf_cap = 0;
static size_t s_ws_rx_payload_len = 0;
static bool s_ws_rx_binary = false;
static volatile bool s_record_start_requested = false;
static volatile bool s_record_stop_requested = false;
static bool s_logged_upload_chunk_diag = false;

static void ai_chat_make_request_id(char *buf, size_t len)
{
    if (buf == NULL || len < 33) {
        return;
    }

    uint32_t r0 = esp_random();
    uint32_t r1 = esp_random();
    uint32_t r2 = esp_random();
    uint32_t r3 = esp_random();
    snprintf(buf, len, "%08lx%08lx%08lx%08lx",
             (unsigned long)r0,
             (unsigned long)r1,
             (unsigned long)r2,
             (unsigned long)r3);
}

static bool ai_chat_is_volc_tts_endpoint(const char *endpoint)
{
    return endpoint != NULL &&
           strstr(endpoint, AI_CHAT_VOLC_TTS_HOST) != NULL &&
           strstr(endpoint, "/api/v3/tts/") != NULL;
}

static bool ai_chat_is_volc_dialog_endpoint(const char *endpoint)
{
    return endpoint != NULL &&
           strstr(endpoint, AI_CHAT_VOLC_TTS_HOST) != NULL &&
           strstr(endpoint, AI_CHAT_VOLC_DIALOG_PATH) != NULL;
}

static bool ai_chat_make_uuid(char *buf, size_t len)
{
    if (buf == NULL || len < 37) {
        return false;
    }

    uint32_t r0 = esp_random();
    uint32_t r1 = esp_random();
    uint32_t r2 = esp_random();
    uint32_t r3 = esp_random();
    snprintf(buf, len, "%08lx-%04lx-%04lx-%04lx-%04lx%08lx",
             (unsigned long)r0,
             (unsigned long)((r1 >> 16) & 0xFFFF),
             (unsigned long)(r1 & 0xFFFF),
             (unsigned long)((r2 >> 16) & 0xFFFF),
             (unsigned long)(r2 & 0xFFFF),
             (unsigned long)r3);
    return true;
}

static uint32_t ai_chat_read_u32_be(const uint8_t *src)
{
    return ((uint32_t)src[0] << 24) |
           ((uint32_t)src[1] << 16) |
           ((uint32_t)src[2] << 8) |
           (uint32_t)src[3];
}

static void ai_chat_write_u32_be(uint8_t *dst, uint32_t value)
{
    dst[0] = (uint8_t)((value >> 24) & 0xFF);
    dst[1] = (uint8_t)((value >> 16) & 0xFF);
    dst[2] = (uint8_t)((value >> 8) & 0xFF);
    dst[3] = (uint8_t)(value & 0xFF);
}

static uint32_t ai_chat_read_u32_le(const uint8_t *src)
{
    return (uint32_t)src[0] |
           ((uint32_t)src[1] << 8) |
           ((uint32_t)src[2] << 16) |
           ((uint32_t)src[3] << 24);
}

static void ai_chat_write_u32_le(uint8_t *dst, uint32_t value)
{
    dst[0] = (uint8_t)(value & 0xFF);
    dst[1] = (uint8_t)((value >> 8) & 0xFF);
    dst[2] = (uint8_t)((value >> 16) & 0xFF);
    dst[3] = (uint8_t)((value >> 24) & 0xFF);
}

static uint32_t ai_chat_now_ms(void)
{
    return xTaskGetTickCount() * portTICK_PERIOD_MS;
}

static void ai_chat_set_state(ai_chat_state_t state)
{
    s_state = state;
    s_state_enter_ms = ai_chat_now_ms();
}

static uint16_t ai_chat_calculate_peak(const int16_t *samples, size_t count)
{
    uint16_t peak = 0;

    for (size_t i = 0; i < count; i++) {
        uint16_t level = (samples[i] < 0) ? (uint16_t)(-samples[i]) : (uint16_t)samples[i];
        if (level > peak) {
            peak = level;
        }
    }

    return peak;
}

static void ai_chat_log_upload_chunk_diag(const int16_t *samples, size_t count)
{
    if (samples == NULL || count == 0 || s_logged_upload_chunk_diag) {
        return;
    }

    uint32_t mean_abs = 0;
    size_t preview = count < 8 ? count : 8;
    char preview_buf[96];
    size_t off = 0;

    for (size_t i = 0; i < count; i++) {
        mean_abs += (samples[i] < 0) ? (uint32_t)(-samples[i]) : (uint32_t)samples[i];
    }
    mean_abs /= (uint32_t)count;

    preview_buf[0] = '\0';
    for (size_t i = 0; i < preview; i++) {
        int written = snprintf(preview_buf + off,
                               sizeof(preview_buf) - off,
                               "%s%d",
                               i == 0 ? "" : ",",
                               (int)samples[i]);
        if (written <= 0 || (size_t)written >= (sizeof(preview_buf) - off)) {
            break;
        }
        off += (size_t)written;
    }

    ESP_LOGI(TAG,
             "dialog upload diag samples=%lu peak=%u mean_abs=%lu first=[%s]",
             (unsigned long)count,
             (unsigned)ai_chat_calculate_peak(samples, count),
             (unsigned long)mean_abs,
             preview_buf);
    s_logged_upload_chunk_diag = true;
}

static esp_err_t ai_chat_reply_ring_init(void)
{
    if (s_reply_mutex == NULL) {
        s_reply_mutex = xSemaphoreCreateMutex();
        if (s_reply_mutex == NULL) {
            return ESP_ERR_NO_MEM;
        }
    }

    if (s_reply_ring != NULL) {
        return ESP_OK;
    }

    s_reply_ring = heap_caps_malloc(AI_CHAT_REPLY_RING_SIZE, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (s_reply_ring == NULL) {
        s_reply_ring = heap_caps_malloc(AI_CHAT_REPLY_RING_SIZE, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    }
    if (s_reply_ring == NULL) {
        return ESP_ERR_NO_MEM;
    }

    if (s_b64_decode_buf == NULL) {
        s_b64_decode_buf = heap_caps_malloc(AI_CHAT_REPLY_RING_SIZE, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (s_b64_decode_buf == NULL) {
            s_b64_decode_buf = heap_caps_malloc(AI_CHAT_REPLY_RING_SIZE, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
        }
        if (s_b64_decode_buf == NULL) {
            return ESP_ERR_NO_MEM;
        }
    }

    s_reply_head = 0;
    s_reply_tail = 0;
    s_reply_size = 0;
    return ESP_OK;
}

static void ai_chat_reply_ring_reset(void)
{
    if (s_reply_mutex == NULL) {
        return;
    }

    xSemaphoreTake(s_reply_mutex, portMAX_DELAY);
    s_reply_head = 0;
    s_reply_tail = 0;
    s_reply_size = 0;
    xSemaphoreGive(s_reply_mutex);
}

static size_t ai_chat_reply_ring_bytes(void)
{
    size_t size = 0;

    if (s_reply_mutex == NULL) {
        return 0;
    }

    xSemaphoreTake(s_reply_mutex, portMAX_DELAY);
    size = s_reply_size;
    xSemaphoreGive(s_reply_mutex);
    return size;
}

static size_t ai_chat_reply_ring_pop(uint8_t *dst, size_t len)
{
    size_t copied = 0;

    if (s_reply_mutex == NULL || dst == NULL || len == 0) {
        return 0;
    }

    xSemaphoreTake(s_reply_mutex, portMAX_DELAY);
    while (copied < len && s_reply_size > 0) {
        size_t contiguous = AI_CHAT_REPLY_RING_SIZE - s_reply_tail;
        size_t chunk = len - copied;
        if (chunk > s_reply_size) {
            chunk = s_reply_size;
        }
        if (chunk > contiguous) {
            chunk = contiguous;
        }
        memcpy(dst + copied, s_reply_ring + s_reply_tail, chunk);
        s_reply_tail = (s_reply_tail + chunk) % AI_CHAT_REPLY_RING_SIZE;
        s_reply_size -= chunk;
        copied += chunk;
    }
    xSemaphoreGive(s_reply_mutex);

    return copied;
}

static void ai_chat_reply_ring_push(const uint8_t *src, size_t len)
{
    size_t copied = 0;

    if (s_reply_mutex == NULL || s_reply_ring == NULL || src == NULL || len == 0) {
        return;
    }

    xSemaphoreTake(s_reply_mutex, portMAX_DELAY);
    while (copied < len && s_reply_size < AI_CHAT_REPLY_RING_SIZE) {
        size_t free_bytes = AI_CHAT_REPLY_RING_SIZE - s_reply_size;
        size_t contiguous = AI_CHAT_REPLY_RING_SIZE - s_reply_head;
        size_t chunk = len - copied;
        if (chunk > free_bytes) {
            chunk = free_bytes;
        }
        if (chunk > contiguous) {
            chunk = contiguous;
        }
        memcpy(s_reply_ring + s_reply_head, src + copied, chunk);
        s_reply_head = (s_reply_head + chunk) % AI_CHAT_REPLY_RING_SIZE;
        s_reply_size += chunk;
        copied += chunk;
    }
    xSemaphoreGive(s_reply_mutex);
}

static void ai_chat_set_error(uint32_t error_code)
{
    s_last_error = error_code;
    s_waiting_for_reply = false;
    ai_chat_set_state(AI_CHAT_STATE_ERROR);
}

static bool ai_chat_endpoint_is_valid(const char *endpoint)
{
    return endpoint != NULL &&
           (strncmp(endpoint, "ws://", 5) == 0 || strncmp(endpoint, "wss://", 6) == 0);
}

static void ai_chat_reset_session_state(void)
{
    s_peak_level = 0;
    s_reply_sample_rate = AI_CHAT_SAMPLE_RATE;
    s_reply_channels = AI_CHAT_CHANNELS;
    s_reply_end_received = false;
    s_waiting_for_reply = false;
    s_dialog_connection_ready = false;
    s_dialog_session_ready = false;
    s_dialog_session_id[0] = '\0';
    s_record_start_requested = false;
    s_record_stop_requested = false;
    s_logged_upload_chunk_diag = false;
    ai_chat_reply_ring_reset();
}

static void ai_chat_reset_ws_rx_state(void)
{
    s_ws_rx_payload_len = 0;
    s_ws_rx_binary = false;
}

static esp_err_t ai_chat_reserve_ws_rx_buffer(size_t need_bytes)
{
    if (need_bytes == 0 || need_bytes > AI_CHAT_WS_RX_MAX_SIZE) {
        return ESP_ERR_INVALID_SIZE;
    }

    if (s_ws_rx_buf != NULL && s_ws_rx_buf_cap >= need_bytes) {
        return ESP_OK;
    }

    uint8_t *new_buf = heap_caps_realloc(s_ws_rx_buf, need_bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (new_buf == NULL) {
        new_buf = heap_caps_realloc(s_ws_rx_buf, need_bytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    }
    if (new_buf == NULL) {
        return ESP_ERR_NO_MEM;
    }

    s_ws_rx_buf = new_buf;
    s_ws_rx_buf_cap = need_bytes;
    return ESP_OK;
}

static void ai_chat_reset_runtime_state(int *trigger_frames, int *silence_frames, uint32_t *record_ms)
{
    if (trigger_frames != NULL) {
        *trigger_frames = 0;
    }
    if (silence_frames != NULL) {
        *silence_frames = 0;
    }
    if (record_ms != NULL) {
        *record_ms = 0;
    }
}

static void ai_chat_disconnect_ws(void)
{
    if (s_ws != NULL) {
        esp_websocket_client_stop(s_ws);
        esp_websocket_client_destroy(s_ws);
        s_ws = NULL;
    }
    s_ws_connected = false;
    ai_chat_reset_ws_rx_state();
}

static const char *ai_chat_volc_event_name(uint32_t event)
{
    switch (event) {
        case AI_CHAT_EVT_START_CONNECTION: return "StartConnection";
        case AI_CHAT_EVT_FINISH_CONNECTION: return "FinishConnection";
        case AI_CHAT_EVT_START_SESSION: return "StartSession";
        case AI_CHAT_EVT_FINISH_SESSION: return "FinishSession";
        case AI_CHAT_EVT_TASK_REQUEST: return "TaskRequest";
        case AI_CHAT_EVT_CHAT_TTS_TEXT: return "ChatTTSText";
        case AI_CHAT_EVT_CONNECTION_STARTED: return "ConnectionStarted";
        case AI_CHAT_EVT_CONNECTION_FAILED: return "ConnectionFailed";
        case AI_CHAT_EVT_SESSION_STARTED: return "SessionStarted";
        case AI_CHAT_EVT_SESSION_FINISHED: return "SessionFinished";
        case AI_CHAT_EVT_SESSION_FAILED: return "SessionFailed";
        case AI_CHAT_EVT_TTS_SENTENCE_START: return "TTSSentenceStart";
        case AI_CHAT_EVT_TTS_RESPONSE: return "TTSResponse";
        case AI_CHAT_EVT_TTS_ENDED: return "TTSEnded";
        case AI_CHAT_EVT_ASR_RESPONSE: return "ASRResponse";
        case AI_CHAT_EVT_ASR_ENDED: return "ASREnded";
        case AI_CHAT_EVT_CHAT_RESPONSE: return "ChatResponse";
        case AI_CHAT_EVT_CHAT_ENDED: return "ChatEnded";
        default: return "Unknown";
    }
}

static uint8_t *ai_chat_gzip_compress_alloc(const uint8_t *src, size_t src_len, size_t *out_len)
{
    if ((src == NULL && src_len > 0) || out_len == NULL) {
        return NULL;
    }

    size_t blocks = (src_len / 65535U) + 1U;
    size_t deflate_cap = blocks * 5U + src_len;
    size_t total_cap = AI_CHAT_GZIP_HEADER_SIZE + deflate_cap + AI_CHAT_GZIP_TRAILER_SIZE;
    uint8_t *dst = heap_caps_malloc(total_cap, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    size_t src_off = 0;
    size_t dst_off = AI_CHAT_GZIP_HEADER_SIZE;

    if (dst == NULL) {
        return NULL;
    }

    if (total_cap < (AI_CHAT_GZIP_HEADER_SIZE + AI_CHAT_GZIP_TRAILER_SIZE + 5U + src_len)) {
        free(dst);
        return NULL;
    }

    dst[0] = 0x1F;
    dst[1] = 0x8B;
    dst[2] = 0x08;
    dst[3] = 0x00;
    dst[4] = 0x00;
    dst[5] = 0x00;
    dst[6] = 0x00;
    dst[7] = 0x00;
    dst[8] = 0x00;
    dst[9] = 0xFF;

    while (src_off < src_len || (src_len == 0 && src_off == 0)) {
        size_t chunk_len = src_len - src_off;
        bool is_final = false;

        if (chunk_len > 65535U) {
            chunk_len = 65535U;
        } else {
            is_final = true;
        }

        dst[dst_off++] = is_final ? 0x01 : 0x00;
        dst[dst_off++] = (uint8_t)(chunk_len & 0xFF);
        dst[dst_off++] = (uint8_t)((chunk_len >> 8) & 0xFF);
        dst[dst_off++] = (uint8_t)((~chunk_len) & 0xFF);
        dst[dst_off++] = (uint8_t)(((~chunk_len) >> 8) & 0xFF);
        if (chunk_len > 0) {
            memcpy(dst + dst_off, src + src_off, chunk_len);
            dst_off += chunk_len;
            src_off += chunk_len;
        } else {
            src_off = 1;
        }
    }

    uint32_t crc = (uint32_t)mz_crc32(MZ_CRC32_INIT, src, src_len);
    uint8_t *trailer = dst + dst_off;
    ai_chat_write_u32_le(trailer, crc);
    ai_chat_write_u32_le(trailer + 4, (uint32_t)(src_len & 0xFFFFFFFFU));

    *out_len = dst_off + AI_CHAT_GZIP_TRAILER_SIZE;
    return dst;
}

static uint8_t *ai_chat_gzip_decompress_alloc(const uint8_t *src, size_t src_len, size_t *out_len)
{
    if (src == NULL || src_len < (AI_CHAT_GZIP_HEADER_SIZE + AI_CHAT_GZIP_TRAILER_SIZE) || out_len == NULL) {
        return NULL;
    }
    if (src[0] != 0x1F || src[1] != 0x8B || src[2] != 0x08) {
        return NULL;
    }

    size_t offset = AI_CHAT_GZIP_HEADER_SIZE;
    uint8_t flags = src[3];
    if ((flags & 0x04U) != 0) {
        if (offset + 2 > src_len) {
            return NULL;
        }
        uint16_t extra_len = (uint16_t)src[offset] | ((uint16_t)src[offset + 1] << 8);
        offset += 2U + extra_len;
    }
    if ((flags & 0x08U) != 0) {
        while (offset < src_len && src[offset] != '\0') {
            offset++;
        }
        offset++;
    }
    if ((flags & 0x10U) != 0) {
        while (offset < src_len && src[offset] != '\0') {
            offset++;
        }
        offset++;
    }
    if ((flags & 0x02U) != 0) {
        offset += 2;
    }

    if (offset + AI_CHAT_GZIP_TRAILER_SIZE > src_len) {
        return NULL;
    }

    size_t deflate_len = src_len - offset - AI_CHAT_GZIP_TRAILER_SIZE;
    const uint8_t *deflate_ptr = src + offset;
    uint32_t expected_crc = ai_chat_read_u32_le(src + src_len - AI_CHAT_GZIP_TRAILER_SIZE);
    uint32_t expected_size = ai_chat_read_u32_le(src + src_len - 4);

    size_t cap = expected_size > 0 ? expected_size : (src_len * 6U);
    if (cap < 2048U) {
        cap = 2048U;
    }

    while (cap <= AI_CHAT_WS_DECODE_MAX_SIZE) {
        uint8_t *dst = heap_caps_malloc(cap, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (dst == NULL) {
            dst = heap_caps_malloc(cap, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
        }
        if (dst == NULL) {
            return NULL;
        }

        size_t actual = tinfl_decompress_mem_to_mem(dst,
                                                    cap,
                                                    deflate_ptr,
                                                    deflate_len,
                                                    TINFL_FLAG_USING_NON_WRAPPING_OUTPUT_BUF);
        if (actual != TINFL_DECOMPRESS_MEM_TO_MEM_FAILED) {
            uint32_t actual_crc = (uint32_t)mz_crc32(MZ_CRC32_INIT, dst, actual);
            if (actual_crc != expected_crc ||
                (expected_size != 0 && actual != (size_t)expected_size)) {
                free(dst);
                return NULL;
            }
            *out_len = actual;
            return dst;
        }

        free(dst);
        cap *= 2U;
    }

    return NULL;
}

static int ai_chat_send_volc_frame_ex(uint8_t message_type, uint8_t flags,
                                      uint8_t serialization, uint32_t event,
                                      const char *session_id,
                                      const uint8_t *payload, size_t payload_len)
{
    uint8_t header[4];
    size_t session_len = (session_id != NULL && session_id[0] != '\0') ? strlen(session_id) : 0;
    size_t actual_payload_len = payload_len;
    const uint8_t *actual_payload = payload;
    uint8_t *compressed_payload = NULL;
    uint8_t compression = AI_CHAT_COMPRESS_NONE;
    size_t frame_len = sizeof(header) + 4 + (session_len > 0 ? (4 + session_len) : 0) + 4 + payload_len;
    uint8_t *frame = heap_caps_malloc(frame_len, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    size_t off = 0;
    int sent = -1;

    if (s_ws == NULL || !s_ws_connected || !esp_websocket_client_is_connected(s_ws)) {
        ESP_LOGE(TAG, "dialog send aborted: ws not connected evt=%lu(%s)",
                 (unsigned long)event, ai_chat_volc_event_name(event));
        if (frame != NULL) {
            free(frame);
        }
        return -1;
    }

    if (frame == NULL) {
        ESP_LOGE(TAG, "dialog send aborted: frame alloc failed evt=%lu(%s) bytes=%lu",
                 (unsigned long)event,
                 ai_chat_volc_event_name(event),
                 (unsigned long)frame_len);
        return -1;
    }

    if (ai_chat_is_volc_dialog_endpoint(settings_get_ai_endpoint())) {
        compressed_payload = ai_chat_gzip_compress_alloc(payload, payload_len, &actual_payload_len);
        if (compressed_payload == NULL) {
            ESP_LOGW(TAG, "dialog gzip encode failed, fallback to plain evt=%lu(%s)",
                     (unsigned long)event, ai_chat_volc_event_name(event));
            actual_payload_len = payload_len;
            actual_payload = payload;
            compression = AI_CHAT_COMPRESS_NONE;
        } else {
            actual_payload = compressed_payload;
            compression = AI_CHAT_COMPRESS_GZIP;
            frame_len = sizeof(header) + 4 + (session_len > 0 ? (4 + session_len) : 0) + 4 + actual_payload_len;
            free(frame);
            frame = heap_caps_malloc(frame_len, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
            if (frame == NULL) {
                ESP_LOGE(TAG, "dialog send aborted: frame realloc failed evt=%lu(%s) bytes=%lu",
                         (unsigned long)event,
                         ai_chat_volc_event_name(event),
                         (unsigned long)frame_len);
                free(compressed_payload);
                return -1;
            }
        }
    }

    header[0] = (AI_CHAT_PROTO_VER << 4) | AI_CHAT_PROTO_HDR_WORDS;
    header[1] = (message_type << 4) | flags;
    header[2] = (serialization << 4) | compression;
    header[3] = 0;

    memcpy(frame + off, header, sizeof(header));
    off += sizeof(header);
    ai_chat_write_u32_be(frame + off, event);
    off += 4;
    if (session_len > 0) {
        ai_chat_write_u32_be(frame + off, (uint32_t)session_len);
        off += 4;
        memcpy(frame + off, session_id, session_len);
        off += session_len;
    }
    ai_chat_write_u32_be(frame + off, (uint32_t)actual_payload_len);
    off += 4;
    if (actual_payload_len > 0 && actual_payload != NULL) {
        memcpy(frame + off, actual_payload, actual_payload_len);
        off += actual_payload_len;
    }

    ESP_LOGI(TAG, "dialog send evt=%lu(%s) msg=%u serial=%u compress=%u payload=%lu frame=%lu session=%s",
             (unsigned long)event,
             ai_chat_volc_event_name(event),
             (unsigned)message_type,
             (unsigned)serialization,
             (unsigned)compression,
             (unsigned long)actual_payload_len,
             (unsigned long)off,
             session_len > 0 ? session_id : "-");

    sent = esp_websocket_client_send_bin(s_ws, (const char *)frame, (int)off,
                                         pdMS_TO_TICKS(AI_CHAT_SEND_TIMEOUT_MS));
    if (sent < 0) {
        ESP_LOGE(TAG, "dialog send failed evt=%lu(%s) ret=%d",
                 (unsigned long)event, ai_chat_volc_event_name(event), sent);
    } else {
        ESP_LOGI(TAG, "dialog send ok evt=%lu(%s) ret=%d",
                 (unsigned long)event, ai_chat_volc_event_name(event), sent);
    }
    free(frame);
    if (compressed_payload != NULL) {
        free(compressed_payload);
    }
    return sent;
}

static int ai_chat_send_volc_frame(uint8_t message_type, uint8_t serialization,
                                   uint32_t event, const char *session_id,
                                   const uint8_t *payload, size_t payload_len)
{
    return ai_chat_send_volc_frame_ex(message_type,
                                      AI_CHAT_FLAG_HAS_EVENT,
                                      serialization,
                                      event,
                                      session_id,
                                      payload,
                                      payload_len);
}

static int ai_chat_send_volc_json_event(uint32_t event, const char *session_id, cJSON *root)
{
    char *json = NULL;
    int ret;

    if (root == NULL) {
        return -1;
    }
    json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (json == NULL) {
        return -1;
    }

    ret = ai_chat_send_volc_frame(AI_CHAT_MSG_CLIENT_FULL_REQ, AI_CHAT_SERIAL_JSON,
                                  event, session_id, (const uint8_t *)json, strlen(json));
    cJSON_free(json);
    return ret;
}

static cJSON *ai_chat_build_volc_start_session(void)
{
    cJSON *root = cJSON_CreateObject();
    cJSON *request_meta = cJSON_AddObjectToObject(root, "request_meta");
    cJSON *user = cJSON_AddObjectToObject(root, "user");
    cJSON *request = cJSON_AddObjectToObject(root, "request");
    cJSON *source_audio = cJSON_AddObjectToObject(root, "source_audio");
    cJSON *target_audio = cJSON_AddObjectToObject(root, "target_audio");
    cJSON *audio = cJSON_AddObjectToObject(root, "audio");

    if (root == NULL) {
        return NULL;
    }

    if (request_meta != NULL) {
        cJSON_AddStringToObject(request_meta, "session_id", s_dialog_session_id);
    }
    if (user != NULL) {
        cJSON_AddStringToObject(user, "uid", settings_get_ai_device_id());
        cJSON_AddStringToObject(user, "did", settings_get_ai_device_id());
    }
    if (request != NULL) {
        cJSON_AddStringToObject(request, "mode", "s2s");
    }
    if (source_audio != NULL) {
        cJSON_AddStringToObject(source_audio, "format", "pcm");
        cJSON_AddStringToObject(source_audio, "codec", "raw");
        cJSON_AddNumberToObject(source_audio, "rate", AI_CHAT_SAMPLE_RATE);
        cJSON_AddNumberToObject(source_audio, "bits", AI_CHAT_BITS_PER_SAMPLE);
        cJSON_AddNumberToObject(source_audio, "channel", AI_CHAT_CHANNELS);
    }
    if (target_audio != NULL) {
        cJSON_AddStringToObject(target_audio, "format", "pcm");
        cJSON_AddNumberToObject(target_audio, "rate", AI_CHAT_SAMPLE_RATE);
    }
    if (audio != NULL) {
        cJSON_AddStringToObject(audio, "format", "pcm");
        cJSON_AddNumberToObject(audio, "sample_rate", AI_CHAT_SAMPLE_RATE);
        cJSON_AddNumberToObject(audio, "rate", AI_CHAT_SAMPLE_RATE);
        cJSON_AddNumberToObject(audio, "channel_num", AI_CHAT_CHANNELS);
        cJSON_AddNumberToObject(audio, "channel", AI_CHAT_CHANNELS);
        cJSON_AddNumberToObject(audio, "bits_per_sample", AI_CHAT_BITS_PER_SAMPLE);
        cJSON_AddNumberToObject(audio, "bits", AI_CHAT_BITS_PER_SAMPLE);
    }
    return root;
}

static esp_err_t ai_chat_wait_for_dialog_flag(volatile bool *flag)
{
    uint32_t start_ms = ai_chat_now_ms();
    while ((ai_chat_now_ms() - start_ms) < AI_CHAT_DIALOG_WAIT_MS) {
        if (!s_active || s_state == AI_CHAT_STATE_ERROR) {
            return ESP_FAIL;
        }
        if (*flag) {
            return ESP_OK;
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }
    return ESP_ERR_TIMEOUT;
}

static esp_err_t ai_chat_open_volc_dialog_session(void)
{
    if (!ai_chat_is_volc_dialog_endpoint(settings_get_ai_endpoint())) {
        return ESP_OK;
    }
    if (s_dialog_session_ready) {
        return ESP_OK;
    }
    if (s_dialog_connect_id[0] == '\0') {
        ai_chat_make_uuid(s_dialog_connect_id, sizeof(s_dialog_connect_id));
    }
    if (s_dialog_session_id[0] == '\0') {
        ai_chat_make_uuid(s_dialog_session_id, sizeof(s_dialog_session_id));
    }

    s_dialog_connection_ready = false;
    s_dialog_session_ready = false;

    ESP_LOGI(TAG, "dialog open: connect_id=%s session_id=%s",
             s_dialog_connect_id, s_dialog_session_id);
    vTaskDelay(pdMS_TO_TICKS(80));

    if (ai_chat_send_volc_frame(AI_CHAT_MSG_CLIENT_FULL_REQ, AI_CHAT_SERIAL_JSON,
                                AI_CHAT_EVT_START_CONNECTION, NULL,
                                (const uint8_t *)"{}", 2) < 0) {
        ESP_LOGE(TAG, "dialog StartConnection send failed");
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "dialog sent evt=%u(%s)",
             AI_CHAT_EVT_START_CONNECTION,
             ai_chat_volc_event_name(AI_CHAT_EVT_START_CONNECTION));
    if (ai_chat_wait_for_dialog_flag(&s_dialog_connection_ready) != ESP_OK) {
        ESP_LOGE(TAG, "dialog wait ConnectionStarted timeout");
        return ESP_FAIL;
    }

    if (ai_chat_send_volc_json_event(AI_CHAT_EVT_START_SESSION,
                                     s_dialog_session_id,
                                     ai_chat_build_volc_start_session()) < 0) {
        ESP_LOGE(TAG, "dialog StartSession send failed");
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "dialog sent evt=%u(%s) session=%s",
             AI_CHAT_EVT_START_SESSION,
             ai_chat_volc_event_name(AI_CHAT_EVT_START_SESSION),
             s_dialog_session_id);
    if (ai_chat_wait_for_dialog_flag(&s_dialog_session_ready) != ESP_OK) {
        ESP_LOGE(TAG, "dialog wait SessionStarted timeout");
        return ESP_FAIL;
    }
    return ESP_OK;
}

static void ai_chat_send_volc_finish(uint32_t event, const char *session_id)
{
    ai_chat_send_volc_frame(AI_CHAT_MSG_CLIENT_FULL_REQ, AI_CHAT_SERIAL_JSON,
                            event, session_id, (const uint8_t *)"{}", 2);
}

static void ai_chat_send_text_json(cJSON *root)
{
    if (root == NULL || s_ws == NULL || !s_ws_connected) {
        if (root != NULL) {
            cJSON_Delete(root);
        }
        return;
    }

    char *json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (json == NULL) {
        return;
    }

    esp_websocket_client_send_text(s_ws, json, strlen(json), pdMS_TO_TICKS(AI_CHAT_SEND_TIMEOUT_MS));
    cJSON_free(json);
}

static void ai_chat_send_session_start(void)
{
    if (ai_chat_is_volc_dialog_endpoint(settings_get_ai_endpoint())) {
        return;
    }

    cJSON *root = cJSON_CreateObject();
    if (root == NULL) {
        return;
    }

    cJSON_AddStringToObject(root, "type", "session_start");
    cJSON_AddNumberToObject(root, "session_id", ++s_session_id);
    cJSON_AddStringToObject(root, "device_id", settings_get_ai_device_id());
    cJSON_AddNumberToObject(root, "sample_rate", AI_CHAT_SAMPLE_RATE);
    cJSON_AddNumberToObject(root, "channels", AI_CHAT_CHANNELS);
    cJSON_AddNumberToObject(root, "bits_per_sample", AI_CHAT_BITS_PER_SAMPLE);
    cJSON_AddStringToObject(root, "format", "pcm_s16le");

    if (ai_chat_is_volc_tts_endpoint(settings_get_ai_endpoint())) {
        cJSON *user = cJSON_AddObjectToObject(root, "user");
        cJSON *req_params = cJSON_AddObjectToObject(root, "req_params");
        cJSON *audio_params = req_params ? cJSON_AddObjectToObject(req_params, "audio_params") : NULL;

        cJSON_AddStringToObject(root, "app_id", settings_get_ai_app_id());
        cJSON_AddStringToObject(root, "resource_id", settings_get_ai_resource_id());
        if (user != NULL) {
            cJSON_AddStringToObject(user, "uid", settings_get_ai_device_id());
        }
        if (req_params != NULL) {
            cJSON_AddStringToObject(req_params, "speaker", settings_get_ai_voice_type());
            cJSON_AddStringToObject(req_params, "voice_type", settings_get_ai_voice_type());
        }
        if (audio_params != NULL) {
            cJSON_AddStringToObject(audio_params, "format", "pcm");
            cJSON_AddNumberToObject(audio_params, "sample_rate", AI_CHAT_SAMPLE_RATE);
            cJSON_AddNumberToObject(audio_params, "channel", AI_CHAT_CHANNELS);
            cJSON_AddNumberToObject(audio_params, "bits_per_sample", AI_CHAT_BITS_PER_SAMPLE);
        }
    }

    ai_chat_send_text_json(root);
}

static void ai_chat_send_audio_end(void)
{
    if (ai_chat_is_volc_dialog_endpoint(settings_get_ai_endpoint())) {
        ESP_LOGI(TAG, "dialog send tail TaskRequest");
        ai_chat_send_volc_frame_ex(AI_CHAT_MSG_CLIENT_AUDIO_REQ,
                                   AI_CHAT_FLAG_HAS_EVENT | AI_CHAT_FLAG_TAIL_PACKET,
                                   AI_CHAT_SERIAL_NONE,
                                   AI_CHAT_EVT_TASK_REQUEST,
                                   s_dialog_session_id,
                                   NULL,
                                   0);
        return;
    }

    cJSON *root = cJSON_CreateObject();
    if (root == NULL) {
        return;
    }

    cJSON_AddStringToObject(root, "type", "audio_end");
    cJSON_AddNumberToObject(root, "session_id", s_session_id);
    ai_chat_send_text_json(root);
}

static void ai_chat_begin_recording(void)
{
    if (ai_chat_is_volc_dialog_endpoint(settings_get_ai_endpoint())) {
        ai_chat_reply_ring_reset();
        s_reply_end_received = false;
        s_waiting_for_reply = false;
        s_peak_level = 0;
        s_reply_sample_rate = AI_CHAT_SAMPLE_RATE;
        s_reply_channels = AI_CHAT_CHANNELS;
    } else {
        ai_chat_reset_session_state();
    }

    s_record_stop_requested = false;
    s_logged_upload_chunk_diag = false;
    ESP_LOGI(TAG, "dialog start recording");
    ai_chat_send_session_start();
    ai_chat_set_state(AI_CHAT_STATE_RECORDING_QUERY);
}

static void ai_chat_finish_recording(const char *reason)
{
    s_record_stop_requested = false;
    ESP_LOGI(TAG, "dialog stop recording reason=%s peak=%u",
             reason != NULL ? reason : "manual",
             (unsigned)s_peak_level);
    ai_chat_set_state(AI_CHAT_STATE_UPLOADING);
    ai_chat_send_audio_end();
    s_waiting_for_reply = true;
    ESP_LOGI(TAG, "dialog waiting reply");
    ai_chat_set_state(AI_CHAT_STATE_WAITING_REPLY);
}

static void ai_chat_send_cancel(void)
{
    if (ai_chat_is_volc_dialog_endpoint(settings_get_ai_endpoint())) {
        if (s_dialog_session_ready) {
            ai_chat_send_volc_finish(AI_CHAT_EVT_FINISH_SESSION, NULL);
            s_dialog_session_ready = false;
        }
        if (s_ws_connected) {
            ai_chat_send_volc_finish(AI_CHAT_EVT_FINISH_CONNECTION, NULL);
        }
        return;
    }

    cJSON *root = cJSON_CreateObject();
    if (root == NULL) {
        return;
    }

    cJSON_AddStringToObject(root, "type", "cancel");
    cJSON_AddNumberToObject(root, "session_id", s_session_id);
    ai_chat_send_text_json(root);
}

static void ai_chat_websocket_event(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
{
    (void)base;
    (void)handler_args;

    esp_websocket_event_data_t *data = (esp_websocket_event_data_t *)event_data;

    switch (event_id) {
        case WEBSOCKET_EVENT_CONNECTED:
            s_ws_connected = true;
            ESP_LOGI(TAG, "ws connected");
            if (s_active) {
                s_dialog_connection_ready = false;
                s_dialog_session_ready = false;
                ai_chat_set_state(AI_CHAT_STATE_IDLE);
            }
            break;
        case WEBSOCKET_EVENT_DISCONNECTED:
            s_ws_connected = false;
            ai_chat_reset_ws_rx_state();
            ESP_LOGW(TAG, "ws disconnected");
            if (s_active) {
                ai_chat_set_error((uint32_t)event_id);
            }
            break;
        case WEBSOCKET_EVENT_DATA:
            if (data != NULL && data->data_len > 0) {
                bool is_binary = (data->op_code == 0x2) ||
                                 (data->op_code == 0x0 && s_ws_rx_binary);
                if (data->payload_offset == 0) {
                    if (data->payload_len <= 0 ||
                        ai_chat_reserve_ws_rx_buffer((size_t)data->payload_len) != ESP_OK) {
                        ESP_LOGE(TAG, "ws rx buffer alloc failed: len=%d", data->payload_len);
                        ai_chat_set_error(14);
                        break;
                    }
                    s_ws_rx_payload_len = (size_t)data->payload_len;
                    s_ws_rx_binary = is_binary;
                }

                if (s_ws_rx_buf == NULL ||
                    (size_t)data->payload_offset + (size_t)data->data_len > s_ws_rx_buf_cap ||
                    (size_t)data->payload_offset + (size_t)data->data_len > s_ws_rx_payload_len) {
                    ESP_LOGE(TAG, "ws rx fragment overflow off=%d data=%d total=%d cap=%lu",
                             data->payload_offset, data->data_len, data->payload_len,
                             (unsigned long)s_ws_rx_buf_cap);
                    ai_chat_set_error(15);
                    break;
                }

                memcpy(s_ws_rx_buf + data->payload_offset, data->data_ptr, data->data_len);
                if ((size_t)data->payload_offset + (size_t)data->data_len >= s_ws_rx_payload_len && data->fin) {
                    ai_chat_handle_ws_event((const char *)s_ws_rx_buf, (int)s_ws_rx_payload_len, s_ws_rx_binary);
                    ai_chat_reset_ws_rx_state();
                }
            }
            break;
        case WEBSOCKET_EVENT_ERROR:
            s_ws_connected = false;
            ai_chat_reset_ws_rx_state();
            ESP_LOGE(TAG, "ws event error");
            if (data != NULL) {
                ESP_LOGE(TAG,
                         "ws error type=%d status=%d tls=%d sock_errno=%d",
                         data->error_handle.error_type,
                         data->error_handle.esp_ws_handshake_status_code,
                         data->error_handle.esp_tls_last_esp_err,
                         data->error_handle.esp_transport_sock_errno);
            }
            if (s_active) {
                ai_chat_set_error((uint32_t)event_id);
            }
            break;
        default:
            break;
    }
}

static esp_err_t ai_chat_ensure_ws_connected(void)
{
    const char *endpoint = settings_get_ai_endpoint();
    static char headers[768];

    if (endpoint == NULL || endpoint[0] == '\0') {
        return ESP_ERR_INVALID_STATE;
    }
    if (!ai_chat_endpoint_is_valid(endpoint)) {
        return ESP_ERR_INVALID_ARG;
    }

    if (s_ws != NULL && s_ws_connected) {
        return ESP_OK;
    }

    ai_chat_disconnect_ws();

    headers[0] = '\0';
    if (ai_chat_is_volc_dialog_endpoint(endpoint)) {
        if (s_dialog_connect_id[0] == '\0') {
            ai_chat_make_uuid(s_dialog_connect_id, sizeof(s_dialog_connect_id));
        }
        snprintf(headers, sizeof(headers),
                 "X-Api-App-Id: %s\r\n"
                 "X-Api-Access-Key: %s\r\n"
                 "X-Api-App-Key: %s\r\n"
                 "X-Api-Resource-Id: %s\r\n"
                 "X-Api-Connect-Id: %s\r\n",
                 settings_get_ai_app_id(),
                 settings_get_ai_token(),
                 settings_get_ai_secret_key(),
                 settings_get_ai_resource_id(),
                 s_dialog_connect_id);
        ESP_LOGI(TAG, "ws connect dialog endpoint=%s connect_id=%s", endpoint, s_dialog_connect_id);
    } else if (ai_chat_is_volc_tts_endpoint(endpoint)) {
        char request_id[40];
        ai_chat_make_request_id(request_id, sizeof(request_id));
        snprintf(headers, sizeof(headers),
                "X-Api-App-Id: %s\r\n"
                 "X-Api-Access-Key: %s\r\n"
                 "X-Api-Resource-Id: %s\r\n"
                 "X-Api-Request-Id: %s\r\n"
                 "X-Device-Id: %s\r\n",
                 settings_get_ai_app_id(),
                 settings_get_ai_token(),
                 settings_get_ai_resource_id(),
                 request_id,
                 settings_get_ai_device_id());
    } else if (settings_get_ai_token()[0] != '\0') {
        snprintf(headers, sizeof(headers),
                 "Authorization: Bearer %s\r\nX-Device-Id: %s\r\n",
                 settings_get_ai_token(),
                 settings_get_ai_device_id());
    } else {
        snprintf(headers, sizeof(headers), "X-Device-Id: %s\r\n", settings_get_ai_device_id());
    }

    esp_websocket_client_config_t cfg = {
        .uri = endpoint,
        .headers = headers,
        .buffer_size = AI_CHAT_WS_BUFFER_SIZE,
        .reconnect_timeout_ms = 1000,
        .network_timeout_ms = 10000,
        .crt_bundle_attach = esp_crt_bundle_attach,
    };

    s_ws = esp_websocket_client_init(&cfg);
    if (s_ws == NULL) {
        return ESP_FAIL;
    }

    esp_websocket_register_events(s_ws, WEBSOCKET_EVENT_ANY, ai_chat_websocket_event, NULL);
    if (esp_websocket_client_start(s_ws) != ESP_OK) {
        ai_chat_disconnect_ws();
        return ESP_FAIL;
    }

    for (int i = 0; i < AI_CHAT_CONNECT_RETRY_COUNT; i++) {
        if (!s_active) {
            return ESP_ERR_INVALID_STATE;
        }
        if (s_ws_connected) {
            return ESP_OK;
        }
        vTaskDelay(pdMS_TO_TICKS(AI_CHAT_CONNECT_POLL_MS));
    }

    return ESP_ERR_TIMEOUT;
}

static void ai_chat_apply_reply_format_from_json(cJSON *root)
{
    cJSON *sample_rate = cJSON_GetObjectItem(root, "sample_rate");
    cJSON *channels = cJSON_GetObjectItem(root, "channels");

    if (cJSON_IsNumber(sample_rate) && sample_rate->valueint > 0) {
        s_reply_sample_rate = (uint32_t)sample_rate->valueint;
    }
    if (cJSON_IsNumber(channels) && (channels->valueint == 1 || channels->valueint == 2)) {
        s_reply_channels = (uint8_t)channels->valueint;
    }
}

static void ai_chat_handle_audio_base64(const char *b64)
{
    size_t olen = 0;

    if (b64 == NULL || b64[0] == '\0' || s_b64_decode_buf == NULL) {
        return;
    }

    int ret = mbedtls_base64_decode(s_b64_decode_buf, AI_CHAT_REPLY_RING_SIZE, &olen,
                                    (const unsigned char *)b64, strlen(b64));
    if (ret == 0 && olen > 0) {
        ai_chat_feed_pcm(s_b64_decode_buf, olen);
        ai_chat_set_state(AI_CHAT_STATE_PLAYING_REPLY);
    } else {
        ESP_LOGW(TAG, "base64 audio decode failed: %d", ret);
    }
}

static void ai_chat_handle_json_audio_fields(cJSON *root)
{
    const cJSON *audio_base64 = cJSON_GetObjectItem(root, "audio");
    if (!cJSON_IsString(audio_base64) || audio_base64->valuestring == NULL || audio_base64->valuestring[0] == '\0') {
        audio_base64 = cJSON_GetObjectItem(root, "audio_base64");
    }
    if (!cJSON_IsString(audio_base64) || audio_base64->valuestring == NULL || audio_base64->valuestring[0] == '\0') {
        audio_base64 = cJSON_GetObjectItem(root, "pcm_base64");
    }
    if (cJSON_IsString(audio_base64) && audio_base64->valuestring != NULL && audio_base64->valuestring[0] != '\0') {
        ai_chat_handle_audio_base64(audio_base64->valuestring);
    }
}

static void ai_chat_handle_volc_server_json_event(uint32_t event, cJSON *root)
{
    if (root == NULL) {
        return;
    }

    char *json = cJSON_PrintUnformatted(root);
    ESP_LOGI(TAG, "dialog event=%lu(%s) payload=%s",
             (unsigned long)event,
             ai_chat_volc_event_name(event),
             json != NULL ? json : "{}");
    if (json != NULL) {
        cJSON_free(json);
    }

    switch (event) {
        case AI_CHAT_EVT_CONNECTION_STARTED:
            s_dialog_connection_ready = true;
            ai_chat_set_state(AI_CHAT_STATE_LISTENING_WAKEWORD);
            break;
        case AI_CHAT_EVT_SESSION_STARTED:
            s_dialog_session_ready = true;
            ai_chat_set_state(AI_CHAT_STATE_LISTENING_WAKEWORD);
            break;
        case AI_CHAT_EVT_SESSION_FINISHED:
            s_dialog_session_ready = false;
            ai_chat_set_state(AI_CHAT_STATE_LISTENING_WAKEWORD);
            break;
        case AI_CHAT_EVT_TTS_SENTENCE_START:
            ai_chat_reply_ring_reset();
            s_reply_end_received = false;
            s_waiting_for_reply = true;
            s_reply_sample_rate = AI_CHAT_SAMPLE_RATE;
            s_reply_channels = AI_CHAT_CHANNELS;
            break;
        case AI_CHAT_EVT_TTS_ENDED:
            s_reply_end_received = true;
            if (ai_chat_reply_ring_bytes() == 0) {
                s_waiting_for_reply = false;
                ai_chat_set_state(AI_CHAT_STATE_LISTENING_WAKEWORD);
            }
            break;
        case AI_CHAT_EVT_ASR_ENDED:
        case AI_CHAT_EVT_CHAT_ENDED:
            if (s_state != AI_CHAT_STATE_PLAYING_REPLY) {
                ai_chat_set_state(AI_CHAT_STATE_WAITING_REPLY);
            }
            break;
        case AI_CHAT_EVT_AUDIO_IDLE_TIMEOUT:
            ai_chat_set_error(16);
            break;
        case AI_CHAT_EVT_CONNECTION_FAILED:
        case AI_CHAT_EVT_SESSION_FAILED:
            ai_chat_set_error(11);
            break;
        default:
            break;
    }
}

static void ai_chat_handle_volc_dialog_frame(const uint8_t *data, size_t len)
{
    uint8_t header_size_words;
    uint8_t message_type;
    uint8_t flags;
    uint8_t serialization;
    uint8_t compression;
    size_t offset;
    uint32_t event = 0;
    uint32_t session_len = 0;
    uint32_t payload_len = 0;
    const uint8_t *payload_ptr = NULL;
    const uint8_t *decoded_payload = NULL;
    size_t decoded_len = 0;
    uint8_t *decoded_buf = NULL;
    char incoming_id[40] = {0};

    if (data == NULL || len < 12) {
        return;
    }

    header_size_words = data[0] & 0x0F;
    message_type = data[1] >> 4;
    flags = data[1] & 0x0F;
    serialization = data[2] >> 4;
    compression = data[2] & 0x0F;
    offset = header_size_words * 4U;

    if (offset + 4 > len) {
        return;
    }

    if (message_type == AI_CHAT_MSG_SERVER_ERR) {
        if (offset + 8 <= len) {
            uint32_t err_code = ai_chat_read_u32_be(data + offset);
            uint32_t err_len = ai_chat_read_u32_be(data + offset + 4);
            const uint8_t *err_ptr = data + offset + 8;
            if (offset + 8 + err_len > len) {
                err_len = (uint32_t)(len - offset - 8);
            }
            if (serialization == AI_CHAT_SERIAL_JSON || serialization == AI_CHAT_SERIAL_NONE) {
                ESP_LOGE(TAG, "dialog server error code=%lu detail=%.*s",
                         (unsigned long)err_code,
                         (int)err_len,
                         (const char *)err_ptr);
            } else {
                ESP_LOGE(TAG, "dialog server error code=%lu", (unsigned long)err_code);
            }
        }
        ai_chat_set_error(12);
        return;
    }

    if (message_type != AI_CHAT_MSG_SERVER_FULL_RESP &&
        message_type != AI_CHAT_MSG_SERVER_ACK) {
        return;
    }

    if ((flags & AI_CHAT_FLAG_HAS_SEQ) != 0) {
        if (offset + 4 > len) {
            return;
        }
        offset += 4;
    }

    if ((flags & AI_CHAT_FLAG_HAS_EVENT) != 0) {
        if (offset + 4 > len) {
            return;
        }
        event = ai_chat_read_u32_be(data + offset);
        offset += 4;
    }

    if (offset + 4 > len) {
        return;
    }
    session_len = ai_chat_read_u32_be(data + offset);
    offset += 4;
    if (offset + session_len + 4 > len) {
        return;
    }
    if (session_len > 0 && session_len < sizeof(s_dialog_session_id)) {
        memcpy(incoming_id, data + offset, session_len);
        incoming_id[session_len] = '\0';
    }
    offset += session_len;
    payload_len = ai_chat_read_u32_be(data + offset);
    offset += 4;
    if (offset + payload_len > len) {
        payload_len = (uint32_t)(len - offset);
    }
    payload_ptr = data + offset;

    if (incoming_id[0] != '\0') {
        if (event == AI_CHAT_EVT_CONNECTION_STARTED || event == AI_CHAT_EVT_CONNECTION_FAILED) {
            snprintf(s_dialog_connect_id, sizeof(s_dialog_connect_id), "%s", incoming_id);
        } else {
            snprintf(s_dialog_session_id, sizeof(s_dialog_session_id), "%s", incoming_id);
        }
    }

    if (compression == AI_CHAT_COMPRESS_GZIP) {
        decoded_buf = ai_chat_gzip_decompress_alloc(payload_ptr, payload_len, &decoded_len);
        if (decoded_buf == NULL) {
            ESP_LOGE(TAG, "dialog gzip decode failed evt=%lu(%s) payload_len=%lu",
                     (unsigned long)event,
                     ai_chat_volc_event_name(event),
                     (unsigned long)payload_len);
            ai_chat_set_error(13);
            return;
        }
        decoded_payload = decoded_buf;
    } else if (compression == AI_CHAT_COMPRESS_NONE) {
        decoded_payload = payload_ptr;
        decoded_len = payload_len;
    } else {
        ESP_LOGW(TAG, "dialog compression=%u not supported", (unsigned)compression);
        ai_chat_set_error(13);
        return;
    }

    ESP_LOGI(TAG, "dialog frame msg=%u evt=%lu(%s) serial=%u compress=%u bytes=%lu session=%s",
             (unsigned)message_type,
             (unsigned long)event,
             ai_chat_volc_event_name(event),
             (unsigned)serialization,
             (unsigned)compression,
             (unsigned long)decoded_len,
             incoming_id[0] != '\0' ? incoming_id : "-");

    if (serialization == AI_CHAT_SERIAL_JSON) {
        cJSON *root = cJSON_ParseWithLength((const char *)decoded_payload, decoded_len);
        if (root != NULL) {
            ai_chat_handle_volc_server_json_event(event, root);
            cJSON_Delete(root);
        } else {
            ESP_LOGW(TAG, "dialog json parse failed for event=%lu", (unsigned long)event);
        }
    } else if (serialization == AI_CHAT_SERIAL_NONE) {
        if (event == AI_CHAT_EVT_TTS_RESPONSE && decoded_len > 0) {
            ai_chat_feed_pcm(decoded_payload, decoded_len);
            ai_chat_set_state(AI_CHAT_STATE_PLAYING_REPLY);
        }
    }

    if (decoded_buf != NULL) {
        free(decoded_buf);
    }
}

static void ai_chat_task(void *arg)
{
    (void)arg;

    static int16_t mic_chunk[AI_CHAT_CHUNK_SAMPLES];
    static uint8_t reply_chunk[AI_CHAT_CHUNK_SAMPLES * sizeof(int16_t)];
    bool mic_ready = false;
    bool speaker_ready = false;
    uint32_t record_ms = 0;

    while (1) {
        if (!s_active) {
            if (mic_ready) {
                inmp441_deinit();
                mic_ready = false;
            }
            if (speaker_ready) {
                max98357_deinit();
                speaker_ready = false;
            }
            ai_chat_disconnect_ws();
            ai_chat_reset_session_state();
            ai_chat_reset_runtime_state(NULL, NULL, &record_ms);
            ai_chat_set_state(AI_CHAT_STATE_IDLE);
            vTaskDelay(pdMS_TO_TICKS(50));
            continue;
        }

        if (s_state == AI_CHAT_STATE_ERROR) {
            if (mic_ready) {
                inmp441_deinit();
                mic_ready = false;
            }
            if (speaker_ready) {
                max98357_deinit();
                speaker_ready = false;
            }
            ai_chat_reset_runtime_state(NULL, NULL, &record_ms);
            vTaskDelay(pdMS_TO_TICKS(AI_CHAT_ERROR_RETRY_MS));
            continue;
        }

        if (!wifi_manager_is_connect()) {
            ai_chat_set_error(1);
            vTaskDelay(pdMS_TO_TICKS(AI_CHAT_ERROR_RETRY_MS));
            continue;
        }

        if (ai_chat_ensure_ws_connected() != ESP_OK) {
            ai_chat_set_error(2);
            vTaskDelay(pdMS_TO_TICKS(AI_CHAT_ERROR_RETRY_MS));
            continue;
        }

        if (ai_chat_is_volc_dialog_endpoint(settings_get_ai_endpoint()) &&
            ai_chat_open_volc_dialog_session() != ESP_OK) {
            ai_chat_set_error(10);
            vTaskDelay(pdMS_TO_TICKS(AI_CHAT_ERROR_RETRY_MS));
            continue;
        }

        if (!mic_ready && s_state != AI_CHAT_STATE_PLAYING_REPLY) {
            if (inmp441_init(AI_CHAT_SAMPLE_RATE, AI_CHAT_BITS_PER_SAMPLE) != ESP_OK) {
                ai_chat_set_error(3);
                vTaskDelay(pdMS_TO_TICKS(100));
                continue;
            }
            mic_ready = true;
        }

        if (s_state == AI_CHAT_STATE_PLAYING_REPLY) {
            if (mic_ready) {
                inmp441_deinit();
                mic_ready = false;
                vTaskDelay(pdMS_TO_TICKS(10));
            }
            if (!speaker_ready) {
                if (max98357_init(s_reply_sample_rate, 2, AI_CHAT_BITS_PER_SAMPLE) != ESP_OK) {
                    ai_chat_set_error(4);
                    vTaskDelay(pdMS_TO_TICKS(100));
                    continue;
                }
                speaker_ready = true;
            }

            size_t got = ai_chat_reply_ring_pop(reply_chunk, sizeof(reply_chunk));
            if (got > 0) {
                if (max98357_write(reply_chunk, got, s_reply_channels, pdMS_TO_TICKS(AI_CHAT_PLAY_TIMEOUT_MS)) != ESP_OK) {
                    ai_chat_set_error(5);
                    continue;
                }
            } else if (s_reply_end_received) {
                max98357_deinit();
                speaker_ready = false;
                s_reply_end_received = false;
                s_waiting_for_reply = false;
                ai_chat_set_state(AI_CHAT_STATE_LISTENING_WAKEWORD);
            } else {
                vTaskDelay(pdMS_TO_TICKS(20));
            }
            continue;
        }

        if (speaker_ready) {
            max98357_deinit();
            speaker_ready = false;
            vTaskDelay(pdMS_TO_TICKS(10));
        }

        size_t samples_read = 0;
        if (inmp441_read(mic_chunk, AI_CHAT_CHUNK_SAMPLES, &samples_read, pdMS_TO_TICKS(50)) != ESP_OK ||
            samples_read == 0) {
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }

        s_peak_level = ai_chat_calculate_peak(mic_chunk, samples_read);

        if (s_state == AI_CHAT_STATE_LISTENING_WAKEWORD || s_state == AI_CHAT_STATE_IDLE) {
            if (s_state == AI_CHAT_STATE_IDLE) {
                ai_chat_set_state(AI_CHAT_STATE_LISTENING_WAKEWORD);
            }

            if (s_record_start_requested) {
                s_record_start_requested = false;
                record_ms = 0;
                ai_chat_begin_recording();
            }
            continue;
        }

        if (s_state == AI_CHAT_STATE_RECORDING_QUERY) {
            int send_ret;
            ai_chat_log_upload_chunk_diag(mic_chunk, samples_read);
            if (ai_chat_is_volc_dialog_endpoint(settings_get_ai_endpoint())) {
                send_ret = ai_chat_send_volc_frame(AI_CHAT_MSG_CLIENT_AUDIO_REQ,
                                                   AI_CHAT_SERIAL_NONE,
                                                   AI_CHAT_EVT_TASK_REQUEST,
                                                   s_dialog_session_id,
                                                   (const uint8_t *)mic_chunk,
                                                   samples_read * sizeof(int16_t));
            } else {
                send_ret = esp_websocket_client_send_bin(s_ws, (const char *)mic_chunk,
                                                         samples_read * sizeof(int16_t),
                                                         pdMS_TO_TICKS(AI_CHAT_SEND_TIMEOUT_MS));
            }
            if (send_ret < 0) {
                ai_chat_set_error(6);
                continue;
            }

            record_ms += (uint32_t)((samples_read * 1000U) / AI_CHAT_SAMPLE_RATE);

            if (s_record_stop_requested || record_ms >= AI_CHAT_MAX_RECORD_MS) {
                ai_chat_finish_recording(s_record_stop_requested ? "button" : "max_record");
            }
            continue;
        }

        if (s_state == AI_CHAT_STATE_WAITING_REPLY &&
            (ai_chat_now_ms() - s_state_enter_ms) > AI_CHAT_WAIT_REPLY_TIMEOUT_MS) {
            ai_chat_set_error(8);
        }
    }
}

void ai_chat_init(void)
{
    if (ai_chat_reply_ring_init() != ESP_OK) {
        s_state = AI_CHAT_STATE_ERROR;
        return;
    }

    if (s_ai_task == NULL) {
        xTaskCreate(ai_chat_task, "ai_chat_task", AI_CHAT_TASK_STACK, NULL,
                    AI_CHAT_TASK_PRIORITY, &s_ai_task);
    }
}

void ai_chat_enter_mode(void)
{
    s_last_error = 0;
    ai_chat_reset_session_state();
    max98357_deinit();
    inmp441_deinit();
    s_dialog_connect_id[0] = '\0';
    ai_chat_reset_ws_rx_state();
    if (!ai_chat_is_configured()) {
        ai_chat_set_state(AI_CHAT_STATE_ERROR);
        s_last_error = 0;
    } else {
        ai_chat_set_state(AI_CHAT_STATE_LISTENING_WAKEWORD);
    }
    s_active = true;
}

void ai_chat_exit_mode(void)
{
    ai_chat_send_cancel();
    s_active = false;
    s_reply_end_received = true;
    ai_chat_reset_session_state();
}

void ai_chat_handle_short_press(void)
{
    if (!s_active) {
        return;
    }

    if (s_state == AI_CHAT_STATE_LISTENING_WAKEWORD || s_state == AI_CHAT_STATE_IDLE) {
        s_record_start_requested = true;
        return;
    }

    if (s_state == AI_CHAT_STATE_RECORDING_QUERY) {
        s_record_stop_requested = true;
        return;
    }

    if (s_state == AI_CHAT_STATE_PLAYING_REPLY ||
        s_state == AI_CHAT_STATE_UPLOADING ||
        s_state == AI_CHAT_STATE_WAITING_REPLY) {
        ai_chat_send_cancel();
        ai_chat_reset_session_state();
        ai_chat_set_state(AI_CHAT_STATE_LISTENING_WAKEWORD);
    }
}

void ai_chat_feed_pcm(const uint8_t *pcm_data, size_t pcm_len)
{
    ai_chat_reply_ring_push(pcm_data, pcm_len);
}

void ai_chat_handle_ws_event(const char *payload, int payload_len, bool is_binary)
{
    if (payload == NULL || payload_len <= 0) {
        return;
    }

    if (ai_chat_is_volc_dialog_endpoint(settings_get_ai_endpoint()) && is_binary) {
        ai_chat_handle_volc_dialog_frame((const uint8_t *)payload, (size_t)payload_len);
        return;
    }

    if (is_binary) {
        ai_chat_feed_pcm((const uint8_t *)payload, (size_t)payload_len);
        ai_chat_set_state(AI_CHAT_STATE_PLAYING_REPLY);
        return;
    }

    cJSON *root = cJSON_ParseWithLength(payload, payload_len);
    if (root == NULL) {
        return;
    }

    cJSON *type = cJSON_GetObjectItem(root, "type");
    if (!cJSON_IsString(type)) {
        type = cJSON_GetObjectItem(root, "event");
    }
    const char *type_str = cJSON_GetStringValue(type);

    if (type_str != NULL) {
        if (strcmp(type_str, "tts_start") == 0 ||
            strcmp(type_str, "audio_start") == 0 ||
            strcmp(type_str, "response.audio.start") == 0) {
            ai_chat_apply_reply_format_from_json(root);
            ai_chat_reply_ring_reset();
            s_reply_end_received = false;
            s_waiting_for_reply = true;
            ai_chat_handle_json_audio_fields(root);
            if (ai_chat_reply_ring_bytes() == 0) {
                ai_chat_set_state(AI_CHAT_STATE_WAITING_REPLY);
            }
        } else if (strcmp(type_str, "tts_end") == 0 ||
                   strcmp(type_str, "audio_end") == 0 ||
                   strcmp(type_str, "response.audio.done") == 0) {
            s_reply_end_received = true;
            if (ai_chat_reply_ring_bytes() == 0) {
                s_waiting_for_reply = false;
                ai_chat_set_state(AI_CHAT_STATE_LISTENING_WAKEWORD);
            }
        } else if (strcmp(type_str, "error") == 0) {
            ai_chat_set_error(7);
        } else if (strcmp(type_str, "response.audio.delta") == 0 ||
                   strcmp(type_str, "audio_chunk") == 0 ||
                   strcmp(type_str, "tts_chunk") == 0) {
            ai_chat_apply_reply_format_from_json(root);
            ai_chat_handle_json_audio_fields(root);
        } else if (strcmp(type_str, "session_started") == 0 ||
                   strcmp(type_str, "session.start") == 0) {
            ai_chat_set_state(AI_CHAT_STATE_RECORDING_QUERY);
        }
    }

    ai_chat_handle_json_audio_fields(root);

    cJSON_Delete(root);
}

ai_chat_state_t ai_chat_get_state(void)
{
    return s_state;
}

uint16_t ai_chat_get_peak_level(void)
{
    return s_peak_level;
}

uint32_t ai_chat_get_last_error_code(void)
{
    return s_last_error;
}

bool ai_chat_is_active(void)
{
    return s_active;
}

bool ai_chat_is_configured(void)
{
    if (!ai_chat_endpoint_is_valid(settings_get_ai_endpoint()) ||
        settings_get_ai_device_id()[0] == '\0') {
        return false;
    }

    if (ai_chat_is_volc_dialog_endpoint(settings_get_ai_endpoint())) {
        return settings_get_ai_token()[0] != '\0' &&
               settings_get_ai_secret_key()[0] != '\0' &&
               settings_get_ai_app_id()[0] != '\0' &&
               settings_get_ai_resource_id()[0] != '\0';
    }

    if (ai_chat_is_volc_tts_endpoint(settings_get_ai_endpoint())) {
        return settings_get_ai_token()[0] != '\0' &&
               settings_get_ai_app_id()[0] != '\0' &&
               settings_get_ai_voice_type()[0] != '\0' &&
               settings_get_ai_resource_id()[0] != '\0';
    }

    return true;
}
