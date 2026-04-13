#include "ws2812.h"
#include "driver/gpio.h"
#include "led_strip.h"

static led_strip_handle_t led_strip;

void ws2812_init(void)
{
    led_strip_config_t strip_config = {
        .strip_gpio_num = WS2812_PIN, // The GPIO that connected to the LED strip's data line
        .max_leds = WS2812_NUM,      // The number of LEDs in the strip,
        .led_model = LED_MODEL_WS2812,        // LED strip model
        .color_component_format = (LED_STRIP_COLOR_COMPONENT_FMT_GRB), // The color order of the strip: GRB
        .flags = {
            .invert_out = false, // don't invert the output signal
        }
    };

    // LED strip backend configuration: RMT
    led_strip_rmt_config_t rmt_config = {
        .clk_src = RMT_CLK_SRC_DEFAULT,        // different clock source can lead to different power consumption
        .resolution_hz = 10*1000*1000, // RMT counter clock frequency
        .mem_block_symbols = 0, // the memory block size used by the RMT channel
        .flags = {
            .with_dma = 0,     // Using DMA can improve performance when driving more LEDs
        }
    };

    // LED Strip object handle

    ESP_ERROR_CHECK(led_strip_new_rmt_device(&strip_config, &rmt_config, &led_strip));
}

void ws2812_flash(uint8_t r, uint8_t g, uint8_t b)
{
    uint8_t scaled_r = (uint8_t)(((uint16_t)r * WS2812_BRIGHTNESS_SCALE) / 255U);
    uint8_t scaled_g = (uint8_t)(((uint16_t)g * WS2812_BRIGHTNESS_SCALE) / 255U);
    uint8_t scaled_b = (uint8_t)(((uint16_t)b * WS2812_BRIGHTNESS_SCALE) / 255U);

    for(uint8_t i = 0; i < WS2812_NUM; i++)
    {
        ESP_ERROR_CHECK(led_strip_set_pixel(led_strip, i, scaled_r, scaled_g, scaled_b));
    }
    ESP_ERROR_CHECK(led_strip_refresh(led_strip));
}

void ws2812_off(void)
{
    if (led_strip == NULL) {
        return;
    }

    ESP_ERROR_CHECK(led_strip_clear(led_strip));
}
