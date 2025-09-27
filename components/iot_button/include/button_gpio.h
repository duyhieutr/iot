
#ifndef __IOT_BUTTON_GPIO_H__
#define __IOT_BUTTON_GPIO_H__

#include "driver/gpio.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief gpio button configuration
 * 
 */
typedef struct {
    int32_t gpio_num;
    uint8_t active_level;
} button_gpio_config_t;

/**
 * @brief Initialize gpio button
 * 
 * @param config pointer of configuration struct
 * 
 * @return
 *      - ESP_OK on success
 *      - ESP_ERR_INVALID_ARG   Arguments is NULL.
 */
esp_err_t button_gpio_init(const button_gpio_config_t *config);

/**
 * @brief Deinitialize gpio button
 * 
 * @param gpio_num gpio number of button
 * 
 * @return Always return ESP_OK
 */
esp_err_t button_gpio_deinit(int gpio_num);

/**
 * @brief Get current level on button gpio
 * 
 * @param gpio_num gpio number of button, it will be treated as a uint32_t variable.
 * 
 * @return Level on gpio
 */
uint8_t button_gpio_get_key_level(void *gpio_num);

#ifdef __cplusplus
}
#endif

#endif /**< __IOT_BUTTON_GPIO_H__ */
