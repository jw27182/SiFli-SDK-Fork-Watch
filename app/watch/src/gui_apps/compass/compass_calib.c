#include "compass_calib.h"

#include <math.h>
#include <string.h>

#ifndef COMPASS_CALIB_MIN_SPAN_UT
#define COMPASS_CALIB_MIN_SPAN_UT 12.0f
#endif

#ifndef COMPASS_CALIB_MIN_SAMPLES
#define COMPASS_CALIB_MIN_SAMPLES 120u
#endif

static float _clampf(float x, float lo, float hi)
{
    if (x < lo) return lo;
    if (x > hi) return hi;
    return x;
}

void compass_calib_runtime_reset(compass_calib_runtime_t *st)
{
    if (!st) return;
    memset(st, 0, sizeof(*st));
}

void compass_calib_runtime_add_sample(compass_calib_runtime_t *st, float x_uT, float y_uT)
{
    float cx, cy, vx, vy, ang;
    int bin;

    if (!st) return;
    if (!isfinite(x_uT) || !isfinite(y_uT)) return;

    if (!st->initialized)
    {
        st->initialized = RT_TRUE;
        st->min_x = st->max_x = x_uT;
        st->min_y = st->max_y = y_uT;
    }
    else
    {
        if (x_uT < st->min_x) st->min_x = x_uT;
        if (x_uT > st->max_x) st->max_x = x_uT;
        if (y_uT < st->min_y) st->min_y = y_uT;
        if (y_uT > st->max_y) st->max_y = y_uT;
    }

    st->sample_count++;
    st->sum_x += x_uT;
    st->sum_y += y_uT;
    st->sum_xx += x_uT * x_uT;
    st->sum_xy += x_uT * y_uT;
    st->sum_yy += y_uT * y_uT;

    /*
     * 覆盖率用“去中心后角度”，避免大硬铁偏置时 atan2(y, x) 长期卡在 45/135/...，
     * 导致校准进度假性停在低百分比（如约 11%）。
     */
    cx = 0.5f * (st->min_x + st->max_x);
    cy = 0.5f * (st->min_y + st->max_y);
    vx = x_uT - cx;
    vy = y_uT - cy;
    if ((vx * vx + vy * vy) > 1e-4f)
    {
        ang = atan2f(vy, vx) * 180.0f / (float)M_PI;
        if (ang < 0.f) ang += 360.f;
        bin = (int)(ang / 10.0f);
        if (bin < 0) bin = 0;
        if (bin > 35) bin = 35;
        st->angle_bins |= ((uint64_t)1u << (uint32_t)bin);
    }
}

float compass_calib_runtime_coverage(const compass_calib_runtime_t *st)
{
    uint32_t cnt = 0;
    uint64_t bits;
    if (!st) return 0.0f;
    bits = st->angle_bins;
    while (bits)
    {
        bits &= (bits - 1u);
        cnt++;
    }
    return (float)cnt / 36.0f;
}

rt_bool_t compass_calib_params_valid(const compass_calib_params_t *p)
{
    if (!p) return RT_FALSE;
    if (p->magic != COMPASS_CALIB_MAGIC || p->version != COMPASS_CALIB_VERSION) return RT_FALSE;
    if (p->sample_count < COMPASS_CALIB_MIN_SAMPLES) return RT_FALSE;
    if (!isfinite(p->mat00) || !isfinite(p->mat01) || !isfinite(p->mat10) || !isfinite(p->mat11)) return RT_FALSE;
    if (!isfinite(p->bias_x) || !isfinite(p->bias_y)) return RT_FALSE;
    if (p->quality < 0.35f) return RT_FALSE;
    return RT_TRUE;
}

