#include <math.h>
#include <rtdevice.h>
#include <rtthread.h>

#include "app_mem.h"
#include "gui_app_fwk.h"
#include "lv_ext_resource_manager.h"
#include "lvgl.h"
#include "lvsf_comp.h"
#include "sensor.h"
#include "compass_calib.h"
#include "compass_calib_store.h"

#define MAG_DEV_NAME "mag_mmc56x3"
#define ACCE_DEV_NAME "acce_lsm6dsl"
#define TICK_MS       50
#define POINTER_IMG_WH 288
#define POINTER_PIVOT  (POINTER_IMG_WH / 2)

#ifndef COMPASS_ANGLE_SMOOTH
#define COMPASS_ANGLE_SMOOTH 0.25f
#endif
#ifndef COMPASS_AUTO_CAL_MIN_COVERAGE
#define COMPASS_AUTO_CAL_MIN_COVERAGE 0.70f
#endif
#ifndef COMPASS_AUTO_CAL_MIN_SAMPLES
#define COMPASS_AUTO_CAL_MIN_SAMPLES 220u
#endif
#ifndef COMPASS_MIN_VEC_NORM_UT
#define COMPASS_MIN_VEC_NORM_UT 2.0f
#endif
#ifndef COMPASS_DIAG_LOG_EVERY
#define COMPASS_DIAG_LOG_EVERY 0
#endif
#ifndef COMPASS_CAL_LOG_EVERY
#define COMPASS_CAL_LOG_EVERY 10
#endif
#ifndef COMPASS_HEADING_OFFSET_DEG
#define COMPASS_HEADING_OFFSET_DEG 0.f
#endif
#ifndef COMPASS_MAG_CHIP_MOUNT_ROTATE_DEG
#define COMPASS_MAG_CHIP_MOUNT_ROTATE_DEG 180.f
#endif
/* 仅指针：图源与磁航向零点不一致时加减（度） */
#ifndef COMPASS_NEEDLE_IMG_OFFSET_DEG
#define COMPASS_NEEDLE_IMG_OFFSET_DEG 0.f
#endif
/* 仅标签：东西对调(显示东↔西、南北不变)时用 norm360(360°-θ)。指针不受此项影响 */
#ifndef COMPASS_LABEL_MIRROR_EW
#define COMPASS_LABEL_MIRROR_EW 1
#endif
#ifndef COMPASS_DBG
#define COMPASS_DBG 0
#endif
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

LV_IMG_DECLARE(img_compass);
LV_IMG_DECLARE(img_compass_pointer_diagram);

#if COMPASS_DBG
#define CMP_LOG(...) rt_kprintf("[compass] " __VA_ARGS__)
#else
#define CMP_LOG(...) ((void)0)
#endif

static rt_device_t s_mag, s_acce;
static lv_obj_t *s_needle, *s_lbl_deg, *s_lbl_dir, *s_btn_cal, *s_btn_cal_txt;
static lv_timer_t *s_timer;

/* 平滑后的磁航向（供指针）；标签在 apply_compass_ui 内可再镜像 */
static float s_heading;
static bool s_heading_ok, s_mag_ok, s_mag_we_opened, s_acce_ok, s_acce_we_opened;
static bool s_warn_interference, s_calibrating;
#if COMPASS_DBG
static rt_uint32_t s_read_fail_streak;
#endif
#if COMPASS_DIAG_LOG_EVERY > 0
static rt_uint32_t s_diag_seq;
#endif
#if COMPASS_CAL_LOG_EVERY > 0
static rt_uint32_t s_cal_log_seq;
#endif

static compass_calib_runtime_t s_runtime_cal, s_runtime_bg;
static compass_calib_params_t s_calib_saved, s_calib_live;
static bool s_has_saved, s_has_live;

static const char *s_dir_zh[] = {
    "北", "东北", "东", "东南", "南", "西南", "西", "西北",
};

