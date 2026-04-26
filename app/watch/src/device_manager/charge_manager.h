#ifndef __CHARGE_MANAGER_H__
#define __CHARGE_MANAGER_H__

#include <rtdevice.h>

/*
 * AW32001 current register is configured in 8mA steps.
 * Existing driver currently accepts up to 512mA.
 */
#define CHARGE_MANAGER_CURRENT_MIN_MA 8
#define CHARGE_MANAGER_CURRENT_MAX_MA 512
#define CHARGE_MANAGER_CURRENT_STEP_MA 8
#define CHARGE_MANAGER_CURRENT_DEFAULT_MA 100

typedef void (*charge_manager_callback_t)(rt_uint16_t current_ma, void *user_data);

rt_uint16_t charge_manager_get_current_ma(void);
int charge_manager_set_current_ma(rt_uint16_t current_ma);
int charge_manager_adjust_current(rt_int32_t delta_ma);
int charge_manager_set_ship_mode(rt_bool_t enable);

int charge_manager_register_callback(charge_manager_callback_t callback,
                                     void *user_data);
int charge_manager_unregister_callback(charge_manager_callback_t callback,
                                       void *user_data);

#endif
