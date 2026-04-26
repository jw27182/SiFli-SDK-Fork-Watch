#ifndef __BRIGHTNESS_MANAGER_H__
#define __BRIGHTNESS_MANAGER_H__

#include <rtdevice.h>
#include <rtthread.h>

/* 对外统一亮度范围，UI 和业务都用这一组值 */
#define BRIGHTNESS_MIN_VAL 10
#define BRIGHTNESS_MAX_VAL 100

typedef void (*brightness_manager_callback_t)(rt_uint8_t brightness,
                                              rt_bool_t auto_enabled,
                                              void *user_data);
typedef void (*brightness_manager_lux_callback_t)(rt_int32_t lux,
                                                  void *user_data);

int brightness_manager_init(void);
rt_uint8_t brightness_manager_get_value(void);
rt_bool_t brightness_manager_is_auto_enabled(void);
rt_int32_t brightness_manager_get_lux(void);

int brightness_manager_set_auto_enabled(rt_bool_t enabled);
int brightness_manager_set_manual_brightness(rt_uint8_t value);
int brightness_manager_adjust_manual_brightness(rt_int32_t delta);
int brightness_manager_process_lux(rt_int32_t lux);

int brightness_manager_register_callback(brightness_manager_callback_t callback,
                                         void *user_data);
int brightness_manager_unregister_callback(brightness_manager_callback_t callback,
                                           void *user_data);
int brightness_manager_register_lux_callback(
    brightness_manager_lux_callback_t callback, void *user_data);
int brightness_manager_unregister_lux_callback(
    brightness_manager_lux_callback_t callback, void *user_data);

#endif