rt_bool_t compass_calib_runtime_finalize(const compass_calib_runtime_t *st, compass_calib_params_t *out)
{
    float mean_x, mean_y, cov_xx, cov_xy, cov_yy;
    float trace, det, disc, lam1, lam2;
    float vx, vy, nrm;
    float s1, s2, q_cov, q_span, q_cover, quality;
    float span_x, span_y;

    if (!st || !out || !st->initialized) return RT_FALSE;
    if (st->sample_count < COMPASS_CALIB_MIN_SAMPLES) return RT_FALSE;

    span_x = st->max_x - st->min_x;
    span_y = st->max_y - st->min_y;
    if (span_x < COMPASS_CALIB_MIN_SPAN_UT || span_y < COMPASS_CALIB_MIN_SPAN_UT) return RT_FALSE;

    mean_x = st->sum_x / (float)st->sample_count;
    mean_y = st->sum_y / (float)st->sample_count;
    cov_xx = st->sum_xx / (float)st->sample_count - mean_x * mean_x;
    cov_xy = st->sum_xy / (float)st->sample_count - mean_x * mean_y;
    cov_yy = st->sum_yy / (float)st->sample_count - mean_y * mean_y;

    trace = cov_xx + cov_yy;
    det = cov_xx * cov_yy - cov_xy * cov_xy;
    disc = trace * trace - 4.0f * det;
    if (disc < 0.0f) disc = 0.0f;
    disc = sqrtf(disc);
    lam1 = 0.5f * (trace + disc);
    lam2 = 0.5f * (trace - disc);
    if (lam1 < 1e-4f || lam2 < 1e-4f) return RT_FALSE;
    if (lam2 > lam1)
    {
        float t = lam1;
        lam1 = lam2;
        lam2 = t;
    }

    /* 主特征向量 */
    vx = cov_xy;
    vy = lam1 - cov_xx;
    nrm = sqrtf(vx * vx + vy * vy);
    if (nrm < 1e-6f)
    {
        vx = 1.0f;
        vy = 0.0f;
    }
    else
    {
        vx /= nrm;
        vy /= nrm;
    }

    /* 椭圆白化近似：按主轴缩放，再旋回原坐标 */
    {
        float k = sqrtf((lam1 + lam2) * 0.5f);
        s1 = k / sqrtf(lam1);
        s2 = k / sqrtf(lam2);

        out->mat00 = s1 * vx * vx + s2 * vy * vy;
        out->mat01 = (s1 - s2) * vx * vy;
        out->mat10 = out->mat01;
        out->mat11 = s1 * vy * vy + s2 * vx * vx;
    }

    /* 硬铁中心取 min/max 中心，比均值更抗“只转了半圈”偏差 */
    out->bias_x = 0.5f * (st->min_x + st->max_x);
    out->bias_y = 0.5f * (st->min_y + st->max_y);

    q_cov = _clampf(lam2 / lam1, 0.0f, 1.0f);
    q_span = _clampf(fminf(span_x, span_y) / (2.0f * COMPASS_CALIB_MIN_SPAN_UT), 0.0f, 1.0f);
    q_cover = _clampf(compass_calib_runtime_coverage(st), 0.0f, 1.0f);
    quality = 0.40f * q_cover + 0.35f * q_cov + 0.25f * q_span;

    out->magic = COMPASS_CALIB_MAGIC;
    out->version = COMPASS_CALIB_VERSION;
    out->quality = quality;
    out->sample_count = st->sample_count;
    return compass_calib_params_valid(out);
}

void compass_calib_apply(const compass_calib_params_t *p, float x_uT, float y_uT, float *ox_uT, float *oy_uT)
{
    float dx, dy;
    if (!ox_uT || !oy_uT)
    {
        return;
    }
    if (!compass_calib_params_valid(p))
    {
        *ox_uT = x_uT;
        *oy_uT = y_uT;
        return;
    }
    dx = x_uT - p->bias_x;
    dy = y_uT - p->bias_y;
    *ox_uT = p->mat00 * dx + p->mat01 * dy;
    *oy_uT = p->mat10 * dx + p->mat11 * dy;
}
