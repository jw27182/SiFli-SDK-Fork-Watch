#include "st_lsm6dsl_sensor_v1.h"
#include "sensor_liteon_ltr303.h"
#include "battery_calculator.h"

battery_calculator_t battery_calc;

static int battery_init(void)
{
    static battery_calculator_config_t calc_config = {
        .charging_table = charging_curve_table,
        .discharging_table = discharge_curve_table,
        .charge_filter_threshold = 50,     // 充电时50mV阈值
        .discharge_filter_threshold = 30,  // 放电时30mV阈值
        .filter_count = 3,                 // 需要3次确认
        .secondary_filter_enabled = true,  // 启用二级滤波
        .secondary_filter_weight_pre = 90, // 前电压权重90%
        .secondary_filter_weight_cur = 10  // 当前电压权重10%
    };

    calc_config.charging_table_size = charging_curve_table_size;
    calc_config.discharging_table_size = discharge_curve_table_size;
    
    return battery_calculator_init(&battery_calc, &calc_config);
}

static int lsm6dsl_init(void)
{
    struct rt_sensor_config cfg;
    cfg.intf.dev_name = "i2c3";
    cfg.intf.user_data = (void *)LSM6DSL_ADDR_DEFAULT;
    cfg.irq_pin.pin = RT_PIN_NONE;
    return rt_hw_lsm6dsl_init("lsm6dsl", &cfg);
}

static int ltr303_init(void)
{
    struct rt_sensor_config cfg = {
        .intf.dev_name = "i2c3",
    };
    return rt_hw_ltr303_init("ltr303", &cfg);
}

INIT_DEVICE_EXPORT(battery_init);
INIT_DEVICE_EXPORT(lsm6dsl_init);
INIT_DEVICE_EXPORT(ltr303_init);