#ifndef __GNSS_MANAGER_H__
#define __GNSS_MANAGER_H__

#include <rtthread.h>

#include "time_manager.h"

typedef void (*gnss_manager_sync_success_cb_t)(const dm_date_time_t *utc_time,
                                               void *user_data);
typedef void (*gnss_manager_sync_fail_cb_t)(rt_err_t err, void *user_data);

int gnss_manager_open(void);
int gnss_manager_close(void);

int gnss_manager_sync_utc_to_rtc_async(gnss_manager_sync_success_cb_t on_success,
                                       gnss_manager_sync_fail_cb_t on_fail,
                                       void *user_data);

#endif
