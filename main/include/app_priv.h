// Copyright 2020 Espressif Systems (Shanghai) Co. Ltd.

#ifndef __APP_PRIVATE_H__
#define __APP_PRIVATE_H__

/**
 * @brief 
 * 
 */
void app_driver_init(void);

/**
 * @brief 
 * 
 * @param state 
 * @return int 
 */
int app_driver_set_state(bool state);

/**
 * @brief 
 * 
 * @return true 
 * @return false 
 */
bool app_driver_get_state(void);

#endif /**< __APP_PRIVATE_H__ */
