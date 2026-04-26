#include "charge_manager.h"

#include "charge.h"

#define DBG_TAG "dm.charge"
#define DBG_LVL DBG_INFO
#include <rtdbg.h>

#define clamp(val, min, max) \
    ((val) < (min) ? (min) : ((val) > (max) ? (max) : (val)))

#define CHARGE_MANAGER_MAX_CALLBACKS 4

typedef struct {
    charge_manager_callback_t callback;
    void *user_data;
} charge_manager_callback_slot_t;

static rt_bool_t g_inited = RT_FALSE;
static rt_uint16_t g_current_ma = CHARGE_MANAGER_CURRENT_DEFAULT_MA;
static charge_manager_callback_slot_t g_callbacks[CHARGE_MANAGER_MAX_CALLBACKS];

static rt_uint16_t charge_manager_normalize_current(rt_uint16_t current_ma)
{
    rt_uint16_t clamped = (rt_uint16_t)clamp(
        current_ma, CHARGE_MANAGER_CURRENT_MIN_MA, CHARGE_MANAGER_CURRENT_MAX_MA);
    rt_uint16_t step = CHARGE_MANAGER_CURRENT_STEP_MA;
    rt_uint16_t rem = clamped % step;

    if (rem == 0)
        return clamped;

    if (rem >= (step / 2)) {
        clamped = (rt_uint16_t)(clamped + (step - rem));
    } else {
        clamped = (rt_uint16_t)(clamped - rem);
    }

    return (rt_uint16_t)clamp(
        clamped, CHARGE_MANAGER_CURRENT_MIN_MA, CHARGE_MANAGER_CURRENT_MAX_MA);
}

static void charge_manager_notify(rt_uint16_t current_ma)
{
    rt_size_t i;
    for (i = 0; i < CHARGE_MANAGER_MAX_CALLBACKS; i++) {
        if (g_callbacks[i].callback) {
            g_callbacks[i].callback(current_ma, g_callbacks[i].user_data);
        }
    }
}

static int charge_manager_init(void)
{
    if (g_inited)
        return RT_EOK;

    g_current_ma =
        charge_manager_normalize_current(CHARGE_MANAGER_CURRENT_DEFAULT_MA);
    g_inited = RT_TRUE;
    return RT_EOK;
}

rt_uint16_t charge_manager_get_current_ma(void)
{
    return g_current_ma;
}

int charge_manager_set_current_ma(rt_uint16_t current_ma)
{
    rt_charge_err_t ret;
    rt_uint16_t target;

    if (!g_inited && charge_manager_init() != RT_EOK)
        return -RT_ERROR;

    target = charge_manager_normalize_current(current_ma);
    if (target == g_current_ma)
        return RT_EOK;

    ret = rt_charge_set_cc_current(target);
    if (ret != RT_CHARGE_EOK) {
        LOG_E("set cc current failed: %d, target=%d", ret, target);
        return -RT_ERROR;
    }

    g_current_ma = target;
    charge_manager_notify(g_current_ma);
    return RT_EOK;
}

int charge_manager_adjust_current(rt_int32_t delta_ma)
{
    rt_int32_t target = (rt_int32_t)g_current_ma + delta_ma;
    return charge_manager_set_current_ma((rt_uint16_t)target);
}

int charge_manager_set_ship_mode(rt_bool_t enable)
{
    rt_charge_err_t ret = rt_charge_set_ship_mode(enable ? 1 : 0);
    if (ret != RT_CHARGE_EOK) {
        LOG_E("set ship mode failed: %d", ret);
        return -RT_ERROR;
    }
    return RT_EOK;
}

int charge_manager_register_callback(charge_manager_callback_t callback,
                                     void *user_data)
{
    rt_size_t i;
    rt_size_t free_idx = CHARGE_MANAGER_MAX_CALLBACKS;

    if (!callback)
        return -RT_EINVAL;

    if (!g_inited && charge_manager_init() != RT_EOK)
        return -RT_ERROR;

    for (i = 0; i < CHARGE_MANAGER_MAX_CALLBACKS; i++) {
        if (g_callbacks[i].callback == callback &&
            g_callbacks[i].user_data == user_data) {
            callback(g_current_ma, user_data);
            return RT_EOK;
        }
        if (!g_callbacks[i].callback && free_idx == CHARGE_MANAGER_MAX_CALLBACKS) {
            free_idx = i;
        }
    }

    if (free_idx == CHARGE_MANAGER_MAX_CALLBACKS)
        return -RT_ERROR;

    g_callbacks[free_idx].callback = callback;
    g_callbacks[free_idx].user_data = user_data;
    callback(g_current_ma, user_data);
    return RT_EOK;
}

int charge_manager_unregister_callback(charge_manager_callback_t callback,
                                       void *user_data)
{
    rt_size_t i;

    if (!callback)
        return -RT_EINVAL;

    for (i = 0; i < CHARGE_MANAGER_MAX_CALLBACKS; i++) {
        if (g_callbacks[i].callback == callback &&
            g_callbacks[i].user_data == user_data) {
            g_callbacks[i].callback = RT_NULL;
            g_callbacks[i].user_data = RT_NULL;
            return RT_EOK;
        }
    }

    return -RT_ERROR;
}

static int charge_manager_auto_init(void)
{
    return charge_manager_init();
}
INIT_COMPONENT_EXPORT(charge_manager_auto_init);
