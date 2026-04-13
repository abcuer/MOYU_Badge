#ifndef _APCFG_H_
#define _APCFG_H_

#include <stdbool.h>
#include <stddef.h>

#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "wifi_manager.h"

#define AP_WIFI_TAG                      "apcfg"
#define AP_WIFI_INDEX_HTML_PATH          "/spiffs/apcfg.html"
#define AP_WIFI_CONFIG_BIT               BIT0
#define AP_WIFI_DNS_PORT                 53
#define AP_WIFI_DNS_MAX_PACKET_SIZE      512
#define AP_WIFI_SAVED_CONNECT_TIMEOUT_MS 40000
#define AP_WIFI_NVS_NAMESPACE            "storage"
#define AP_WIFI_NVS_KEY_SSID             "ssid"
#define AP_WIFI_NVS_KEY_PASSWORD         "password"

/** wifi功能和ap配网功能初始化
 * @param f wifi连接状态回调函数
 * @return 无 
*/
void ap_wifi_init(p_wifi_state_callback f);

/** 连接某个热点
 * @param ssid
 * @param password
 * @return 无 
*/
void ap_wifi_set(const char* ssid,const char* password);

/** 启动配网模式
 * @param enable 暂无用，强制true
 * @return 无 
*/
void ap_wifi_apcfg(bool enable);

void save_wifi_to_nvs(const char* ssid, const char* password);
void erase_wifi_from_nvs(void);
void ap_wifi_go(void);
bool ap_wifi_is_config_mode_active(void);

#endif
