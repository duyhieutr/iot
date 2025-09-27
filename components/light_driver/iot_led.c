#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include "esp_log.h"
#include "driver/ledc.h"
#include "driver/gptimer.h"
#include "iot_led.h"

#define LEDC_FADE_MARGIN     (10)
#define LEDC_TIMER_PRECISION LEDC_TIMER_13_BIT
#define LEDC_VALUE_TO_DUTY(value) ((value) * ((1 << LEDC_TIMER_PRECISION)) / (UINT16_MAX))
#define LEDC_FIXED_Q         (8)
#define FLOATINT_2_FIXED(X, Q) ((int)((X) * (0x1U << (Q))))
#define FIXED_2_FLOATING(X, Q) ((int)((X) / (0x1U << (Q))))
#define GET_FIXED_INTEGER_PART(X, Q) ((X) >> (Q))
#define GET_FIXED_DECIMAL_PART(X, Q) ((X) & ((0x1U << (Q)) - 1))

typedef struct {
    int cur;
    int final;
    int step;
    int cycle;
    size_t num;
} ledc_fade_data_t;

typedef struct {
    ledc_fade_data_t fade_data[LEDC_CHANNEL_MAX];
    ledc_mode_t speed_mode;
    ledc_timer_t timer_num;
    gptimer_handle_t fade_timer;
    bool timer_started;
} iot_light_t;

static const char *TAG = "iot_light";
static iot_light_t *g_light_config = NULL;
static uint16_t *g_gamma_table = NULL;

static void gamma_table_create(uint16_t *gamma_table, float correction)
{
    for (int i = 0; i < GAMMA_TABLE_SIZE; i++) {
        float value_tmp = (float)i / (GAMMA_TABLE_SIZE - 1);
        value_tmp = powf(value_tmp, 1.0f / correction);
        gamma_table[i] = (uint16_t)FLOATINT_2_FIXED((value_tmp * GAMMA_TABLE_SIZE), LEDC_FIXED_Q);
    }

    if (gamma_table[255] == 0) {
        gamma_table[255] = UINT16_MAX;
    }
}

static inline uint32_t gamma_value_to_duty(int value)
{
    uint32_t tmp_q = GET_FIXED_INTEGER_PART(value, LEDC_FIXED_Q);
    uint32_t tmp_r = GET_FIXED_DECIMAL_PART(value, LEDC_FIXED_Q);

    uint16_t cur = LEDC_VALUE_TO_DUTY(g_gamma_table[tmp_q]);
    uint16_t next = tmp_q < (GAMMA_TABLE_SIZE - 1) ? LEDC_VALUE_TO_DUTY(g_gamma_table[tmp_q + 1]) : cur;
    return cur + (next - cur) * tmp_r / (0x1U << LEDC_FIXED_Q);
}

static bool IRAM_ATTR fade_timer_cb(gptimer_handle_t timer, const gptimer_alarm_event_data_t *edata, void *user_ctx)
{
    int idle_channel_num = 0;

    for (int channel = 0; channel < LEDC_CHANNEL_MAX; channel++) {
        ledc_fade_data_t *fade_data = g_light_config->fade_data + channel;

        if (fade_data->num > 0) {
            fade_data->num--;

            fade_data->cur += fade_data->step;
            uint32_t duty = gamma_value_to_duty(fade_data->cur);

            ledc_set_duty(g_light_config->speed_mode, channel, duty);
            ledc_update_duty(g_light_config->speed_mode, channel);

        } else if (fade_data->cycle) {
            fade_data->num = fade_data->cycle - 1;
            fade_data->step *= -1;
            fade_data->cur += fade_data->step;

            uint32_t duty = gamma_value_to_duty(fade_data->cur);
            ledc_set_duty(g_light_config->speed_mode, channel, duty);
            ledc_update_duty(g_light_config->speed_mode, channel);

        } else {
            idle_channel_num++;
        }
    }

    if (idle_channel_num >= LEDC_CHANNEL_MAX) {
        gptimer_stop(g_light_config->fade_timer);
        g_light_config->timer_started = false;
    }

    return true;
}

/* Public APIs */

