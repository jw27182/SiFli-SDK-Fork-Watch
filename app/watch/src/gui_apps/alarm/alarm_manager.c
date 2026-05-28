/*********************
 *      INCLUDES
 *********************/
#include <rtthread.h>
#include <rtdevice.h>
#include <time.h>
#include <string.h>
#include <stdio.h>

#include "alarm_manager.h"
#include "alarm_storage.h"
#include "vibrator_manager.h"
#include "my_widget.h"

#define LOG_TAG "alarm.mgr"
#define LOG_LVL LOG_LVL_DBG
#include <ulog.h>

/*
 * Alarm Manager
 * =============
 * Bridges alarm_storage (persistent data) with RT-Thread's RTC alarm
 * subsystem (hardware wakeup).
 *
 * For each enabled alarm in storage, we create one or more rt_alarm objects:
 *   - ONE_SHOT  → 1 × RT_ALARM_ONESHOT
 *   - EVERYDAY  → 1 × RT_ALARM_DAILY
 *   - Specific  → N × RT_ALARM_WEEKLY  (one per selected weekday)
 *
 * When an alarm fires, the callback (runs in alarmsvc thread) does:
 *   1. Verify the alarm is still enabled and today matches repeat_days
 *   2. Trigger vibration
 *   3. Disable one-shot alarms after they fire
 *
 * A volatile flag is set so the LVGL main loop can show a toast if desired.
 */

#if defined(RT_USING_RTC) && defined(RT_USING_ALARM)

/* Maximum number of RT-Thread alarm objects.
 * Worst case: 10 alarms × 7 days = 70, but we cap at a reasonable limit. */
#define MAX_RT_ALARMS  (ALARM_MAX_COUNT * 7)

/* Per-RT-alarm bookkeeping */
typedef struct {
    rt_alarm_t  handle;     /* RT-Thread alarm handle */
    uint8_t     alarm_idx;  /* index into alarm_entry_t[] in storage */
    uint8_t     active;     /* 1 if created and started */
} rt_alarm_slot_t;

static rt_alarm_slot_t g_slots[MAX_RT_ALARMS];
static uint8_t         g_slot_count = 0;

/* Flag for LVGL-side notification (set by callback, polled by LVGL timer) */
static volatile int8_t g_triggered_alarm_idx = -1;

/* LVGL timers for alarm trigger UX */
static lv_timer_t *g_poll_timer = NULL;
static lv_timer_t *g_vib_timer = NULL;      /* Continuous vibration timer */
static lv_timer_t *g_auto_close_timer = NULL; /* 10-second auto-dismiss */

/* Snooze alarm handle for cancellation */
static rt_alarm_t g_snooze_handle = RT_NULL;

/* Vibration state */
static volatile uint8_t g_vib_active = 0;

/* Currently triggered alarm index (persisted for auto-close timer) */
static int8_t g_current_alarm_idx = -1;

/* ------------------------------------------------------------------ */
/*  Internal helpers                                                   */
/* ------------------------------------------------------------------ */

/**
 * @brief RT-Thread alarm callback — runs in the alarmsvc thread.
 */
static void rt_alarm_cb(rt_alarm_t alarm, time_t timestamp)
{
    int i;
    for (i = 0; i < g_slot_count; i++) {
        if (g_slots[i].handle == alarm && g_slots[i].active)
            break;
    }
    if (i >= g_slot_count)
        return;

    uint8_t idx = g_slots[i].alarm_idx;
    alarm_entry_t entry;
    if (alarm_storage_get(idx, &entry) != 0)
        return;
    if (entry.state != ALARM_STATE_ENABLE)
        return;

    /* Day-of-week check for DAILY alarms with specific days */
    struct tm *tm_local = localtime(&timestamp);
    int wday = tm_local->tm_wday; /* 0=Sun,1=Mon,...,6=Sat */
    /* Convert to our bitmask: bit0=Mon, bit1=Tue, ..., bit6=Sun */
    uint8_t day_bit = (wday == 0) ? (1 << 6) : (1 << (wday - 1));

    if (entry.repeat_days != ALARM_REPEAT_ONE_SHOT &&
        entry.repeat_days != ALARM_REPEAT_EVERYDAY &&
        !(entry.repeat_days & day_bit)) {
        /* Today is not an alarm day — ignore */
        return;
    }

    LOG_I("=== ALARM %d TRIGGERED at %02d:%02d ===",
          idx, entry.hour, entry.minute);

    /* Signal LVGL side — continuous vibration is driven by LVGL timer */
    g_triggered_alarm_idx = (int8_t)idx;

    /* Disable one-shot alarms after they fire */
    if (entry.repeat_days == ALARM_REPEAT_ONE_SHOT) {
        entry.state = ALARM_STATE_DISABLE;
        alarm_storage_update(idx, &entry);
        LOG_I("One-shot alarm %d disabled after trigger", idx);
    }
}

