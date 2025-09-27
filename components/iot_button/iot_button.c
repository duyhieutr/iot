#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "esp_log.h"
#include "esp_timer.h"
#include "driver/gpio.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "iot_button.h"
#include "button_gpio.h"
#include "button_adc.h"

static const char *TAG = "iot_button";

#define BTN_CHECK(a, str, ret_val) \
    if (!(a)) { \
        ESP_LOGE(TAG, "%s(%d): %s", __FUNCTION__, __LINE__, str); \
        return (ret_val); \
    }

#define TICKS_INTERVAL CONFIG_BUTTON_PERIOD_TIME_MS
#define SHORT_TICKS   (CONFIG_BUTTON_SHORT_PRESS_TIME_MS / TICKS_INTERVAL)
#define LONG_TICKS    (CONFIG_BUTTON_LONG_PRESS_TIME_MS / TICKS_INTERVAL)

typedef struct button_dev {
    button_type_t type;
    uint8_t active_level;
    uint8_t button_level;
    uint16_t ticks;        // tăng từ uint8_t -> uint16_t để tránh overflow
    uint8_t repeat;        // chỉ cần uint8_t
    button_event_t event;
    uint8_t (*hal_button_Level)(void *usr_data);
    void *usr_data;
    button_cb_t cb[BUTTON_EVENT_MAX];
    struct button_dev *next;
} button_dev_t;

static button_dev_t *g_head_handle = NULL;
static esp_timer_handle_t g_button_timer_handle = NULL;
static bool g_is_timer_running = false;

// Forward declaration
static void button_cb(void *arg);

static void call_event_cb(button_dev_t *btn, button_event_t event)
{
    if (btn->cb[event]) {
        btn->cb[event](btn);
    }
}

// State machine to handle button press, release, long press, repeat
static void button_handler(button_dev_t *btn)
{
    uint8_t level = btn->hal_button_Level(btn->usr_data);

    if (btn->button_level != level) {
        btn->button_level = level;
        if (level == btn->active_level) {
            btn->ticks = 0;
            btn->repeat++;
            btn->event = BUTTON_PRESS_DOWN;
            call_event_cb(btn, BUTTON_PRESS_DOWN);
        } else {
            if (btn->ticks < SHORT_TICKS) {
                btn->event = (btn->repeat == 1) ? BUTTON_SINGLE_CLICK :
                             (btn->repeat == 2) ? BUTTON_DOUBLE_CLICK : BUTTON_NONE_PRESS;
                call_event_cb(btn, btn->event);
            }
            btn->ticks = 0;
            btn->event = BUTTON_PRESS_UP;
            call_event_cb(btn, BUTTON_PRESS_UP);
        }
    } else {
        if (level == btn->active_level) {
            btn->ticks++;
            if (btn->ticks == LONG_TICKS) {
                btn->event = BUTTON_LONG_PRESS_START;
                call_event_cb(btn, BUTTON_LONG_PRESS_START);
            } else if (btn->ticks > LONG_TICKS) {
                btn->event = BUTTON_LONG_PRESS_HOLD;
                call_event_cb(btn, BUTTON_LONG_PRESS_HOLD);
            }
        }
    }
}

static void button_cb(void *arg)
{
    button_dev_t *btn = g_head_handle;
    while (btn) {
        button_handler(btn);
        btn = btn->next;
    }
}

static button_dev_t *button_create_com(uint8_t active_level,
                                       uint8_t (*hal_get_key_state)(void *usr_data),
                                       void *usr_data)
{
    BTN_CHECK(hal_get_key_state != NULL, "hal_get_key_state invalid", NULL);

    button_dev_t *btn = calloc(1, sizeof(button_dev_t));
    BTN_CHECK(btn != NULL, "malloc failed", NULL);

    btn->active_level = active_level;
    btn->button_level = !active_level;
    btn->hal_button_Level = hal_get_key_state;
    btn->usr_data = usr_data;
    btn->event = BUTTON_NONE_PRESS;

    btn->next = g_head_handle;
    g_head_handle = btn;

    if (!g_is_timer_running) {
        esp_timer_create_args_t timer_args = {
            .callback = button_cb,
            .arg = NULL,
            .dispatch_method = ESP_TIMER_TASK,
            .name = "button_timer"
        };
        ESP_ERROR_CHECK(esp_timer_create(&timer_args, &g_button_timer_handle));
        ESP_ERROR_CHECK(esp_timer_start_periodic(g_button_timer_handle, TICKS_INTERVAL * 1000U));
        g_is_timer_running = true;
    }

    return btn;
}

