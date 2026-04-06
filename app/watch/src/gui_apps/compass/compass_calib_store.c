/*
 * 校准参数持久化：与手表工程一致，使用 middleware share_prefs（依赖 CONFIG_BSP_SHARE_PREFS）。
 * 底层通常为 FlashDB KV；proj.conf 需 CONFIG_BSP_SHARE_PREFS=y。
 */
#include "compass_calib_store.h"

#include <string.h>

#include <rtthread.h>
#include "share_prefs.h"

#define COMPASS_PREF_NAME "compass"
#define COMPASS_PREF_KEY  "calib_v1"

#if COMPASS_CAL_STORE_LOG
#define CS_LOG(...) rt_kprintf("[compass-store] " __VA_ARGS__)
#else
#define CS_LOG(...) ((void)0)
#endif

static share_prefs_t *_open_compass_pref(void)
{
    return share_prefs_open(COMPASS_PREF_NAME, SHAREPREFS_MODE_PRIVATE);
}

rt_bool_t compass_calib_store_load(compass_calib_params_t *out)
{
    share_prefs_t *pf;
    int32_t n;
    if (!out) {
        return RT_FALSE;
    }
    memset(out, 0, sizeof(*out));

    pf = _open_compass_pref();
    if (!pf) {
        CS_LOG("load: share_prefs_open failed\n");
        return RT_FALSE;
    }

    n = share_prefs_get_block(pf, COMPASS_PREF_KEY, out, sizeof(*out));
    share_prefs_close(pf);
    if (n != (int32_t)sizeof(*out)) {
        CS_LOG("load: get_block n=%d want=%u (0=no key/wrong len)\n", (int)n,
               (unsigned)sizeof(*out));
        return RT_FALSE;
    }
    if (!compass_calib_params_valid(out)) {
        CS_LOG("load: invalid magic=0x%08lx ver=0x%08lx q=%.3f sc=%lu\n",
               (unsigned long)out->magic, (unsigned long)out->version, (double)out->quality,
               (unsigned long)out->sample_count);
        return RT_FALSE;
    }
    CS_LOG("load ok q=%.2f sc=%lu\n", (double)out->quality, (unsigned long)out->sample_count);
    return RT_TRUE;
}

rt_err_t compass_calib_store_save(const compass_calib_params_t *in)
{
    share_prefs_t *pf;
    rt_err_t ret;
    if (!in || !compass_calib_params_valid(in)) {
        CS_LOG("save: skip, invalid arg\n");
        return -RT_EINVAL;
    }

    pf = _open_compass_pref();
    if (!pf) {
        CS_LOG("save: share_prefs_open failed\n");
        return -RT_ERROR;
    }

    ret = share_prefs_set_block(pf, COMPASS_PREF_KEY, in, sizeof(*in));
    share_prefs_close(pf);
    if (ret != RT_EOK) {
        CS_LOG("save: set_block err=%d\n", (int)ret);
    } else {
        CS_LOG("save ok bytes=%u q=%.2f\n", (unsigned)sizeof(*in), (double)in->quality);
    }
    return ret;
}

void compass_calib_store_clear(void)
{
    share_prefs_t *pf = _open_compass_pref();
    if (!pf) return;
    share_prefs_remove(pf, COMPASS_PREF_KEY);
    share_prefs_close(pf);
}