/**
 * @brief Destroy all RT-Thread alarm objects.
 */
static void destroy_all_slots(void)
{
    for (int i = 0; i < g_slot_count; i++) {
        if (g_slots[i].handle) {
            rt_alarm_stop(g_slots[i].handle);
            rt_alarm_delete(g_slots[i].handle);
            g_slots[i].handle = RT_NULL;
        }
        g_slots[i].active = 0;
    }
    g_slot_count = 0;
}

/**
 * @brief Create and start one RT-Thread alarm.
 * @param alarm_idx  Index into alarm_entry_t[] in storage
 * @param entry      Alarm entry data
 * @param wday       RT-Thread weekday (0=Sun,1=Mon,...,6=Sat), used for WEEKLY
 * @return 0 on success, -1 on failure
 */
static int create_one_alarm(uint8_t alarm_idx, const alarm_entry_t *entry,
                            int wday)
{
    if (g_slot_count >= MAX_RT_ALARMS) {
        LOG_E("RT alarm slots exhausted (%d)", g_slot_count);
        return -1;
    }

    struct rt_alarm_setup setup;
    memset(&setup, 0, sizeof(setup));
    setup.wktime.tm_hour = entry->hour;
    setup.wktime.tm_min  = entry->minute;
    setup.wktime.tm_sec  = 0;

    if (entry->repeat_days == ALARM_REPEAT_ONE_SHOT) {
        setup.flag = RT_ALARM_ONESHOT;
    } else if (entry->repeat_days == ALARM_REPEAT_EVERYDAY) {
        setup.flag = RT_ALARM_DAILY;
    } else {
        setup.flag = RT_ALARM_WEEKLY;
        setup.wktime.tm_wday = wday;
    }

    rt_alarm_t h = rt_alarm_create(rt_alarm_cb, &setup);
    if (h == RT_NULL) {
        LOG_E("rt_alarm_create failed for alarm %d wday=%d", alarm_idx, wday);
        return -1;
    }
    rt_alarm_start(h);

    g_slots[g_slot_count].handle    = h;
    g_slots[g_slot_count].alarm_idx = alarm_idx;
    g_slots[g_slot_count].active    = 1;
    g_slot_count++;

    return 0;
}

/* ------------------------------------------------------------------ */
/*  Alarm trigger popup UI                                             */
/* ------------------------------------------------------------------ */

#define SNOOZE_MINUTES    5
#define POLL_INTERVAL_MS  200
#define AUTO_CLOSE_MS     10000   /* 10 seconds */
#define VIB_DURATION_MS   400     /* 单次震动时长 */
#define VIB_INTERVAL_MS   600     /* 震动间隔 */
#define VIB_INTENSITY     80      /* 震动强度 0-100 */

/* Forward declarations */
static void snooze_alarm(uint8_t alarm_idx);
static void alarm_poll_timer_cb(lv_timer_t *timer);

/* ------------------------------------------------------------------ */
/*  Vibration control helpers                                          */
/* ------------------------------------------------------------------ */

/**
 * @brief LVGL timer callback for continuous vibration.
 */
static void vibration_timer_cb(lv_timer_t *timer)
{
    (void)timer;
    if (g_vib_active) {
        vibrator_send(VIB_DURATION_MS, VIB_INTENSITY);
    }
}