esp_err_t iot_button_register_cb(button_handle_t handle, button_event_t event, button_cb_t cb)
{
    BTN_CHECK(handle != NULL, "handle NULL", ESP_ERR_INVALID_ARG);
    BTN_CHECK(event < BUTTON_EVENT_MAX, "invalid event", ESP_ERR_INVALID_ARG);

    button_dev_t *btn = (button_dev_t *)handle;
    btn->cb[event] = cb;
    return ESP_OK;
}

esp_err_t iot_button_unregister_cb(button_handle_t handle, button_event_t event)
{
    BTN_CHECK(handle != NULL, "handle NULL", ESP_ERR_INVALID_ARG);
    BTN_CHECK(event < BUTTON_EVENT_MAX, "invalid event", ESP_ERR_INVALID_ARG);

    button_dev_t *btn = (button_dev_t *)handle;
    btn->cb[event] = NULL;
    return ESP_OK;
}

button_handle_t iot_button_create(const button_config_t *config)
{
    BTN_CHECK(config != NULL, "config NULL", NULL);
    button_dev_t *btn = NULL;
    esp_err_t ret;

    switch (config->type) {
    case BUTTON_TYPE_GPIO: {
        const button_gpio_config_t *cfg = &config->gpio_button_config;
        ret = button_gpio_init(cfg);
        BTN_CHECK(ret == ESP_OK, "gpio init failed", NULL);
        btn = button_create_com(cfg->active_level, button_gpio_get_key_level,
                                (void *)(intptr_t)cfg->gpio_num);
    } break;

    case BUTTON_TYPE_ADC: {
        const button_adc_config_t *cfg = &config->adc_button_config;
        ret = button_adc_init(cfg);
        BTN_CHECK(ret == ESP_OK, "adc init failed", NULL);
        btn = button_create_com(1, button_adc_get_key_level,
                                (void *)(intptr_t)ADC_BUTTON_COMBINE(cfg->adc_channel, cfg->button_index));
    } break;

    default:
        ESP_LOGE(TAG, "unsupported type");
        return NULL;
    }

    btn->type = config->type;
    return (button_handle_t)btn;
}

esp_err_t iot_button_delete(button_handle_t handle)
{
    BTN_CHECK(handle != NULL, "handle NULL", ESP_ERR_INVALID_ARG);
    button_dev_t *btn = (button_dev_t *)handle;
    esp_err_t ret = ESP_OK;

    // Deinit underlying driver
    if (btn->type == BUTTON_TYPE_GPIO) {
        ret = button_gpio_deinit((int)(intptr_t)btn->usr_data);
    } else if (btn->type == BUTTON_TYPE_ADC) {
        int combined = (int)(intptr_t)btn->usr_data;
        adc1_channel_t channel = ADC_BUTTON_SPLIT_CHANNEL(combined);
        int button_index = ADC_BUTTON_SPLIT_INDEX(combined);
        ret = button_adc_deinit(channel, button_index);
    }

    // Remove from list
    button_dev_t **curr = &g_head_handle;
    while (*curr) {
        if (*curr == btn) {
            *curr = btn->next;
            free(btn);
            break;
        }
        curr = &(*curr)->next;
    }

    // Stop timer if no button left
    if (g_head_handle == NULL && g_is_timer_running) {
        esp_timer_stop(g_button_timer_handle);
        esp_timer_delete(g_button_timer_handle);
        g_is_timer_running = false;
    }

    return ret;
}

button_event_t iot_button_get_event(button_handle_t handle)
{
    BTN_CHECK(handle != NULL, "handle NULL", BUTTON_NONE_PRESS);
    return ((button_dev_t *)handle)->event;
}

uint8_t iot_button_get_repeat(button_handle_t handle)
{
    BTN_CHECK(handle != NULL, "handle NULL", 0);
    return ((button_dev_t *)handle)->repeat;
}
