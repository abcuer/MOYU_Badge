#ifndef __SETTINGS_H
#define __SETTINGS_H

#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SETTINGS_DEFAULT_VOLUME 12
#define SETTINGS_DEFAULT_AI_DEVICE_ID "moyu-badge"
#define SETTINGS_DEFAULT_AI_ENDPOINT "wss://openspeech.bytedance.com/api/v3/realtime/dialogue"
#define SETTINGS_DEFAULT_AI_TOKEN "j_WdJUfPzgRZAzIwMKgZBL8n53vrp5PQ"
#define SETTINGS_DEFAULT_AI_SECRET_KEY "PlgvMymc7f3tQnJ6"
#define SETTINGS_DEFAULT_AI_APP_ID "4948964173"
#define SETTINGS_DEFAULT_AI_VOICE_TYPE "zh_female_wanqudashu_moon_bigtts"
#define SETTINGS_DEFAULT_AI_RESOURCE_ID "volc.speech.dialog"

esp_err_t settings_init(void);
uint8_t settings_get_volume(void);
void settings_set_volume(uint8_t volume);
esp_err_t settings_save_volume(void);
const char *settings_get_ai_endpoint(void);
const char *settings_get_ai_device_id(void);
const char *settings_get_ai_token(void);
const char *settings_get_ai_secret_key(void);
const char *settings_get_ai_app_id(void);
const char *settings_get_ai_voice_type(void);
const char *settings_get_ai_resource_id(void);
void settings_set_ai_endpoint(const char *endpoint);
void settings_set_ai_device_id(const char *device_id);
void settings_set_ai_token(const char *token);
void settings_set_ai_secret_key(const char *secret_key);
void settings_set_ai_app_id(const char *app_id);
void settings_set_ai_voice_type(const char *voice_type);
void settings_set_ai_resource_id(const char *resource_id);
esp_err_t settings_save_ai_config(void);

#ifdef __cplusplus
}
#endif

#endif
