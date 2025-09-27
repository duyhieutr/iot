#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/* Button */
#define LIGHT_BUTTON_GPIO          9     // nút BOOT mặc định
#define LIGHT_BUTTON_ACTIVE_LEVEL  0       // active low

/* LEDs (ví dụ LED đơn hoặc RGB nếu có) */
#define LIGHT_GPIO_RED          3
#define LIGHT_GPIO_GREEN        4
#define LIGHT_GPIO_BLUE         5
#define LIGHT_GPIO_COLD         7
#define LIGHT_GPIO_WARM         10
#define LIGHT_FADE_PERIOD_MS    100     /**< The time from the current state to the next state */
#define LIGHT_BLINK_PERIOD_MS   1500    /**< Period of blinking lights */
#define LIGHT_FREQ_HZ           5000 
#define LEDC_USE_APB_CLK            0
#define LEDC_TIMER_11_BIT           11

#ifdef __cplusplus
}
#endif
