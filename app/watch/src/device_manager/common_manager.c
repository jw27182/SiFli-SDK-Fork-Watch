#include "st_lsm6dsl_sensor_v1.h"
#include "sensor_liteon_ltr303.h"

static int lsm6dsl_init(void)
{
    struct rt_sensor_config cfg;
    cfg.intf.dev_name = "i2c3";
    cfg.intf.user_data = (void *)LSM6DSL_ADDR_DEFAULT;
    cfg.irq_pin.pin = RT_PIN_NONE;
    return rt_hw_lsm6dsl_init("lsm6dsl", &cfg);
}

INIT_DEVICE_EXPORT(lsm6dsl_init);

static int ltr303_init(void)
{
    struct rt_sensor_config cfg = {
        .intf.dev_name = "i2c3",
    };
    return rt_hw_ltr303_init("ltr303", &cfg);
}

INIT_DEVICE_EXPORT(ltr303_init);