/**
 * @brief Stop continuous vibration.
 */
static void stop_vibration(void)
{
    g_vib_active = 0;
    if (g_vib_timer) {
        lv_timer_del(g_vib_timer);
        g_vib_timer = NULL;
    }
}

/**
 * @brief Start continuous vibration.
 */
static void start_vibration(void)
{
    g_vib_active = 1;
    if (g_vib_timer == NULL) {
        g_vib_timer = lv_timer_create(vibration_timer_cb,
                                      VIB_DURATION_MS + VIB_INTERVAL_MS, NULL);
    }
    /* Start first vibration immediately */
    vibrator_send(VIB_DURATION_MS, VIB_INTENSITY);
}

/* ------------------------------------------------------------------ */
/*  Snooze notification popup                                          */
/* ------------------------------------------------------------------ */

/* Static storage for button user_data */
static uint8_t s_snooze_cancel_idx;

/**
 * @brief Callback for "关闭下次响起" button.
 */
static void cancel_snooze_btn_cb(lv_event_t *e)
{
    (void)e;
    /* Cancel the snooze alarm if it exists */
    if (g_snooze_handle) {
        rt_alarm_stop(g_snooze_handle);
        rt_alarm_delete(g_snooze_handle);
        g_snooze_handle = RT_NULL;
        LOG_I("Snooze alarm cancelled");
    }
}

/**
 * @brief Callback for "知道了" button (just closes popup).
 */
static void dismiss_snooze_btn_cb(lv_event_t *e)
{
    (void)e;
    /* Nothing to do, popup will auto-close */
}

/**
 * @brief Show snooze notification popup.
 */
static void show_snooze_notification(uint8_t alarm_idx, uint8_t hour, uint8_t minute)
{
    char subtitle[64];
    uint8_t snooze_hour = hour;
    uint8_t snooze_min  = minute + SNOOZE_MINUTES;
    if (snooze_min >= 60) {
        snooze_min -= 60;
        snooze_hour = (snooze_hour + 1) % 24;
    }
    snprintf(subtitle, sizeof(subtitle), "闹钟将在%d:%02d再次响起",
             snooze_hour, snooze_min);

    /* Store idx for cancel button */
    s_snooze_cancel_idx = alarm_idx;

    myui_popup_btn_t btns[2];
    btns[0].text       = "关闭下次响起";
    btns[0].text_color = lv_color_hex(0xc0c0c0);
    btns[0].cb         = cancel_snooze_btn_cb;
    btns[0].user_data  = &s_snooze_cancel_idx;

    btns[1].text       = "知道了";
    btns[1].text_color = lv_color_hex(0x55aa6c);
    btns[1].cb         = dismiss_snooze_btn_cb;
    btns[1].user_data  = NULL;

    myui_popup_show(NULL, "贪睡提醒", subtitle, btns, 2);
}

/* ------------------------------------------------------------------ */
/*  Auto-close timer                                                   */
/* ------------------------------------------------------------------ */

/**
 * @brief LVGL timer callback for 10-second auto-dismiss.
 */
static void auto_close_timer_cb(lv_timer_t *timer)
{
    (void)timer;
    LOG_I("Auto-close: no user interaction for 10 seconds");

    /* Stop vibration */
    stop_vibration();

    /* Use persisted alarm index */
    int8_t idx = g_current_alarm_idx;
    if (idx < 0) {
        /* No triggered alarm, just close popup */
        myui_popup_close();
        return;
    }

    alarm_entry_t entry;
    if (alarm_storage_get((uint8_t)idx, &entry) == 0) {
        /* Create snooze alarm */
        snooze_alarm((uint8_t)idx);
        /* Close current popup first, then show snooze notification */
        myui_popup_close();
        show_snooze_notification((uint8_t)idx, entry.hour, entry.minute);
    } else {
        /* Just close popup if can't get alarm entry */
        myui_popup_close();
    }

    /* Reset state */
    g_current_alarm_idx = -1;
    g_triggered_alarm_idx = -1;

    /* Delete this timer */
    if (g_auto_close_timer) {
        lv_timer_del(g_auto_close_timer);
        g_auto_close_timer = NULL;
    }
}

