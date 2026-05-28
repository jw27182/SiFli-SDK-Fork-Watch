#ifndef EEZ_LVGL_UI_SCREENS_H
#define EEZ_LVGL_UI_SCREENS_H

#include <lvgl/lvgl.h>

#ifdef __cplusplus
extern "C" {
#endif

// Screens

enum ScreensEnum {
    _SCREEN_ID_FIRST = 1,
    SCREEN_ID_SETTING = 1,
    SCREEN_ID_DEVELOPER_MODE = 2,
    SCREEN_ID_LOCATION = 3,
    SCREEN_ID_CLOCK = 4,
    _SCREEN_ID_LAST = 4
};

typedef struct _objects_t {
    lv_obj_t *setting;
    lv_obj_t *developer_mode;
    lv_obj_t *location;
    lv_obj_t *clock;
    lv_obj_t *obj0;
    lv_obj_t *obj1;
    lv_obj_t *obj2;
    lv_obj_t *obj3;
    lv_obj_t *obj4;
    lv_obj_t *obj5;
    lv_obj_t *obj6;
    lv_obj_t *obj7;
    lv_obj_t *obj8;
    lv_obj_t *dropdown_auto_screen_off_time;
    lv_obj_t *obj9;
    lv_obj_t *obj10;
    lv_obj_t *switch_auto_brightness;
    lv_obj_t *obj11;
    lv_obj_t *obj12;
    lv_obj_t *label_current_brightness;
    lv_obj_t *slider_brightness;
    lv_obj_t *obj13;
    lv_obj_t *obj14;
    lv_obj_t *obj15;
    lv_obj_t *label_charge_current;
    lv_obj_t *bar_charge_current;
    lv_obj_t *obj16;
    lv_obj_t *obj17;
    lv_obj_t *obj18;
    lv_obj_t *obj19;
    lv_obj_t *obj20;
    lv_obj_t *obj21;
    lv_obj_t *obj22;
    lv_obj_t *obj23;
    lv_obj_t *obj24;
    lv_obj_t *obj25;
    lv_obj_t *obj26;
    lv_obj_t *obj27;
    lv_obj_t *obj28;
    lv_obj_t *obj29;
    lv_obj_t *label_gps_satellites_num;
    lv_obj_t *label_gps_status;
    lv_obj_t *obj30;
    lv_obj_t *obj31;
    lv_obj_t *obj32;
    lv_obj_t *obj33;
    lv_obj_t *label_gps_east;
    lv_obj_t *label_gps_north;
    lv_obj_t *label_gps_utc_time;
    lv_obj_t *obj34;
    lv_obj_t *obj35;
    lv_obj_t *label_gps_precision;
    lv_obj_t *obj36;
    lv_obj_t *obj37;
    lv_obj_t *label_gps_speed;
    lv_obj_t *obj38;
    lv_obj_t *obj39;
    lv_obj_t *label_gps_altitude;
    lv_obj_t *obj40;
    lv_obj_t *obj41;
    lv_obj_t *obj42;
    lv_obj_t *obj43;
    lv_obj_t *obj44;
    lv_obj_t *obj45;
    lv_obj_t *obj46;
    lv_obj_t *obj47;
    lv_obj_t *panel_sv;
    lv_obj_t *arc_battery_val;
    lv_obj_t *img_corona_minute;
    lv_obj_t *img_corona_second;
    lv_obj_t *img_battery_icon;
    lv_obj_t *label_hour;
    lv_obj_t *label_minute;
    lv_obj_t *label_week;
    lv_obj_t *label_date;
    lv_obj_t *arc_temperature_status;
    lv_obj_t *img_satellite_icon;
    lv_obj_t *label_battery_val;
    lv_obj_t *label_temperature_val;
} objects_t;

extern objects_t objects;

void create_screen_setting();
void tick_screen_setting();

void create_screen_developer_mode();
void tick_screen_developer_mode();

void create_screen_location();
void tick_screen_location();

void create_screen_clock();
void tick_screen_clock();

void tick_screen_by_id(enum ScreensEnum screenId);
void tick_screen(int screen_index);

void create_screens();

#ifdef __cplusplus
}
#endif

#endif /*EEZ_LVGL_UI_SCREENS_H*/