#include "brightness_manager.h"

#include <math.h>

#include "lvgl.h"
#include "sensor.h"

#define DBG_TAG "dm.brightness"
#define DBG_LVL DBG_INFO
#include <rtdbg.h>

#define clamp(val, min, max) \
    ((val) < (min) ? (min) : ((val) > (max) ? (max) : (val)))

/* 自动亮度参数 */
#define AUTO_LUX_EMA_SHIFT 4
#define AUTO_RAMP_STEP_UP 2
#define AUTO_RAMP_STEP_DOWN 1
#define AUTO_TARGET_HYST_UP 5
#define AUTO_TARGET_HYST_DOWN 6

#define AUTO_LUX_LOG_ANCHOR_LUX 50.0f
#define AUTO_LUX_LOG_ANCHOR_LEVEL 50.0f
#define AUTO_LUX_LOG_GAIN 15.0f
#define AUTO_LUX_LOG_INPUT_MIN 1.0f

#define AUTO_TIMER_PERIOD_MS 50
#define BRIGHTNESS_MANAGER_MAX_CALLBACKS 8
#define BRIGHTNESS_MANAGER_MAX_LUX_CALLBACKS 4

typedef struct {
    brightness_manager_callback_t callback;
    void *user_data;
} brightness_callback_slot_t;

typedef struct {
    brightness_manager_lux_callback_t callback;
    void *user_data;
} brightness_lux_callback_slot_t;

static rt_device_t g_lcd_sensor_dev = RT_NULL;
static rt_device_t g_brightness_sensor_dev = RT_NULL;
static lv_timer_t *g_auto_timer = RT_NULL;
static rt_bool_t g_inited = RT_FALSE;
static rt_uint8_t g_brightness_now = BRIGHTNESS_MAX_VAL;
static rt_bool_t g_auto_brightness = RT_FALSE;
static rt_bool_t g_auto_lux_filter_reset = RT_FALSE;
static rt_int32_t g_auto_lux_ema = -1;
static rt_int32_t g_latest_lux = 0;
static rt_uint8_t g_auto_hyst_target = BRIGHTNESS_MAX_VAL;
static brightness_callback_slot_t g_callbacks[BRIGHTNESS_MANAGER_MAX_CALLBACKS];
static brightness_lux_callback_slot_t
    g_lux_callbacks[BRIGHTNESS_MANAGER_MAX_LUX_CALLBACKS];

static rt_uint8_t lux_to_brightness_target(rt_int32_t lux) {
    float x = (float)lux;
    float t;

    if (x < AUTO_LUX_LOG_INPUT_MIN) x = AUTO_LUX_LOG_INPUT_MIN;

    t = AUTO_LUX_LOG_ANCHOR_LEVEL +
        AUTO_LUX_LOG_GAIN * logf(x / AUTO_LUX_LOG_ANCHOR_LUX);

    return (rt_uint8_t)clamp((rt_int32_t)(t + 0.5f), BRIGHTNESS_MIN_VAL,
                             BRIGHTNESS_MAX_VAL);
}

static rt_uint8_t auto_target_apply_hysteresis(rt_uint8_t raw_target,
                                                rt_uint8_t hyst_now) {
    rt_int16_t d = (rt_int16_t)raw_target - (rt_int16_t)hyst_now;

    if (d >= (rt_int16_t)AUTO_TARGET_HYST_UP) return raw_target;
    if (d <= -(rt_int16_t)AUTO_TARGET_HYST_DOWN) return raw_target;
    return hyst_now;
}

static rt_uint8_t auto_brightness_ramp_step(rt_uint8_t cur, rt_uint8_t goal) {
    rt_int32_t delta;

    if (cur == goal) return cur;

    if (cur < goal) {
        delta = (rt_int32_t)goal - (rt_int32_t)cur;
        return (rt_uint8_t)((rt_int32_t)cur +
                            clamp(delta, 1, AUTO_RAMP_STEP_UP));
    }

    delta = (rt_int32_t)cur - (rt_int32_t)goal;
    return (rt_uint8_t)((rt_int32_t)cur - clamp(delta, 1, AUTO_RAMP_STEP_DOWN));
}

static void brightness_notify(rt_uint8_t brightness, rt_bool_t auto_enabled) {
    rt_size_t i;
    for (i = 0; i < BRIGHTNESS_MANAGER_MAX_CALLBACKS; i++) {
        if (g_callbacks[i].callback) {
            g_callbacks[i].callback(brightness, auto_enabled,
                                    g_callbacks[i].user_data);
        }
    }
}

static void brightness_lux_notify(rt_int32_t lux) {
    rt_size_t i;
    for (i = 0; i < BRIGHTNESS_MANAGER_MAX_LUX_CALLBACKS; i++) {
        if (g_lux_callbacks[i].callback) {
            g_lux_callbacks[i].callback(lux, g_lux_callbacks[i].user_data);
        }
    }
}