/* ------------------------------------------------------------------ */
/*  Public API                                                         */
/* ------------------------------------------------------------------ */

int alarm_manager_init(void)
{
    memset(g_slots, 0, sizeof(g_slots));
    g_slot_count = 0;
    g_triggered_alarm_idx = -1;
    g_current_alarm_idx = -1;
    g_vib_active = 0;
    g_vib_timer = NULL;
    g_auto_close_timer = NULL;
    g_snooze_handle = RT_NULL;

    /* Start LVGL timer to poll for triggered alarms */
    if (g_poll_timer == NULL) {
        g_poll_timer = lv_timer_create(alarm_poll_timer_cb,
                                       POLL_INTERVAL_MS, NULL);
    }

    LOG_I("alarm_manager initialized");
    return alarm_manager_sync();
}

int alarm_manager_sync(void)
{
    destroy_all_slots();

    alarm_db_t db;
    alarm_storage_load(&db);

    int registered = 0;

    for (uint8_t i = 0; i < db.count; i++) {
        const alarm_entry_t *e = &db.alarms[i];
        if (e->state != ALARM_STATE_ENABLE)
            continue;

        if (e->repeat_days == ALARM_REPEAT_ONE_SHOT) {
            /* One-shot: single ONESHOT alarm */
            if (create_one_alarm(i, e, 0) == 0) {
                registered++;
                LOG_I("  [%d] one-shot  %02d:%02d", i, e->hour, e->minute);
            }
        } else if (e->repeat_days == ALARM_REPEAT_EVERYDAY) {
            /* Every day: single DAILY alarm */
            if (create_one_alarm(i, e, 0) == 0) {
                registered++;
                LOG_I("  [%d] daily     %02d:%02d", i, e->hour, e->minute);
            }
        } else {
            /* Specific weekdays: one WEEKLY alarm per selected day */
            uint8_t days = e->repeat_days;
            int day_count = 0;
            for (int d = 0; d < 7; d++) {
                if (days & (1 << d)) {
                    /* Our bitmask: bit0=Mon..bit6=Sun
                     * RT-Thread wday: 0=Sun,1=Mon,...,6=Sat */
                    int rt_wday = (d == 6) ? 0 : (d + 1);
                    if (create_one_alarm(i, e, rt_wday) == 0)
                        day_count++;
                }
            }
            if (day_count > 0) {
                registered++;
                LOG_I("  [%d] weekly    %02d:%02d (%d days)",
                      i, e->hour, e->minute, day_count);
            }
        }
    }

    LOG_I("alarm_manager_sync: %d alarms, %d RT-alarm objects registered",
          registered, g_slot_count);
    return 0;
}

int8_t alarm_manager_get_triggered(void)
{
    int8_t idx = g_triggered_alarm_idx;
    if (idx >= 0)
        g_triggered_alarm_idx = -1; /* consume */
    return idx;
}

/**
 * @brief Create a one-shot snooze alarm SNOOZE_MINUTES from now.
 */
static void snooze_alarm(uint8_t alarm_idx)
{
    time_t now = time(NULL);
    struct tm *t = localtime(&now);
    int new_min  = t->tm_min + SNOOZE_MINUTES;
    int new_hour = t->tm_hour;
    if (new_min >= 60) {
        new_min -= 60;
        new_hour = (new_hour + 1) % 24;
    }

    struct rt_alarm_setup setup;
    memset(&setup, 0, sizeof(setup));
    setup.wktime.tm_hour = new_hour;
    setup.wktime.tm_min  = new_min;
    setup.wktime.tm_sec  = 0;
    setup.flag = RT_ALARM_ONESHOT;

    /* Cancel any previous snooze alarm */
    if (g_snooze_handle) {
        rt_alarm_stop(g_snooze_handle);
        rt_alarm_delete(g_snooze_handle);
        g_snooze_handle = RT_NULL;
    }

    rt_alarm_t h = rt_alarm_create(rt_alarm_cb, &setup);
    if (h) {
        rt_alarm_start(h);
        g_snooze_handle = h; /* Track for cancellation */

        /* Track this alarm so it can be cleaned up on sync */
        if (g_slot_count < MAX_RT_ALARMS) {
            g_slots[g_slot_count].handle    = h;
            g_slots[g_slot_count].alarm_idx = alarm_idx;
            g_slots[g_slot_count].active    = 1;
            g_slot_count++;
        }
        LOG_I("Snooze: alarm %d rescheduled to %02d:%02d",
              alarm_idx, new_hour, new_min);
    } else {
        LOG_E("Snooze: rt_alarm_create failed");
    }
}

