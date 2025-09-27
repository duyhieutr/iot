#ifndef __LIGHT_DRIVER_H__
#define __LIGHT_DRIVER_H__

#include "iot_led.h"
#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef  __cplusplus
extern "C" {
#endif

/** Light mode */
typedef enum {
    MODE_NONE                = 0,
    MODE_RGB                 = 1,
    MODE_HSV                 = 2,
    MODE_CTB                 = 3,
    MODE_ON                  = 4,
    MODE_OFF                 = 5,
    MODE_HUE_INCREASE        = 6,
    MODE_HUE_DECREASE        = 7,
    MODE_WARM_INCREASE       = 8,
    MODE_WARM_DECREASE       = 9,
    MODE_BRIGHTNESS_INCREASE = 10,
    MODE_BRIGHTNESS_DECREASE = 11,
} light_mode_t;

/** Light driver configuration */
typedef struct {
    gpio_num_t gpio_red;
    gpio_num_t gpio_green;
    gpio_num_t gpio_blue;
    gpio_num_t gpio_warm;
    gpio_num_t gpio_cold;
    uint32_t fade_period_ms;
    uint32_t blink_period_ms;
    uint32_t freq_hz;
    ledc_timer_t timer_num;
    ledc_mode_t speed_mode;
    ledc_clk_cfg_t clk_cfg;
    ledc_timer_bit_t duty_resolution;
} light_driver_config_t;

/** Initialize light driver */
esp_err_t light_driver_init(light_driver_config_t *config);
esp_err_t light_driver_deinit();
esp_err_t light_driver_config(uint32_t fade_period_ms, uint32_t blink_period_ms);

/** Switch and HSV/CTB control */
esp_err_t light_driver_set_switch(bool status);
esp_err_t light_driver_set_hue(uint16_t hue);
esp_err_t light_driver_set_saturation(uint8_t saturation);
esp_err_t light_driver_set_value(uint8_t value);
esp_err_t light_driver_set_color_temperature(uint8_t color_temperature);
esp_err_t light_driver_set_brightness(uint8_t brightness);
esp_err_t light_driver_set_hsv(uint16_t hue, uint8_t saturation, uint8_t value);
esp_err_t light_driver_set_ctb(uint8_t color_temperature, uint8_t brightness);

uint16_t light_driver_get_hue();
uint8_t light_driver_get_saturation();
uint8_t light_driver_get_value();
uint8_t light_driver_get_color_temperature();
uint8_t light_driver_get_brightness();
bool light_driver_get_switch();
uint8_t light_driver_get_mode();
esp_err_t light_driver_get_hsv(uint16_t *hue, uint8_t *saturation, uint8_t *value);
esp_err_t light_driver_get_ctb(uint8_t *color_temperature, uint8_t *brightness);

/** RGB control */
esp_err_t light_driver_set_rgb(uint8_t red, uint8_t green, uint8_t blue);

/** Blink and breath */
esp_err_t light_driver_breath_start(uint8_t red, uint8_t green, uint8_t blue);
esp_err_t light_driver_breath_stop();
esp_err_t light_driver_blink_start(uint8_t red, uint8_t green, uint8_t blue);
esp_err_t light_driver_blink_stop();

/** Fade functions */
esp_err_t light_driver_fade_brightness(uint8_t brightness);
esp_err_t light_driver_fade_hue(uint16_t hue);
esp_err_t light_driver_fade_warm(uint8_t color_temperature);
esp_err_t light_driver_fade_stop();

/** Convert HSV to RGB */
void light_driver_hsv2rgb(uint16_t hue, uint8_t saturation, uint8_t value,
                          uint8_t *red, uint8_t *green, uint8_t *blue);

#ifdef __cplusplus
}
#endif

#endif /**< __LIGHT_DRIVER_H__ */