static void brightness_apply_to_lcd(rt_uint8_t value) {
    rt_uint8_t level = (rt_uint8_t)clamp(value, BRIGHTNESS_MIN_VAL,
                                         BRIGHTNESS_MAX_VAL);
    if (!g_lcd_sensor_dev) return;
    rt_device_control(g_lcd_sensor_dev, RTGRAPHIC_CTRL_SET_BRIGHTNESS, &level);
}

static void brightness_auto_timer_cb(lv_timer_t *timer) {
    static struct rt_sensor_data brightness_data;
    rt_int32_t lux_raw;

    (void)timer;

    if (!g_brightness_sensor_dev) return;
    if (rt_device_read(g_brightness_sensor_dev, 0, &brightness_data, 1) != 1) {
        return;
    }

    lux_raw = brightness_data.data.light;
    g_latest_lux = lux_raw;
    brightness_lux_notify(lux_raw);
    brightness_manager_process_lux(lux_raw);
}

static int brightness_sensor_init(void) {
    rt_err_t ret;

    if (g_brightness_sensor_dev) return RT_EOK;

    g_brightness_sensor_dev = rt_device_find("li_ltr303");
    if (g_brightness_sensor_dev == RT_NULL) {
        LOG_W("find ltr303 sensor device failed\n");
        return -RT_ERROR;
    }

    ret = rt_device_open(g_brightness_sensor_dev, RT_DEVICE_FLAG_RDONLY);
    if (ret != RT_EOK) {
        LOG_W("open ltr303 sensor failed: %d\n", ret);
        g_brightness_sensor_dev = RT_NULL;
        return -RT_ERROR;
    }

    return RT_EOK;
}

int brightness_manager_init(void) {
    if (g_inited) return RT_EOK;

    g_lcd_sensor_dev = rt_device_find("lcd");
    if (g_lcd_sensor_dev == RT_NULL) {
        LOG_E("find lcd device failed!\n");
        return -RT_ERROR;
    }

    brightness_sensor_init();

    g_auto_timer = lv_timer_create(brightness_auto_timer_cb, AUTO_TIMER_PERIOD_MS,
                                   RT_NULL);
    if (!g_auto_timer) {
        LOG_E("create auto brightness timer failed\n");
        return -RT_ERROR;
    }
    lv_timer_set_repeat_count(g_auto_timer, -1);

    g_inited = RT_TRUE;
    brightness_apply_to_lcd(g_brightness_now);
    return RT_EOK;
}

rt_uint8_t brightness_manager_get_value(void) { return g_brightness_now; }

rt_bool_t brightness_manager_is_auto_enabled(void) { return g_auto_brightness; }

rt_int32_t brightness_manager_get_lux(void) { return g_latest_lux; }

int brightness_manager_set_auto_enabled(rt_bool_t enabled) {
    rt_bool_t changed = RT_FALSE;

    if (!g_inited && brightness_manager_init() != RT_EOK) return -RT_ERROR;

    enabled = enabled ? RT_TRUE : RT_FALSE;
    if (g_auto_brightness != enabled) {
        g_auto_brightness = enabled;
        changed = RT_TRUE;
    }

    if (g_auto_brightness) {
        g_auto_lux_filter_reset = RT_TRUE;
    } else {
        g_auto_lux_ema = -1;
    }

    if (changed) {
        LOG_I("auto brightness set: %s\n", g_auto_brightness ? "on" : "off");
        brightness_notify(g_brightness_now, g_auto_brightness);
    }

    return RT_EOK;
}

int brightness_manager_set_manual_brightness(rt_uint8_t value) {
    rt_uint8_t target =
        (rt_uint8_t)clamp(value, BRIGHTNESS_MIN_VAL, BRIGHTNESS_MAX_VAL);
    rt_bool_t changed = RT_FALSE;
    rt_bool_t mode_changed = RT_FALSE;

    if (!g_inited && brightness_manager_init() != RT_EOK) return -RT_ERROR;

    if (g_auto_brightness) {
        g_auto_brightness = RT_FALSE;
        g_auto_lux_ema = -1;
        mode_changed = RT_TRUE;
    }

    if (g_brightness_now != target) {
        g_brightness_now = target;
        brightness_apply_to_lcd(g_brightness_now);
        changed = RT_TRUE;
    }

    if (mode_changed || changed) {
        if (mode_changed) LOG_I("auto brightness set: off\n");
        brightness_notify(g_brightness_now, g_auto_brightness);
    }

    return RT_EOK;
}

