#include "rtconfig.h"

#ifdef ACC_USING_LSM6DSL
#include "st_lsm6dsl_sensor_v1.h"

static int lsm6dsl_init(void)
{
    struct rt_sensor_config cfg;
    cfg.intf.dev_name = "i2c3";
    cfg.intf.user_data = (void *)LSM6DSL_ADDR_DEFAULT;
    cfg.irq_pin.pin = RT_PIN_NONE;
    return rt_hw_lsm6dsl_init("lsm6dsl", &cfg);
}

INIT_DEVICE_EXPORT(lsm6dsl_init);
#endif

#ifdef ASL_USING_LTR303
#include "sensor_liteon_ltr303.h"

static int ltr303_init(void)
{
    struct rt_sensor_config cfg = {
        .intf.dev_name = "i2c3",
    };
    return rt_hw_ltr303_init("ltr303", &cfg);
}

INIT_DEVICE_EXPORT(ltr303_init);
#endif

#if defined(HUMITURE_USING_AHT20)
#include "sensor_aht20.h"

static int aht20_sensor_register_init(void)
{
    struct rt_sensor_config cfg = {0};

    cfg.intf.dev_name = "i2c3";
    cfg.intf.user_data = (void *)0x38;

    return rt_hw_aht20_init("aht20", &cfg);
}

INIT_DEVICE_EXPORT(aht20_sensor_register_init);
#endif

/* GH3018：不在此启动心率测量，仅在「心率」应用 on_start 中开启，on_pause/on_stop 关闭 */
