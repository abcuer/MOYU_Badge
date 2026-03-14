#include "bsp_delay.h"
#include "esp_rom_sys.h"    // 对应 esp_rom_delay_us
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

void delay_us(uint32_t us)
{
    esp_rom_delay_us(us);
}

void delay_ms(uint32_t ms)
{
    delay_us(ms * 1000);
}