#ifndef __ALARM_MANAGER_H__
#define __ALARM_MANAGER_H__

#include <rtthread.h>
#include "alarm_storage.h"

/**
 * @brief Initialize alarm manager.
 *        Loads all enabled alarms from storage and registers them
 *        with RT-Thread's RTC alarm subsystem.
 *        Must be called after alarm_storage_init().
 * @retval 0 on success, negative on error
 */
int alarm_manager_init(void);

/**
 * @brief Re-synchronize all RT-Thread alarms with current storage state.
 *        Call this after any alarm add/delete/update/enable/disable.
 * @retval 0 on success, negative on error
 */
int alarm_manager_sync(void);

/**
 * @brief Check if an alarm was triggered (called from LVGL timer / main loop).
 * @return Index of the triggered alarm, or -1 if none pending.
 *         Returns -1 only once per trigger (consumed on read).
 */
int8_t alarm_manager_get_triggered(void);

#endif /* __ALARM_MANAGER_H__ */
