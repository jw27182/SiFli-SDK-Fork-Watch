#ifndef EEZ_LVGL_UI_EVENTS_H
#define EEZ_LVGL_UI_EVENTS_H

#include <lvgl/lvgl.h>

#ifdef __cplusplus
extern "C" {
#endif

extern void action_on_shutdown_clicked(lv_event_t * e);
extern void action_on_auto_brightness_clicked(lv_event_t * e);
extern void action_on_brightness_add_clicked(lv_event_t * e);
extern void action_on_brightness_subtract_clicked(lv_event_t * e);
extern void action_on_charge_current_add_clicked(lv_event_t * e);
extern void action_on_charge_current_subtract_clicked(lv_event_t * e);
extern void action_on_developer_mode_clicked(lv_event_t * e);
extern void action_on_developer_mode_show_toast_tip(lv_event_t * e);
extern void action_on_developer_mode_show_toast_warning(lv_event_t * e);
extern void action_on_sync_utc_time_clicked(lv_event_t * e);
extern void action_on_auto_screen_off_clicked(lv_event_t * e);
extern void action_auto_screen_off_time_change(lv_event_t * e);

#ifdef __cplusplus
}
#endif

#endif /*EEZ_LVGL_UI_EVENTS_H*/