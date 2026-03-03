#ifndef EEZ_LVGL_UI_SCREENS_H
#define EEZ_LVGL_UI_SCREENS_H

#include <lvgl/lvgl.h>

#ifdef __cplusplus
extern "C" {
#endif

// Screens

enum ScreensEnum {
    _SCREEN_ID_FIRST = 1,
    SCREEN_ID_LOCATION = 1,
    SCREEN_ID_CLOCK = 2,
    _SCREEN_ID_LAST = 2
};

typedef struct _objects_t {
    lv_obj_t *location;
    lv_obj_t *clock;
    lv_obj_t *label_gps_satellites_num;
    lv_obj_t *label_gps_status;
    lv_obj_t *obj0;
    lv_obj_t *obj1;
    lv_obj_t *obj2;
    lv_obj_t *obj3;
    lv_obj_t *label_gps_east;
    lv_obj_t *label_gps_north;
    lv_obj_t *label_gps_utc_time;
    lv_obj_t *obj4;
    lv_obj_t *obj5;
    lv_obj_t *label_gps_precision;
    lv_obj_t *obj6;
    lv_obj_t *obj7;
    lv_obj_t *label_gps_speed;
    lv_obj_t *obj8;
    lv_obj_t *obj9;
    lv_obj_t *label_gps_altitude;
    lv_obj_t *obj10;
    lv_obj_t *obj11;
    lv_obj_t *obj12;
    lv_obj_t *obj13;
    lv_obj_t *obj14;
    lv_obj_t *obj15;
    lv_obj_t *obj16;
    lv_obj_t *obj17;
    lv_obj_t *panel_sv;
    lv_obj_t *arc_battery_val;
    lv_obj_t *img_corona_minute;
    lv_obj_t *img_corona_second;
    lv_obj_t *label_battery_val;
    lv_obj_t *label_hour;
    lv_obj_t *label_minute;
    lv_obj_t *label_week;
    lv_obj_t *label_date;
    lv_obj_t *arc_gps_status;
} objects_t;

extern objects_t objects;

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