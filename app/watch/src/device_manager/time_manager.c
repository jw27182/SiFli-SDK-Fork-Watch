#include "time_manager.h"

void dm_print_time(const char *tag, dm_date_time_t dt)
{
    printf("%s %04d %02d %02d %02d:%02d:%02d\n", tag, dt.year, dt.month, dt.day, dt.hour, dt.minute, dt.second);
}


/**
  * @brief  Set system date and time.
  * @param  dt date and time, eg: 2024.1.1 08:30:00
  *
  * @retval If success, return RT_EOK.
  */
rt_err_t dm_set_date_time(dm_date_time_t dt)
{
    time_t now;
    rt_device_t device;
    struct tm *p_tm;
    struct tm tm_new = {0};

    /* Get current time (calendar time). */
    now = time(RT_NULL);

    /* Lock scheduler */
    rt_enter_critical();
    /* Convert calendar time to local time. */
    p_tm = localtime(&now);
    /* Copy time */
    memcpy(&tm_new, p_tm, sizeof(struct tm));
    /* Unlock scheduler */
    rt_exit_critical();

    /* Update system time. */
    tm_new.tm_year = dt.year  - 1900;
    tm_new.tm_mon  = dt.month - 1;
    tm_new.tm_mday = dt.day;
    tm_new.tm_hour = dt.hour;
    tm_new.tm_min = dt.minute;
    tm_new.tm_sec = dt.second;
    /* Convert local time to calendar time. */
    now = mktime(&tm_new);

    /* Find RTC device. */
    device = rt_device_find("rtc");
    if (device == RT_NULL)
    {
        rt_kprintf("app_set_system_time not find device\n");
        return -RT_ERROR;
    }
    /* Update time to RTC device */
    rt_device_control(device, RT_DEVICE_CTRL_RTC_SET_TIME, &now);

    return RT_EOK;
}

/**
  * @brief  Get system date and time.
  * @param dt pointer to variable used to storing current system time.
  * @retval dm_date_time_t current system time.
  */
dm_date_time_t *dm_get_date_time(dm_date_time_t *dt)
{
    /* Get current time(calendar time) */
    time_t ts = time(NULL);
    /* Convert calendar time to local time */
    struct tm *p_tm = localtime(&ts);

    /* Convert local time to dm_date_time_t  */
    LOCAL_TIME_2_DATE_TIME_T(dt, p_tm);

    return dt;
}

#if defined(RT_USING_RTC) && defined(RT_USING_ALARM)

/**
  * @brief  Create and start a one-shot alarm at the specified time.
  * @param  hour  Hour (0-23)
  * @param  min   Minute (0-59)
  * @param  sec   Second (0-59)
  * @param  cb    Callback function invoked when the alarm fires
  * @retval rt_alarm_t handle on success, RT_NULL on failure
  *
  * @note   For full alarm management (repeating alarms, persistence,
  *         day-of-week filtering), use alarm_manager instead.
  */
rt_alarm_t dm_set_alarm(int hour, int min, int sec, rt_alarm_callback_t cb)
{
    struct rt_alarm_setup setup;
    rt_alarm_t alarm;

    memset(&setup, 0, sizeof(setup));
    setup.wktime.tm_hour = hour;
    setup.wktime.tm_min  = min;
    setup.wktime.tm_sec  = sec;
    setup.flag = RT_ALARM_ONESHOT;

    alarm = rt_alarm_create(cb, &setup);
    if (alarm == RT_NULL) {
        rt_kprintf("dm_set_alarm: rt_alarm_create failed\n");
        return RT_NULL;
    }
    rt_alarm_start(alarm);

    rt_kprintf("SET ONESHOT ALARM: [%02d:%02d:%02d]\n", hour, min, sec);
    return alarm;
}

/**
  * @brief  Create and start an alarm with custom flags.
  * @param  hour  Hour (0-23)
  * @param  min   Minute (0-59)
  * @param  sec   Second (0-59)
  * @param  flag  Alarm flag: RT_ALARM_ONESHOT / RT_ALARM_DAILY / RT_ALARM_WEEKLY
  * @param  wday  Weekday for RT_ALARM_WEEKLY (0=Sun,1=Mon,...,6=Sat), ignored otherwise
  * @param  cb    Callback function invoked when the alarm fires
  * @retval rt_alarm_t handle on success, RT_NULL on failure
  */
rt_alarm_t dm_set_alarm_ex(int hour, int min, int sec, uint32_t flag,
                            int wday, rt_alarm_callback_t cb)
{
    struct rt_alarm_setup setup;
    rt_alarm_t alarm;

    memset(&setup, 0, sizeof(setup));
    setup.wktime.tm_hour = hour;
    setup.wktime.tm_min  = min;
    setup.wktime.tm_sec  = sec;
    setup.flag = flag;
    if (flag == RT_ALARM_WEEKLY)
        setup.wktime.tm_wday = wday;

    alarm = rt_alarm_create(cb, &setup);
    if (alarm == RT_NULL) {
        rt_kprintf("dm_set_alarm_ex: rt_alarm_create failed\n");
        return RT_NULL;
    }
    rt_alarm_start(alarm);

    rt_kprintf("SET ALARM: [%02d:%02d:%02d] flag=0x%04x wday=%d\n",
               hour, min, sec, (unsigned)flag, wday);
    return alarm;
}

#endif
