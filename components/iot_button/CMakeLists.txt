idf_component_register(
    SRCS 
        "iot_button.c" 
        "button_gpio.c" 
        "button_adc.c"
    INCLUDE_DIRS 
        "include"
    REQUIRES 
        driver
        esp_adc       # thay esp_adc_cal bằng esp_adc
        esp_timer
)
