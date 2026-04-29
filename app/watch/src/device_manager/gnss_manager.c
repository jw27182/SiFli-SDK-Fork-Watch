#include "gnss_manager.h"

#include <string.h>
#include <time.h>

#include "rtconfig.h"
#include "lvgl.h"

#ifdef GPS_USING_ATGM336H
#include "um_gps_if.h"
#endif

#define DBG_TAG "dm.gnss"
#define DBG_LVL DBG_INFO
#include <rtdbg.h>

#define GNSS_SYNC_POLL_PERIOD_MS 1000
#define GNSS_SYNC_TIMEOUT_MS 30000

#ifdef GPS_USING_ATGM336H

static rt_uint16_t g_open_ref = 0;
static rt_bool_t g_gnss_started = RT_FALSE;

static lv_timer_t *g_sync_timer = RT_NULL;
static rt_uint32_t g_sync_elapsed_ms = 0;
static rt_bool_t g_sync_in_progress = RT_FALSE;
static rt_bool_t g_sync_hold_open = RT_FALSE;

static gnss_manager_sync_success_cb_t g_sync_success_cb = RT_NULL;
static gnss_manager_sync_fail_cb_t g_sync_fail_cb = RT_NULL;
static void *g_sync_user_data = RT_NULL;
static dm_date_time_t g_last_sync_time = {0};

static rt_bool_t gnss_time_is_valid(const GpsData *gps_data) {
    const struct tm *utc;

    if (!gps_data) return RT_FALSE;
    utc = &gps_data->location_data.timestamp;

    if (utc->tm_year < 2020 || utc->tm_year > 2100) return RT_FALSE;
    if (utc->tm_mon < 1 || utc->tm_mon > 12) return RT_FALSE;
    if (utc->tm_mday < 1 || utc->tm_mday > 31) return RT_FALSE;
    if (utc->tm_hour < 0 || utc->tm_hour > 23) return RT_FALSE;
    if (utc->tm_min < 0 || utc->tm_min > 59) return RT_FALSE;
    if (utc->tm_sec < 0 || utc->tm_sec > 59) return RT_FALSE;

    return RT_TRUE;
}

static void gnss_sync_finish(rt_err_t err) {
    if (g_sync_timer) {
        lv_timer_del(g_sync_timer);
        g_sync_timer = RT_NULL;
    }

    g_sync_in_progress = RT_FALSE;
    g_sync_elapsed_ms = 0;

    if (err == RT_EOK) {
        if (g_sync_success_cb) {
            g_sync_success_cb(&g_last_sync_time, g_sync_user_data);
        }
    } else {
        if (g_sync_fail_cb) {
            g_sync_fail_cb(err, g_sync_user_data);
        }
    }

    g_sync_success_cb = RT_NULL;
    g_sync_fail_cb = RT_NULL;
    g_sync_user_data = RT_NULL;

    if (g_sync_hold_open) {
        gnss_manager_close();
        g_sync_hold_open = RT_FALSE;
    }
}

static void gnss_sync_timer_cb(lv_timer_t *timer) {
    GpsData gps_data;
    dm_date_time_t dt_set;

    (void)timer;

    memset(&gps_data, 0, sizeof(gps_data));
    if (um_gps_get_data(RT_NULL, RT_NULL, RT_NULL, &gps_data) == 0 &&
        gnss_time_is_valid(&gps_data)) {
        dt_set.year = gps_data.location_data.timestamp.tm_year;
        dt_set.month = gps_data.location_data.timestamp.tm_mon;
        dt_set.day = gps_data.location_data.timestamp.tm_mday;
        dt_set.hour = gps_data.location_data.timestamp.tm_hour;
        dt_set.minute = gps_data.location_data.timestamp.tm_min;
        dt_set.second = gps_data.location_data.timestamp.tm_sec;

        if (dm_set_date_time(dt_set) == RT_EOK) {
            g_last_sync_time = dt_set;
            gnss_sync_finish(RT_EOK);
        } else {
            gnss_sync_finish(-RT_ERROR);
        }
        return;
    }

    g_sync_elapsed_ms += GNSS_SYNC_POLL_PERIOD_MS;
    if (g_sync_elapsed_ms >= GNSS_SYNC_TIMEOUT_MS) {
        gnss_sync_finish(-RT_ETIMEOUT);
    }
}

int gnss_manager_open(void) {
    int ret;

    if (g_open_ref == 0 && !g_gnss_started) {
        ret = um_gps_open();
        if (ret != 0) {
            LOG_E("open gnss failed: %d", ret);
            return -RT_ERROR;
        }
        g_gnss_started = RT_TRUE;
    }

    g_open_ref++;
    return RT_EOK;
}

int gnss_manager_close(void) {
    int ret;

    if (g_open_ref == 0) return -RT_ERROR;

    g_open_ref--;
    if (g_open_ref > 0 || g_sync_in_progress || !g_gnss_started) return RT_EOK;

    ret = um_gps_close();
    if (ret != 0) {
        LOG_E("close gnss failed: %d", ret);
        return -RT_ERROR;
    }

    g_gnss_started = RT_FALSE;
    return RT_EOK;
}

int gnss_manager_sync_utc_to_rtc_async(gnss_manager_sync_success_cb_t on_success,
                                       gnss_manager_sync_fail_cb_t on_fail,
                                       void *user_data) {
    int ret;

    if (g_sync_in_progress) return -RT_EBUSY;

    ret = gnss_manager_open();
    if (ret != RT_EOK) {
        LOG_E("open gnss for sync failed: %d", ret);
        return ret;
    }
    g_sync_hold_open = RT_TRUE;

    g_sync_success_cb = on_success;
    g_sync_fail_cb = on_fail;
    g_sync_user_data = user_data;
    g_sync_elapsed_ms = 0;
    g_sync_in_progress = RT_TRUE;

    g_sync_timer = lv_timer_create(gnss_sync_timer_cb, GNSS_SYNC_POLL_PERIOD_MS,
                                   RT_NULL);
    if (!g_sync_timer) {
        g_sync_in_progress = RT_FALSE;
        g_sync_success_cb = RT_NULL;
        g_sync_fail_cb = RT_NULL;
        g_sync_user_data = RT_NULL;
        if (g_sync_hold_open) {
            gnss_manager_close();
            g_sync_hold_open = RT_FALSE;
        }
        return -RT_ENOMEM;
    }

    lv_timer_set_repeat_count(g_sync_timer, -1);
    return RT_EOK;
}

static int gps_init(void)
{
    return um_gps_init();
    gnss_manager_close();
}

INIT_DEVICE_EXPORT(gps_init);

#else

int gnss_manager_open(void) { return -RT_ENOSYS; }

int gnss_manager_close(void) { return -RT_ENOSYS; }

int gnss_manager_sync_utc_to_rtc_async(gnss_manager_sync_success_cb_t on_success,
                                       gnss_manager_sync_fail_cb_t on_fail,
                                       void *user_data) {
    (void)on_success;
    (void)on_fail;
    (void)user_data;
    return -RT_ENOSYS;
}

#endif
