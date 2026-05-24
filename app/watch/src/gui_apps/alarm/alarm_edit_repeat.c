/*********************
 *      INCLUDES
 *********************/
#include <rtthread.h>
#include <rtdevice.h>
#include <string.h>

#include "app_mem.h"
#include "gui_app_fwk.h"
#include "littlevgl2rtt.h"
#include "lv_ex_data.h"
#include "lv_ext_resource_manager.h"
#include "lvgl.h"
#include "lvsf_comp.h"
#include "alarm_storage.h"

#define SCR_W 410
#define SCR_H 502

static int32_t repeat_idx = -1;
static alarm_entry_t repeat_alarm;
static lv_obj_t *btnm = NULL;

static void on_start(void);
static void on_resume(void);
static void on_pause(void);
static void on_stop(void);

#define SUBPAGE_NAME "alm_rpt"
static void msg_handler(gui_app_msg_type_t msg, void *param)
{
    (void)param;
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
}

static const char *btnm_map[] = {
    "Everyday", "Workday", "Weekend", "\n",
    "Mon", "Tue", "Wed", "\n",
    "Thu", "Fri", "Sat", "\n",
    "Sun", ""
};

static void update_btnm_state(void)
{
    if (!btnm)
        return;
    uint8_t days = repeat_alarm.repeat_days;

    /* Everyday (idx 0) */
    if (days == ALARM_REPEAT_EVERYDAY)
        lv_btnmatrix_set_btn_ctrl(btnm, 0, LV_BTNMATRIX_CTRL_CHECKED);
    else
        lv_btnmatrix_clear_btn_ctrl(btnm, 0, LV_BTNMATRIX_CTRL_CHECKED);

    /* Workday (idx 1) */
    if (days == ALARM_REPEAT_WORKDAY)
        lv_btnmatrix_set_btn_ctrl(btnm, 1, LV_BTNMATRIX_CTRL_CHECKED);
    else
        lv_btnmatrix_clear_btn_ctrl(btnm, 1, LV_BTNMATRIX_CTRL_CHECKED);

    /* Weekend (idx 2) */
    if (days == ALARM_REPEAT_WEEKEND)
        lv_btnmatrix_set_btn_ctrl(btnm, 2, LV_BTNMATRIX_CTRL_CHECKED);
    else
        lv_btnmatrix_clear_btn_ctrl(btnm, 2, LV_BTNMATRIX_CTRL_CHECKED);

    /* 各天 (idx 3-9 -> bit 0-6) */
    for (int i = 0; i < 7; i++) {
        if (days & (1 << i))
            lv_btnmatrix_set_btn_ctrl(btnm, i + 3,
                                      LV_BTNMATRIX_CTRL_CHECKED);
        else
            lv_btnmatrix_clear_btn_ctrl(btnm, i + 3,
                                        LV_BTNMATRIX_CTRL_CHECKED);
    }
}

static void btnm_event_handler(lv_event_t *e)
{
    lv_obj_t *obj = lv_event_get_target(e);
    uint16_t id = lv_btnmatrix_get_selected_btn(obj);
    uint8_t *days = &repeat_alarm.repeat_days;

    switch (id) {
    case 0: /* Everyday */
        *days = (*days == ALARM_REPEAT_EVERYDAY) ? ALARM_REPEAT_ONE_SHOT
                                                 : ALARM_REPEAT_EVERYDAY;
        break;
    case 1: /* Workday */
        *days = (*days == ALARM_REPEAT_WORKDAY) ? ALARM_REPEAT_ONE_SHOT
                                                : ALARM_REPEAT_WORKDAY;
        break;
    case 2: /* Weekend */
        *days = (*days == ALARM_REPEAT_WEEKEND) ? ALARM_REPEAT_ONE_SHOT
                                                : ALARM_REPEAT_WEEKEND;
        break;
    default: /* 各天 */
        if (id >= 3 && id <= 9) {
            uint8_t bit = 1 << (id - 3);
            *days ^= bit;
        }
        break;
    }

    update_btnm_state();
}

/* 入口函数 —— 由 alarm_edit.c 调用 */
void app_alarm_edit_repeat(int32_t idx, const alarm_entry_t *entry)
{
    repeat_idx = idx;
    if (entry)
        memcpy(&repeat_alarm, entry, sizeof(alarm_entry_t));
    else
        memset(&repeat_alarm, 0, sizeof(repeat_alarm));

    gui_app_create_page(SUBPAGE_NAME, msg_handler);
}

static void on_start(void)
{
    lv_obj_t *scr = lv_scr_act();

    /* 标题 */
    lv_obj_t *title = lv_label_create(scr);
    lv_ext_set_local_font(title, FONT_TITLE, lv_color_hex(0xFFFFFF));
    lv_label_set_text(title, "Repeat");
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 20);

    /* 按钮矩阵 */
    btnm = lv_btnmatrix_create(scr);
    lv_btnmatrix_set_map(btnm, btnm_map);
    lv_obj_set_size(btnm, SCR_W - 40, SCR_H - 100);
    lv_obj_align(btnm, LV_ALIGN_TOP_MID, 0, 60);

    /* 设置所有按钮可选中 */
    for (int i = 0; i < 10; i++) {
        lv_btnmatrix_set_btn_ctrl(btnm, i,
                                  LV_BTNMATRIX_CTRL_CHECKABLE |
                                  LV_BTNMATRIX_CTRL_CLICK_TRIG);
    }

    /* 样式 */
    lv_obj_set_style_bg_opa(btnm, LV_OPA_TRANSP,
                            LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(btnm, lv_color_hex(0x333333),
                              LV_PART_ITEMS | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(btnm, lv_color_hex(0xFFFFFF),
                                LV_PART_ITEMS | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(btnm, lv_color_hex(0x3A6D7B),
                              LV_PART_ITEMS | LV_STATE_CHECKED);
    lv_obj_set_style_radius(btnm, 12,
                            LV_PART_ITEMS | LV_STATE_DEFAULT);
    lv_ext_set_local_font(btnm, FONT_NORMAL, lv_color_hex(0xFFFFFF));

    update_btnm_state();

    lv_obj_add_event_cb(btnm, btnm_event_handler, LV_EVENT_VALUE_CHANGED,
                        NULL);

    lv_img_cache_invalidate_src(NULL);
}

static void on_resume(void)
{
}

static void on_pause(void)
{
    /* 离开时自动保存 */
    if (repeat_idx >= 0)
        alarm_storage_update(repeat_idx, &repeat_alarm);
}

static void on_stop(void)
{
    btnm = NULL;
}