static float norm360(float a) {
    a = fmodf(a, 360.f);
    return a < 0.f ? a + 360.f : a;
}

/* 校准后水平面磁力 (vx,vy) → 界面磁航向（北 0°、顺时针） */
static float heading_from_horizontal_field(float vx, float vy) {
    float plane_deg = atan2f(vy, vx) * (180.f / (float)M_PI);
    return norm360(90.f - plane_deg + COMPASS_HEADING_OFFSET_DEG +
                   COMPASS_MAG_CHIP_MOUNT_ROTATE_DEG);
}

static int32_t ui_deg_to_lv_angle_01(float deg) {
    int32_t a = (int32_t)(deg * 10.f + (deg >= 0.f ? 0.5f : -0.5f));
    a %= 3600;
    if (a < 0) {
        a += 3600;
    }
    return a;
}

/* 标签：可选镜像东西；指针：heading_deg + COMPASS_NEEDLE_IMG_OFFSET_DEG */
static void apply_compass_ui(float heading_deg) {
    int32_t a_lbl, a_needle;
    int ideg, idx;
    char buf[24];
    float h_lbl = heading_deg;

#if COMPASS_LABEL_MIRROR_EW
    h_lbl = norm360(360.f - heading_deg);
#endif

    if (s_needle == NULL) {
        return;
    }
    a_lbl = ui_deg_to_lv_angle_01(h_lbl);
    a_needle = ui_deg_to_lv_angle_01(norm360(heading_deg + COMPASS_NEEDLE_IMG_OFFSET_DEG));
    lv_img_set_angle(s_needle, (int16_t)a_needle);
    if (s_lbl_deg == NULL || s_lbl_dir == NULL) {
        return;
    }
    ideg = (int)((a_lbl + 5) / 10);
    if (ideg >= 360) {
        ideg -= 360;
    }
    rt_snprintf(buf, sizeof(buf), "%d度", ideg);
    lv_label_set_text(s_lbl_deg, buf);
    idx = ((ideg + 22) / 45) % 8;
    lv_label_set_text(s_lbl_dir, s_dir_zh[idx]);
}

static void labels_set_no_sensor(void) {
    lv_label_set_text(s_lbl_deg, "--°");
    lv_label_set_text(s_lbl_dir, "无磁力计");
}

static rt_bool_t compute_tilt_compensated_xy(float mx, float my, float mz,
                                             float *ox, float *oy, float *tilt_deg) {
    struct rt_sensor_data a;
    float ax, ay, az, an, roll, pitch, sr, cr, sp, cp, hx, hy;

    if (!ox || !oy || !s_acce_ok || s_acce == RT_NULL) {
        return RT_FALSE;
    }
    if (rt_device_read(s_acce, 0, &a, 1) < 1) {
        return RT_FALSE;
    }

    ax = (float)a.data.acce.x;
    ay = (float)a.data.acce.y;
    az = (float)a.data.acce.z;
    an = sqrtf(ax * ax + ay * ay + az * az);
    if (an < 1e-3f || !isfinite(an)) {
        return RT_FALSE;
    }
    ax /= an;
    ay /= an;
    az /= an;

    roll = atan2f(ay, az);
    pitch = atan2f(-ax, sqrtf(ay * ay + az * az));
    sr = sinf(roll);
    cr = cosf(roll);
    sp = sinf(pitch);
    cp = cosf(pitch);
    hx = mx * cp + mz * sp;
    hy = mx * sr * sp + my * cr - mz * sr * cp;
    if (!isfinite(hx) || !isfinite(hy)) {
        return RT_FALSE;
    }
    *ox = hx;
    *oy = hy;
    if (tilt_deg) {
        *tilt_deg = acosf(fmaxf(-1.f, fminf(1.f, az))) * (180.f / (float)M_PI);
    }
    return RT_TRUE;
}

