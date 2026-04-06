#pragma once

#include <rtthread.h>
#include <stdint.h>

#define COMPASS_CALIB_MAGIC   0x434D4342u /* CMCB */
#define COMPASS_CALIB_VERSION 0x00010001u

typedef struct
{
    uint32_t magic;
    uint32_t version;
    float bias_x;
    float bias_y;
    float mat00;
    float mat01;
    float mat10;
    float mat11;
    float quality;
    uint32_t sample_count;
} compass_calib_params_t;

typedef struct
{
    rt_bool_t initialized;
    uint32_t sample_count;
    float sum_x;
    float sum_y;
    float sum_xx;
    float sum_xy;
    float sum_yy;
    float min_x;
    float max_x;
    float min_y;
    float max_y;
    uint64_t angle_bins; /* 36 bins: 每 10 度一个 */
} compass_calib_runtime_t;

void compass_calib_runtime_reset(compass_calib_runtime_t *st);
void compass_calib_runtime_add_sample(compass_calib_runtime_t *st, float x_uT, float y_uT);
float compass_calib_runtime_coverage(const compass_calib_runtime_t *st);
rt_bool_t compass_calib_runtime_finalize(const compass_calib_runtime_t *st, compass_calib_params_t *out);

rt_bool_t compass_calib_params_valid(const compass_calib_params_t *p);
void compass_calib_apply(const compass_calib_params_t *p, float x_uT, float y_uT, float *ox_uT, float *oy_uT);
