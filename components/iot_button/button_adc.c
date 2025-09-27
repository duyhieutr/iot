#include <string.h>
#include <stdlib.h>
#include "esp_log.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_timer.h"
#include "button_adc.h"

static const char *TAG = "adc_button";

#define ADC_BTN_CHECK(a, str, ret_val) \
    if (!(a)) { \
        ESP_LOGE(TAG, "%s(%d): %s", __FUNCTION__, __LINE__, str); \
        return (ret_val); \
    }

#define NO_OF_SAMPLES   CONFIG_ADC_BUTTON_SAMPLE_TIMES
#define ADC_BUTTON_MAX_CHANNEL CONFIG_ADC_BUTTON_MAX_CHANNEL
#define ADC_BUTTON_MAX_BUTTON  CONFIG_ADC_BUTTON_MAX_BUTTON_PER_CHANNEL
#define ADC_DEBOUNCE_US 10000  // 10ms

typedef struct {
    uint16_t min;
    uint16_t max;
} button_data_t;

typedef struct {
    adc_channel_t channel;
    uint8_t is_init;
    button_data_t btns[ADC_BUTTON_MAX_BUTTON];
    uint64_t last_time;
    uint16_t last_voltage;
} btn_adc_channel_t;

typedef struct {
    bool is_configured;
    adc_oneshot_unit_handle_t unit_handle;
    adc_cali_handle_t cali_handle;
    btn_adc_channel_t ch[ADC_BUTTON_MAX_CHANNEL];
    uint8_t ch_num;
} adc_button_t;

static adc_button_t g_button = {0};

static int find_unused_channel(void) {
    for (size_t i = 0; i < ADC_BUTTON_MAX_CHANNEL; i++) {
        if (!g_button.ch[i].is_init) return i;
    }
    return -1;
}

static int find_channel(adc_channel_t channel) {
    for (size_t i = 0; i < ADC_BUTTON_MAX_CHANNEL; i++) {
        if (g_button.ch[i].channel == channel) return i;
    }
    return -1;
}

esp_err_t button_adc_init(const button_adc_config_t *config) {
    ADC_BTN_CHECK(config, "config invalid", ESP_ERR_INVALID_ARG);

    int ch_index = find_channel(config->adc_channel);
    if (ch_index >= 0) {
        ADC_BTN_CHECK(g_button.ch[ch_index].btns[config->button_index].max == 0,
                      "button_index already used", ESP_ERR_INVALID_STATE);
    } else {
        int unused = find_unused_channel();
        ADC_BTN_CHECK(unused >= 0, "exceed max channel", ESP_ERR_INVALID_STATE);
        ch_index = unused;
    }

    if (!g_button.is_configured) {
        adc_oneshot_unit_init_cfg_t init_cfg = {
            .unit_id = ADC_UNIT_1,
        };
        ESP_ERROR_CHECK(adc_oneshot_new_unit(&init_cfg, &g_button.unit_handle));

        adc_cali_curve_fitting_config_t cali_cfg = {
            .unit_id = ADC_UNIT_1,
            .atten = ADC_ATTEN_DB_12,
        };
        if (adc_cali_create_scheme_curve_fitting(&cali_cfg, &g_button.cali_handle) == ESP_OK) {
            ESP_LOGI(TAG, "ADC calibration done");
        } else {
            ESP_LOGW(TAG, "ADC calibration failed");
            g_button.cali_handle = NULL;
        }

        g_button.is_configured = true;
    }

    if (!g_button.ch[ch_index].is_init) {
        adc_oneshot_chan_cfg_t chan_cfg = {
            .atten = ADC_ATTEN_DB_12,
            .bitwidth = ADC_BITWIDTH_DEFAULT,
        };
        ESP_ERROR_CHECK(adc_oneshot_config_channel(g_button.unit_handle,
                                                   config->adc_channel, &chan_cfg));
        g_button.ch[ch_index].channel = config->adc_channel;
        g_button.ch[ch_index].is_init = 1;
        g_button.ch[ch_index].last_time = 0;
        g_button.ch[ch_index].last_voltage = 0;
    }

    g_button.ch[ch_index].btns[config->button_index].max = config->max;
    g_button.ch[ch_index].btns[config->button_index].min = config->min;
    g_button.ch_num++;

    return ESP_OK;
}

esp_err_t button_adc_deinit(adc1_channel_t channel, int button_index) {
    int ch_index = find_channel(channel);
    ADC_BTN_CHECK(ch_index >= 0, "channel not found", ESP_ERR_INVALID_ARG);

    btn_adc_channel_t *ch_data = &g_button.ch[ch_index];
    ch_data->btns[button_index].max = 0;
    ch_data->btns[button_index].min = 0;

    // Nếu không còn button nào trên channel
    bool empty = true;
    for (int i = 0; i < ADC_BUTTON_MAX_BUTTON; i++) {
        if (ch_data->btns[i].max != 0) {
            empty = false;
            break;
        }
    }
    if (empty) {
        ch_data->is_init = 0;
        g_button.ch_num--;
        // Không xóa unit/cali để tránh xung đột nếu còn channel khác
    }
    return ESP_OK;
}

static uint32_t get_adc_voltage(adc_channel_t channel) {
    int raw = 0, voltage = 0, sum = 0;
    for (int i = 0; i < NO_OF_SAMPLES; i++) {
        adc_oneshot_read(g_button.unit_handle, channel, &raw);
        sum += raw;
    }
    raw = sum / NO_OF_SAMPLES;

    if (g_button.cali_handle) {
        adc_cali_raw_to_voltage(g_button.cali_handle, raw, &voltage);
    } else {
        voltage = raw;
    }
    ESP_LOGV(TAG, "Raw: %d Voltage: %dmV", raw, voltage);
    return voltage;
}

uint8_t button_adc_get_key_level(void *button_index) {
    uint32_t ch = ADC_BUTTON_SPLIT_CHANNEL(button_index);
    uint32_t index = ADC_BUTTON_SPLIT_INDEX(button_index);
    int ch_index = find_channel(ch);
    ADC_BTN_CHECK(ch_index >= 0, "channel not found", 0);

    btn_adc_channel_t *ch_data = &g_button.ch[ch_index];

    if ((esp_timer_get_time() - ch_data->last_time) > ADC_DEBOUNCE_US) {
        ch_data->last_voltage = get_adc_voltage(ch);
        ch_data->last_time = esp_timer_get_time();
    }

    if (ch_data->last_voltage <= ch_data->btns[index].max &&
        ch_data->last_voltage > ch_data->btns[index].min) {
        return 1;
    }
    return 0;
}
