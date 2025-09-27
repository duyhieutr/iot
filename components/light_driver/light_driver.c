#include <stdio.h>
#include <string.h>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/timers.h"

#include "light_driver.h"
#include "iot_led.h"

#define TAG "light_driver"

#ifndef MAX
#define MAX(a, b) ((a) > (b) ? (a) : (b))
#endif
#ifndef MIN
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#endif

typedef struct {
    uint8_t mode;
    uint8_t on;
    uint16_t hue;
    uint8_t saturation;
    uint8_t value;
    uint8_t color_temperature;
    uint8_t brightness;
    uint32_t fade_period_ms;
    uint32_t blink_period_ms;
} light_status_t;

enum light_channel {
    CHANNEL_ID_RED = 0,
    CHANNEL_ID_GREEN,
    CHANNEL_ID_BLUE,
    CHANNEL_ID_WARM,
    CHANNEL_ID_COLD,
};

static light_status_t g_light_status = {0};
static light_driver_config_t g_driver_config;
static TimerHandle_t g_fade_timer = NULL;
static int g_fade_mode = MODE_NONE;
static uint16_t g_fade_hue = 0;

/* Forward declarations */
static void light_fade_timer_stop(void);

/* HSV to RGB conversion */
void light_driver_hsv2rgb(uint16_t hue, uint8_t saturation, uint8_t value,
                          uint8_t *red, uint8_t *green, uint8_t *blue)
{
    float h = hue / 60.0f;
    float s = saturation / 100.0f;
    float v = value / 100.0f;

    int i = (int)h % 6;
    float f = h - i;
    float p = v * (1 - s);
    float q = v * (1 - f * s);
    float t = v * (1 - (1 - f) * s);

    float r, g, b;
    switch(i) {
        case 0: r=v; g=t; b=p; break;
        case 1: r=q; g=v; b=p; break;
        case 2: r=p; g=v; b=t; break;
        case 3: r=p; g=q; b=v; break;
        case 4: r=t; g=p; b=v; break;
        case 5: r=v; g=p; b=q; break;
        default: r=g=b=0; break;
    }

    *red   = (uint8_t)(r * 255);
    *green = (uint8_t)(g * 255);
    *blue  = (uint8_t)(b * 255);
}

/* Fade timer callback */
static void light_fade_timer_cb(TimerHandle_t xTimer)
{
    (void)xTimer;
    uint8_t r, g, b;
    uint32_t fade_period_ms = g_light_status.fade_period_ms;

    int variety = (g_fade_hue > 180) ? 10 : -10;
    g_light_status.hue = (g_light_status.hue + variety + 360) % 360;

    light_driver_hsv2rgb(g_light_status.hue,
                         g_light_status.saturation,
                         g_light_status.value,
                         &r, &g, &b);

    iot_led_set_channel(CHANNEL_ID_RED, r, fade_period_ms);
    iot_led_set_channel(CHANNEL_ID_GREEN, g, fade_period_ms);
    iot_led_set_channel(CHANNEL_ID_BLUE, b, fade_period_ms);

    ESP_LOGD(TAG, "Fade hue=%d RGB=(%d,%d,%d)", g_light_status.hue, r, g, b);
}

/* Stop fade timer */
static void light_fade_timer_stop(void)
{
    if (g_fade_timer) {
        xTimerStop(g_fade_timer, 0);
        xTimerDelete(g_fade_timer, 0);
        g_fade_timer = NULL;
        ESP_LOGI(TAG, "Fade timer stopped");
    }
}

/* Initialize light driver */
esp_err_t light_driver_init(light_driver_config_t *config)
{
    if (!config) return ESP_ERR_INVALID_ARG;
    memcpy(&g_driver_config, config, sizeof(light_driver_config_t));

    g_light_status.mode = MODE_NONE;
    g_light_status.on = 0;
    g_light_status.hue = 0;
    g_light_status.saturation = 100;
    g_light_status.value = 100;
    g_light_status.fade_period_ms = config->fade_period_ms;
    g_light_status.blink_period_ms = config->blink_period_ms;

    /* Initialize LED driver */
    esp_err_t ret = iot_led_init(config->timer_num,
                                 config->speed_mode,
                                 config->freq_hz,
                                 config->clk_cfg,
                                 config->duty_resolution);
    if (ret != ESP_OK) return ret;

    /* Register LED channels */
    iot_led_regist_channel(CHANNEL_ID_RED, config->gpio_red);
    iot_led_regist_channel(CHANNEL_ID_GREEN, config->gpio_green);
    iot_led_regist_channel(CHANNEL_ID_BLUE, config->gpio_blue);
    iot_led_regist_channel(CHANNEL_ID_WARM, config->gpio_warm);
    iot_led_regist_channel(CHANNEL_ID_COLD, config->gpio_cold);

    ESP_LOGI(TAG, "Light driver initialized");
    return ESP_OK;
}

/* Set switch */
esp_err_t light_driver_set_switch(bool status)
{
    g_light_status.on = status;
    uint8_t val = status ? 255 : 0;
    iot_led_set_channel(CHANNEL_ID_RED, val, 0);
    iot_led_set_channel(CHANNEL_ID_GREEN, val, 0);
    iot_led_set_channel(CHANNEL_ID_BLUE, val, 0);
    iot_led_set_channel(CHANNEL_ID_WARM, val, 0);
    iot_led_set_channel(CHANNEL_ID_COLD, val, 0);
    return ESP_OK;
}

/* Fade hue */
esp_err_t light_driver_fade_hue(uint16_t hue)
{
    g_fade_mode = MODE_HSV;
    g_fade_hue = hue;

    light_fade_timer_stop();

    g_light_status.mode = MODE_HSV;
    if (g_light_status.value == 0) g_light_status.value = 100;

    uint8_t r, g, b;
    light_driver_hsv2rgb(hue, g_light_status.saturation, g_light_status.value,
                         &r, &g, &b);
    iot_led_set_channel(CHANNEL_ID_RED, r, 0);
    iot_led_set_channel(CHANNEL_ID_GREEN, g, 0);
    iot_led_set_channel(CHANNEL_ID_BLUE, b, 0);

    g_fade_timer = xTimerCreate("fade_timer",
                                pdMS_TO_TICKS(g_light_status.fade_period_ms),
                                pdTRUE,
                                NULL,
                                light_fade_timer_cb);
    if (!g_fade_timer) return ESP_FAIL;

    xTimerStart(g_fade_timer, 0);
    ESP_LOGI(TAG, "Fade hue started, target hue=%d", hue);
    return ESP_OK;
}