esp_err_t iot_led_init(ledc_timer_t timer_num, ledc_mode_t speed_mode, uint32_t freq_hz,
                       ledc_clk_cfg_t clk_cfg, ledc_timer_bit_t duty_resolution)
{
    esp_err_t ret;
    ledc_timer_config_t ledc_time_config = {
        .speed_mode      = speed_mode,
        .duty_resolution = duty_resolution,
        .timer_num       = timer_num,
        .freq_hz         = freq_hz,
        .clk_cfg         = clk_cfg,
    };

    ret = ledc_timer_config(&ledc_time_config);
    if (ret != ESP_OK) return ret;

    if (g_gamma_table == NULL) {
        g_gamma_table = calloc(GAMMA_TABLE_SIZE + 1, sizeof(uint16_t));
        gamma_table_create(g_gamma_table, GAMMA_CORRECTION);
    }

    if (g_light_config == NULL) {
        g_light_config = calloc(1, sizeof(iot_light_t));
        g_light_config->timer_num = timer_num;
        g_light_config->speed_mode = speed_mode;
        g_light_config->timer_started = false;

        gptimer_config_t config = {
            .clk_src = GPTIMER_CLK_SRC_DEFAULT,
            .direction = GPTIMER_COUNT_UP,
            .resolution_hz = 1000000, // 1us
        };
        gptimer_new_timer(&config, &g_light_config->fade_timer);

        gptimer_event_callbacks_t cbs = {
            .on_alarm = fade_timer_cb,
        };
        gptimer_register_event_callbacks(g_light_config->fade_timer, &cbs, NULL);

        gptimer_alarm_config_t alarm_config = {
            .alarm_count = DUTY_SET_CYCLE * 1000, // ms → us
            .reload_count = 0,
            .flags.auto_reload_on_alarm = true,
        };
        gptimer_set_alarm_action(g_light_config->fade_timer, &alarm_config);
        gptimer_enable(g_light_config->fade_timer);
    }

    return ESP_OK;
}

esp_err_t iot_led_deinit()
{
    if (g_gamma_table) free(g_gamma_table);
    if (g_light_config) {
        if (g_light_config->fade_timer) {
            gptimer_disable(g_light_config->fade_timer);
            gptimer_del_timer(g_light_config->fade_timer);
        }
        free(g_light_config);
    }
    return ESP_OK;
}

esp_err_t iot_led_regist_channel(ledc_channel_t channel, gpio_num_t gpio_num)
{
    const ledc_channel_config_t ledc_ch_config = {
        .gpio_num   = gpio_num,
        .channel    = channel,
        .intr_type  = LEDC_INTR_DISABLE,
        .speed_mode = g_light_config->speed_mode,
        .timer_sel  = g_light_config->timer_num,
    };

    return ledc_channel_config(&ledc_ch_config);
}

esp_err_t iot_led_get_channel(ledc_channel_t channel, uint8_t *dst)
{
    if (!dst) return ESP_ERR_INVALID_ARG;
    int cur = g_light_config->fade_data[channel].cur;
    *dst = FIXED_2_FLOATING(cur, LEDC_FIXED_Q);
    return ESP_OK;
}

esp_err_t iot_led_set_channel(ledc_channel_t channel, uint8_t value, uint32_t fade_ms)
{
    ledc_fade_data_t *fade_data = g_light_config->fade_data + channel;

    fade_data->final = FLOATINT_2_FIXED(value, LEDC_FIXED_Q);
    fade_data->num = (fade_ms < DUTY_SET_CYCLE) ? 1 : fade_ms / DUTY_SET_CYCLE;
    fade_data->step = abs(fade_data->cur - fade_data->final) / fade_data->num;
    if (fade_data->cur > fade_data->final) fade_data->step *= -1;
    fade_data->cycle = 0;

    if (!g_light_config->timer_started) {
        gptimer_start(g_light_config->fade_timer);
        g_light_config->timer_started = true;
    }

    return ESP_OK;
}

esp_err_t iot_led_start_blink(ledc_channel_t channel, uint8_t value, uint32_t period_ms, bool fade_flag)
{
    ledc_fade_data_t *fade_data = g_light_config->fade_data + channel;

    fade_data->final = fade_data->cur = FLOATINT_2_FIXED(value, LEDC_FIXED_Q);
    fade_data->cycle = period_ms / 2 / DUTY_SET_CYCLE;
    fade_data->num   = (fade_flag) ? fade_data->cycle : 0;
    fade_data->step  = (fade_flag) ? fade_data->cur / fade_data->num * -1 : 0;

    if (!g_light_config->timer_started) {
        gptimer_start(g_light_config->fade_timer);
        g_light_config->timer_started = true;
    }
    return ESP_OK;
}

esp_err_t iot_led_stop_blink(ledc_channel_t channel)
{
    ledc_fade_data_t *fade_data = g_light_config->fade_data + channel;
    fade_data->cycle = fade_data->num = 0;
    return ESP_OK;
}

esp_err_t iot_led_set_gamma_table(const uint16_t gamma_table[GAMMA_TABLE_SIZE])
{
    memcpy(g_gamma_table, gamma_table, GAMMA_TABLE_SIZE * sizeof(uint16_t));
    return ESP_OK;
}