static void snooze_btn_cb(lv_event_t *e)
{
    uint8_t idx = *(uint8_t *)lv_event_get_user_data(e);

    /* Stop vibration */
    stop_vibration();

    /* Create snooze alarm */
    snooze_alarm(idx);

    /* Get alarm entry for notification */
    alarm_entry_t entry;
    if (alarm_storage_get(idx, &entry) == 0) {
        /* Show snooze notification */
        show_snooze_notification(idx, entry.hour, entry.minute);
    }

    /* Reset state */
    g_current_alarm_idx = -1;
    g_triggered_alarm_idx = -1;

    /* Delete auto-close timer if running */
    if (g_auto_close_timer) {
        lv_timer_del(g_auto_close_timer);
        g_auto_close_timer = NULL;
    }
}

/**
 * @brief Dismiss button callback — stops vibration.
 */
static void dismiss_btn_cb(lv_event_t *e)
{
    (void)e;
    stop_vibration();

    /* Reset state */
    g_current_alarm_idx = -1;
    g_triggered_alarm_idx = -1;

    /* Delete auto-close timer if running */
    if (g_auto_close_timer) {
        lv_timer_del(g_auto_close_timer);
        g_auto_close_timer = NULL;
    }
}

/**
 * @brief LVGL timer callback — polls the triggered flag from the LVGL thread.
 */
static void alarm_poll_timer_cb(lv_timer_t *timer)
{
    (void)timer;
    int8_t idx = alarm_manager_get_triggered();
    if (idx < 0)
        return;

    alarm_entry_t entry;
    if (alarm_storage_get((uint8_t)idx, &entry) != 0)
        return;

    /* Persist idx for auto-close timer */
    g_current_alarm_idx = idx;

    char time_str[16];
    snprintf(time_str, sizeof(time_str), "%d:%02d", entry.hour, entry.minute);

    /* Store idx in static so user_data pointer remains valid during popup lifetime */
    static uint8_t s_snooze_idx;
    s_snooze_idx = (uint8_t)idx;

    myui_popup_btn_t btns[2];
    btns[0].text       = "贪睡";
    btns[0].text_color = lv_color_hex(0x55aa6c);
    btns[0].cb         = snooze_btn_cb;
    btns[0].user_data  = &s_snooze_idx;

    btns[1].text       = "关闭";
    btns[1].text_color = lv_color_hex(0xc0c0c0);
    btns[1].cb         = dismiss_btn_cb;
    btns[1].user_data  = NULL;

    myui_popup_show(NULL, "闹钟时间到", time_str, btns, 2);

    /* Start continuous vibration */
    start_vibration();

    /* Start 10-second auto-close timer */
    if (g_auto_close_timer) {
        lv_timer_del(g_auto_close_timer);
    }
    g_auto_close_timer = lv_timer_create(auto_close_timer_cb,
                                         AUTO_CLOSE_MS, NULL);
    lv_timer_set_repeat_count(g_auto_close_timer, 1);
}

#else /* !RT_USING_RTC || !RT_USING_ALARM */

/* Stubs when RTC alarm is not available */
int alarm_manager_init(void)
{
    LOG_W("RT_USING_ALARM not enabled, alarm manager disabled");
    return 0;
}

int alarm_manager_sync(void)  { return 0; }
int8_t alarm_manager_get_triggered(void) { return -1; }

#endif /* RT_USING_RTC && RT_USING_ALARM */