int brightness_manager_adjust_manual_brightness(rt_int32_t delta) {
    rt_int32_t value = (rt_int32_t)g_brightness_now + delta;
    return brightness_manager_set_manual_brightness(
        (rt_uint8_t)clamp(value, BRIGHTNESS_MIN_VAL, BRIGHTNESS_MAX_VAL));
}

int brightness_manager_process_lux(rt_int32_t lux) {
    rt_uint8_t raw_target;
    rt_uint8_t next;

    if (!g_inited && brightness_manager_init() != RT_EOK) return -RT_ERROR;
    if (!g_auto_brightness) return RT_EOK;

    if (g_auto_lux_filter_reset || g_auto_lux_ema < 0) {
        g_auto_lux_ema = lux;
        g_auto_hyst_target = g_brightness_now;
        g_auto_lux_filter_reset = RT_FALSE;
    } else {
        g_auto_lux_ema =
            g_auto_lux_ema + ((lux - g_auto_lux_ema) >> AUTO_LUX_EMA_SHIFT);
    }

    raw_target = lux_to_brightness_target(g_auto_lux_ema);
    g_auto_hyst_target =
        auto_target_apply_hysteresis(raw_target, g_auto_hyst_target);
    next = auto_brightness_ramp_step(g_brightness_now, g_auto_hyst_target);
    if (next == g_brightness_now) return RT_EOK;

    g_brightness_now = next;
    brightness_apply_to_lcd(g_brightness_now);
    brightness_notify(g_brightness_now, g_auto_brightness);
    return RT_EOK;
}

int brightness_manager_register_callback(brightness_manager_callback_t callback,
                                         void *user_data) {
    rt_size_t i;
    rt_size_t free_idx = BRIGHTNESS_MANAGER_MAX_CALLBACKS;

    if (!callback) return -RT_EINVAL;

    for (i = 0; i < BRIGHTNESS_MANAGER_MAX_CALLBACKS; i++) {
        if (g_callbacks[i].callback == callback &&
            g_callbacks[i].user_data == user_data) {
            callback(g_brightness_now, g_auto_brightness, user_data);
            return RT_EOK;
        }
        if (!g_callbacks[i].callback &&
            free_idx == BRIGHTNESS_MANAGER_MAX_CALLBACKS) {
            free_idx = i;
        }
    }

    if (free_idx == BRIGHTNESS_MANAGER_MAX_CALLBACKS) return -RT_ERROR;

    g_callbacks[free_idx].callback = callback;
    g_callbacks[free_idx].user_data = user_data;
    callback(g_brightness_now, g_auto_brightness, user_data);
    return RT_EOK;
}

int brightness_manager_unregister_callback(brightness_manager_callback_t callback,
                                           void *user_data) {
    rt_size_t i;
    if (!callback) return -RT_EINVAL;

    for (i = 0; i < BRIGHTNESS_MANAGER_MAX_CALLBACKS; i++) {
        if (g_callbacks[i].callback == callback &&
            g_callbacks[i].user_data == user_data) {
            g_callbacks[i].callback = RT_NULL;
            g_callbacks[i].user_data = RT_NULL;
            return RT_EOK;
        }
    }

    return -RT_ERROR;
}

int brightness_manager_register_lux_callback(
    brightness_manager_lux_callback_t callback, void *user_data) {
    rt_size_t i;
    rt_size_t free_idx = BRIGHTNESS_MANAGER_MAX_LUX_CALLBACKS;

    if (!callback) return -RT_EINVAL;

    for (i = 0; i < BRIGHTNESS_MANAGER_MAX_LUX_CALLBACKS; i++) {
        if (g_lux_callbacks[i].callback == callback &&
            g_lux_callbacks[i].user_data == user_data) {
            callback(g_latest_lux, user_data);
            return RT_EOK;
        }
        if (!g_lux_callbacks[i].callback &&
            free_idx == BRIGHTNESS_MANAGER_MAX_LUX_CALLBACKS) {
            free_idx = i;
        }
    }

    if (free_idx == BRIGHTNESS_MANAGER_MAX_LUX_CALLBACKS) return -RT_ERROR;

    g_lux_callbacks[free_idx].callback = callback;
    g_lux_callbacks[free_idx].user_data = user_data;
    callback(g_latest_lux, user_data);
    return RT_EOK;
}

int brightness_manager_unregister_lux_callback(
    brightness_manager_lux_callback_t callback, void *user_data) {
    rt_size_t i;
    if (!callback) return -RT_EINVAL;

    for (i = 0; i < BRIGHTNESS_MANAGER_MAX_LUX_CALLBACKS; i++) {
        if (g_lux_callbacks[i].callback == callback &&
            g_lux_callbacks[i].user_data == user_data) {
            g_lux_callbacks[i].callback = RT_NULL;
            g_lux_callbacks[i].user_data = RT_NULL;
            return RT_EOK;
        }
    }

    return -RT_ERROR;
}
