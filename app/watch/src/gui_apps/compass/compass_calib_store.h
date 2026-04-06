#pragma once

#include <rtthread.h>

#include "compass_calib.h"

/* 临时诊断日志（load/save/on_start）；稳定后改为 0 */
#ifndef COMPASS_CAL_STORE_LOG
#define COMPASS_CAL_STORE_LOG 1
#endif

/* share_prefs 二进制块读写；需 CONFIG_BSP_SHARE_PREFS */

rt_bool_t compass_calib_store_load(compass_calib_params_t *out);
rt_err_t compass_calib_store_save(const compass_calib_params_t *in);
void compass_calib_store_clear(void);
