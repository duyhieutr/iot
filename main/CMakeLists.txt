idf_component_register(
    SRCS
        "app_main.c"
        "app_driver.c"
    INCLUDE_DIRS
        "." "include"
    REQUIRES
        esp_wifi
        esp_netif
        nvs_flash
        wifi_provisioning
        iot_button
        app_storage
        light_driver
        qrcode
)

target_compile_definitions(${COMPONENT_LIB} PUBLIC
    DEVELOPMENT_BOARD="board_esp32s3_devkitc.h"
)