static void calib_btn_refresh(void) {
    const char *t;

    if (s_btn_cal_txt == NULL) {
        return;
    }
    if (s_calibrating) {
        t = "结束校准";
    } else if ((s_has_saved && compass_calib_params_valid(&s_calib_saved)) ||
               (s_has_live && compass_calib_params_valid(&s_calib_live))) {
        t = "重新校准";
    } else {
        t = "开始校准";
    }
    lv_label_set_text(s_btn_cal_txt, t);
}

static void cal_btn_apply_style(lv_obj_t *btn) {
    lv_obj_set_style_radius(btn, 22, LV_PART_MAIN);
    lv_obj_set_style_bg_color(btn, lv_color_hex(0x333333), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(btn, 0, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(btn, 0, LV_PART_MAIN);
    lv_obj_set_style_outline_width(btn, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_hor(btn, 20, LV_PART_MAIN);
    lv_obj_set_style_pad_ver(btn, 10, LV_PART_MAIN);
    lv_obj_set_style_bg_color(btn, lv_color_hex(0x444444), LV_PART_MAIN | LV_STATE_PRESSED);
    lv_obj_set_style_transform_width(btn, -4, LV_STATE_PRESSED);
    lv_obj_set_style_transform_height(btn, -3, LV_STATE_PRESSED);
}

static void compass_runtime_reset(void) {
    s_heading_ok = RT_FALSE;
    s_warn_interference = RT_FALSE;
    compass_calib_runtime_reset(&s_runtime_bg);
}

static void calib_mode_enter(void) {
    s_calibrating = true;
    compass_calib_runtime_reset(&s_runtime_cal);
    lv_label_set_text(s_lbl_deg, "0%");
    lv_label_set_text(s_lbl_dir, "任意姿态慢转一圈");
    calib_btn_refresh();
}

static void calib_mode_finish(bool auto_finish) {
    compass_calib_params_t out = {0};

    if (compass_calib_runtime_finalize(&s_runtime_cal, &out)) {
        s_calib_saved = out;
        s_has_saved = true;
        s_has_live = false;
        lv_label_set_text(s_lbl_deg, "OK");
        if (compass_calib_store_save(&s_calib_saved) != RT_EOK) {
            lv_label_set_text(s_lbl_dir,
                              auto_finish ? "校准完成,存储失败" : "已保存,存储失败");
        } else {
            lv_label_set_text(s_lbl_dir, auto_finish ? "校准完成" : "已保存校准");
        }
    } else {
        lv_label_set_text(s_lbl_deg, "FAIL");
        lv_label_set_text(s_lbl_dir, "校准失败,请重试");
    }
    s_calibrating = false;
    calib_btn_refresh();
    compass_runtime_reset();
}

static void calib_btn_event_cb(lv_event_t *e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) {
        return;
    }
    if (!s_calibrating) {
        calib_mode_enter();
    } else {
        calib_mode_finish(false);
    }
}

static const compass_calib_params_t *get_working_calib(void) {
    if (s_has_saved && compass_calib_params_valid(&s_calib_saved)) {
        return &s_calib_saved;
    }
    if (s_has_live && compass_calib_params_valid(&s_calib_live)) {
        return &s_calib_live;
    }
    return RT_NULL;
}

static void tick_cb(lv_timer_t *timer) {
    struct rt_sensor_data d;
    float mx, my, mz, mx_use, my_use, vx, vy, r_new, dlt;
    const compass_calib_params_t *p;
#if COMPASS_CAL_LOG_EVERY > 0
    float tilt_deg = 0.f;
#endif

    (void)timer;
    if (!s_mag_ok || s_mag == RT_NULL) {
        return;
    }
    if (rt_device_read(s_mag, 0, &d, 1) < 1) {
#if COMPASS_DBG
        s_read_fail_streak++;
        if (s_read_fail_streak == 1u || (s_read_fail_streak % 40u) == 0u) {
            CMP_LOG("read fail streak=%u\n", (unsigned)s_read_fail_streak);
        }
#endif
        return;
    }
#if COMPASS_DBG
    s_read_fail_streak = 0;
#endif

    mx = (float)d.data.mag.x;
    my = (float)d.data.mag.y;
    mz = (float)d.data.mag.z;
    mx_use = mx;
    my_use = my;
    if (!isfinite(mx) || !isfinite(my)) {
        return;
    }

    (void)compute_tilt_compensated_xy(mx, my, mz, &mx_use, &my_use,
#if COMPASS_CAL_LOG_EVERY > 0
                                      &tilt_deg
#else
                                      NULL
#endif
    );

    if (s_calibrating) {
        float cov;
        int pct;
        char buf[24];

        compass_calib_runtime_add_sample(&s_runtime_cal, mx_use, my_use);
        cov = compass_calib_runtime_coverage(&s_runtime_cal);
        pct = (int)(cov * 100.f + 0.5f);
        if (pct > 100) {
            pct = 100;
        }
        rt_snprintf(buf, sizeof(buf), "%d%%", pct);
        lv_label_set_text(s_lbl_deg, buf);
#if COMPASS_CAL_LOG_EVERY > 0
        s_cal_log_seq++;
        if ((s_cal_log_seq % (rt_uint32_t)COMPASS_CAL_LOG_EVERY) == 0u) {
            float cx = 0.5f * (s_runtime_cal.min_x + s_runtime_cal.max_x);
            float cy = 0.5f * (s_runtime_cal.min_y + s_runtime_cal.max_y);
            rt_kprintf("[compass-cal] n=%u cov=%d%% tilt=%.1f c=(%.1f,%.1f) v=(%.1f,%.1f)\n",
                       (unsigned)s_runtime_cal.sample_count, pct, (double)tilt_deg,
                       (double)cx, (double)cy, (double)(mx_use - cx), (double)(my_use - cy));
        }
#endif
        if (s_runtime_cal.sample_count >= COMPASS_AUTO_CAL_MIN_SAMPLES &&
            cov >= COMPASS_AUTO_CAL_MIN_COVERAGE) {
            calib_mode_finish(true);
        }
        return;
    }

    compass_calib_runtime_add_sample(&s_runtime_bg, mx_use, my_use);
    if (!s_has_saved && !s_has_live &&
        s_runtime_bg.sample_count >= COMPASS_AUTO_CAL_MIN_SAMPLES &&
        compass_calib_runtime_coverage(&s_runtime_bg) >= COMPASS_AUTO_CAL_MIN_COVERAGE &&
        compass_calib_runtime_finalize(&s_runtime_bg, &s_calib_live)) {
        s_has_live = true;
        calib_btn_refresh();
    }

    p = get_working_calib();
    if (p == RT_NULL) {
        lv_label_set_text(s_lbl_deg, "--度");
        lv_label_set_text(s_lbl_dir, "请点击校准并慢转一圈");
        return;
    }

    compass_calib_apply(p, mx_use, my_use, &vx, &vy);
    if ((vx * vx + vy * vy) < (COMPASS_MIN_VEC_NORM_UT * COMPASS_MIN_VEC_NORM_UT)) {
        if (!s_warn_interference) {
            lv_label_set_text(s_lbl_dir, "磁干扰较强");
            s_warn_interference = true;
        }
        return;
    }
    s_warn_interference = false;

#if COMPASS_DIAG_LOG_EVERY > 0
    s_diag_seq++;
    if ((s_diag_seq % (rt_uint32_t)COMPASS_DIAG_LOG_EVERY) == 0u) {
        rt_kprintf("[compass] cov=%.2f bg_n=%u use=%s q=%.2f raw=(%.1f,%.1f) cal=(%.1f,%.1f)\n",
                   (double)compass_calib_runtime_coverage(&s_runtime_bg),
                   (unsigned)s_runtime_bg.sample_count,
                   s_has_saved ? "saved" : (s_has_live ? "live" : "none"),
                   (double)p->quality, (double)mx_use, (double)my_use, (double)vx, (double)vy);
    }
#endif

    r_new = heading_from_horizontal_field(vx, vy);

    if (!s_heading_ok) {
        s_heading = r_new;
        s_heading_ok = true;
    } else {
        dlt = r_new - s_heading;
        if (dlt > 180.f) {
            dlt -= 360.f;
        } else if (dlt < -180.f) {
            dlt += 360.f;
        }
        s_heading = norm360(s_heading + COMPASS_ANGLE_SMOOTH * dlt);
    }
    apply_compass_ui(s_heading);
}

static void create_ui(void) {
    lv_obj_t *scr = lv_scr_act();

    s_needle = lv_img_create(scr);
    lv_img_set_src(s_needle, &img_compass_pointer_diagram);
    lv_img_set_pivot(s_needle, POINTER_PIVOT, POINTER_PIVOT);
    lv_obj_center(s_needle);

    s_lbl_deg = lv_label_create(scr);
    lv_ext_set_local_font(s_lbl_deg, FONT_BIGL, lv_color_hex(0xffffff));
    lv_obj_align(s_lbl_deg, LV_ALIGN_TOP_MID, 0, 2);

    s_lbl_dir = lv_label_create(scr);
    lv_ext_set_local_font(s_lbl_dir, FONT_BIGL, lv_color_hex(0xffffff));
    lv_obj_align(s_lbl_dir, LV_ALIGN_TOP_MID, 0, 44);

    s_btn_cal = lv_btn_create(scr);
    lv_obj_set_size(s_btn_cal, 168, 44);
    lv_obj_align(s_btn_cal, LV_ALIGN_BOTTOM_MID, 0, -10);
    cal_btn_apply_style(s_btn_cal);
    lv_obj_add_event_cb(s_btn_cal, calib_btn_event_cb, LV_EVENT_CLICKED, NULL);

    s_btn_cal_txt = lv_label_create(s_btn_cal);
    lv_ext_set_local_font(s_btn_cal_txt, FONT_NORMAL, lv_color_hex(0xffffff));
    lv_label_set_long_mode(s_btn_cal_txt, LV_LABEL_LONG_CLIP);
    lv_obj_center(s_btn_cal_txt);
    calib_btn_refresh();
    apply_compass_ui(0.f);
}

static void mag_open(void) {
    rt_err_t e;

#if COMPASS_DBG
    s_read_fail_streak = 0;
#endif
    s_mag_we_opened = RT_FALSE;
    s_mag = rt_device_find(MAG_DEV_NAME);
    if (s_mag == RT_NULL) {
        s_mag_ok = RT_FALSE;
        CMP_LOG("find %s fail\n", MAG_DEV_NAME);
        return;
    }
    e = rt_device_open(s_mag, RT_DEVICE_FLAG_RDONLY);
    if (e == RT_EOK) {
        s_mag_ok = true;
        s_mag_we_opened = true;
        CMP_LOG("open %s\n", MAG_DEV_NAME);
    } else if (e == -RT_EBUSY) {
        s_mag_ok = true;
        CMP_LOG("%s EBUSY\n", MAG_DEV_NAME);
    } else {
        s_mag_ok = false;
        CMP_LOG("open %s err %d\n", MAG_DEV_NAME, (int)e);
    }
}

static void acce_open(void) {
    rt_err_t e;

    s_acce_we_opened = RT_FALSE;
    s_acce = rt_device_find(ACCE_DEV_NAME);
    if (s_acce == RT_NULL) {
        s_acce_ok = false;
        return;
    }
    e = rt_device_open(s_acce, RT_DEVICE_FLAG_RDONLY);
    if (e == RT_EOK) {
        s_acce_ok = true;
        s_acce_we_opened = true;
    } else if (e == -RT_EBUSY) {
        s_acce_ok = true;
    } else {
        s_acce_ok = false;
    }
}

static void mag_close(void) {
    if (s_mag != RT_NULL && s_mag_we_opened) {
        rt_device_close(s_mag);
    }
    s_mag = RT_NULL;
    s_mag_ok = false;
    s_mag_we_opened = false;
}

static void acce_close(void) {
    if (s_acce != RT_NULL && s_acce_we_opened) {
        rt_device_close(s_acce);
    }
    s_acce = RT_NULL;
    s_acce_ok = false;
    s_acce_we_opened = false;
}

static void timer_del(void) {
    if (s_timer) {
        lv_timer_del(s_timer);
        s_timer = NULL;
    }
}

static void on_start(void) {
    memset(&s_calib_saved, 0, sizeof(s_calib_saved));
    memset(&s_calib_live, 0, sizeof(s_calib_live));
    s_has_saved = compass_calib_store_load(&s_calib_saved);
#if COMPASS_CAL_STORE_LOG
    rt_kprintf("[compass] on_start calib_loaded=%d\n", (int)s_has_saved);
#endif
    s_has_live = false;
    s_calibrating = false;
    compass_calib_runtime_reset(&s_runtime_cal);
    compass_runtime_reset();
    mag_open();
    acce_open();
    create_ui();
    if (s_needle) {
        lv_img_dsc_t *rot = app_cache_copy_alloc(&img_compass_pointer_diagram, ROTATE_MEM);
        RT_ASSERT(rot != NULL);
        lv_img_set_src(s_needle, rot);
        lv_img_set_pivot(s_needle, POINTER_PIVOT, POINTER_PIVOT);
    }
    if (s_mag_ok) {
        lv_label_set_text(s_lbl_deg, "…");
        lv_label_set_text(s_lbl_dir, "读取中");
        s_timer = lv_timer_create(tick_cb, TICK_MS, NULL);
        lv_timer_set_repeat_count(s_timer, -1);
    } else {
        labels_set_no_sensor();
    }
    lv_img_cache_invalidate_src(NULL);
}

static void on_pause(void) {
    timer_del();
    mag_close();
    acce_close();
}

static void on_resume(void) {
    mag_open();
    acce_open();
    if (s_mag_ok && s_timer == NULL) {
        s_calibrating = false;
        calib_btn_refresh();
        compass_runtime_reset();
        s_timer = lv_timer_create(tick_cb, TICK_MS, NULL);
        lv_timer_set_repeat_count(s_timer, -1);
    }
}

static void on_stop(void) {
    timer_del();
    if (s_needle) {
        lv_img_dsc_t *cur = (lv_img_dsc_t *)lv_img_get_src(s_needle);
        if (cur && cur != &img_compass_pointer_diagram) {
            app_cache_copy_free(cur);
            lv_img_set_src(s_needle, &img_compass_pointer_diagram);
        }
    }
    mag_close();
    acce_close();
    s_needle = s_lbl_deg = s_lbl_dir = s_btn_cal = s_btn_cal_txt = NULL;
}

static void msg_handler(gui_app_msg_type_t msg, void *param) {
    switch (msg) {
    case GUI_APP_MSG_ONSTART:
        on_start();
        break;
    case GUI_APP_MSG_ONRESUME:
        on_resume();
        break;
    case GUI_APP_MSG_ONPAUSE:
        on_pause();
        break;
    case GUI_APP_MSG_ONSTOP:
        on_stop();
        break;
    default:
        break;
    }
    (void)param;
}

#define APP_ID "compass"

static int app_main(intent_t i) {
    gui_app_regist_msg_handler(APP_ID, msg_handler);
    (void)i;
    return 0;
}

BUILTIN_APP_EXPORT(LV_EXT_STR_ID(compass), LV_EXT_IMG_GET(img_compass), APP_ID,
                   app_main);
