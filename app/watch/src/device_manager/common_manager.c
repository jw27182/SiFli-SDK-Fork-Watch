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

#ifdef MAG_USING_MMC56X3
#include "sensor_memsic_mmc56x3.h"

static int mmc56x3_sensor_register_init(void)
{
    struct rt_sensor_config cfg = {0};

    cfg.intf.dev_name = "i2c3";
    cfg.irq_pin.pin = RT_PIN_NONE;

    /* 注册名 mmc56x3 → 设备名 mag_mmc56x3（见 rt_hw_sensor_register 前缀规则） */
    return rt_hw_mmc56x3_init("mmc56x3", &cfg);
}

INIT_DEVICE_EXPORT(mmc56x3_sensor_register_init);
#endif

#ifdef GPS_USING_ATGM336H
#include "um_gps_if.h"

static int gps_init(void)
{
    return um_gps_init();
}

INIT_DEVICE_EXPORT(gps_init);
#endif
