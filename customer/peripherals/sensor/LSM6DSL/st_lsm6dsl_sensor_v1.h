
#ifndef SENSOR_ST_LSM6DSL_H__
#define SENSOR_ST_LSM6DSL_H__


#include <rtthread.h>
#include <rtdevice.h>
#include "board.h"
#include "sensor.h"

#if defined(RT_VERSION_CHECK)
    #if (RTTHREAD_VERSION >= RT_VERSION_CHECK(5, 0, 2))
        #define RT_SIZE_TYPE   rt_ssize_t
    #else
        #define RT_SIZE_TYPE   rt_size_t
    #endif
#else
    #define RT_SIZE_TYPE   rt_size_t
#endif
#include "lsm6dsl.h"

#ifdef __cplusplus
extern "C" {
#endif


#define LSM6DSL_ADDR_DEFAULT (0x6A)

int rt_hw_lsm6dsl_init(const char *name, struct rt_sensor_config *cfg);

/* Gyro zero-rate bias in mdps, subtracted in driver after LSM6DSL_GYRO_GetAxes. */
void lsm6dsl_gyro_bias_set(int32_t bx, int32_t by, int32_t bz);
void lsm6dsl_gyro_bias_get(int32_t *bx, int32_t *by, int32_t *bz);
void lsm6dsl_gyro_bias_clear(void);
/**
 * Average gyro at rest: enables gyro, discards warm-up samples, then averages @p samples
 * with @p delay_ms between reads. Store result as bias (mdps).
 */
rt_err_t lsm6dsl_gyro_bias_calibrate(rt_uint16_t samples, rt_uint16_t delay_ms);

#ifdef __cplusplus
}
#endif
#endif
