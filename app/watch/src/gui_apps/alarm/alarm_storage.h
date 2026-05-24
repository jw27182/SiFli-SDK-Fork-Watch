#ifndef __ALARM_STORAGE_H__
#define __ALARM_STORAGE_H__

#include <stdint.h>

#define ALARM_MAX_COUNT    10
#define ALARM_FILE_PATH    "/alarm/alarms.dat"

/* 闹钟状态 */
#define ALARM_STATE_DISABLE  0
#define ALARM_STATE_ENABLE   1

/* 贪睡 */
#define ALARM_SNOOZE_DISABLE 0
#define ALARM_SNOOZE_ENABLE  1

/* 重复模式 */
#define ALARM_REPEAT_ONE_SHOT  0x00
#define ALARM_REPEAT_MONDAY    (1 << 0)
#define ALARM_REPEAT_TUESDAY   (1 << 1)
#define ALARM_REPEAT_WEDNESDAY (1 << 2)
#define ALARM_REPEAT_THURSDAY  (1 << 3)
#define ALARM_REPEAT_FRIDAY    (1 << 4)
#define ALARM_REPEAT_SATURDAY  (1 << 5)
#define ALARM_REPEAT_SUNDAY    (1 << 6)
#define ALARM_REPEAT_EVERYDAY  0x7F
#define ALARM_REPEAT_WORKDAY   (0x1F)
#define ALARM_REPEAT_WEEKEND   (0x60)

typedef struct {
    uint8_t hour;
    uint8_t minute;
    uint8_t state;
    uint8_t snooze;
    uint8_t repeat_days;
    uint8_t reserved[3];
} alarm_entry_t;

typedef struct {
    uint8_t count;
    uint8_t reserved[3];
    alarm_entry_t alarms[ALARM_MAX_COUNT];
} alarm_db_t;

int alarm_storage_init(void);
int alarm_storage_load(alarm_db_t *db);
int alarm_storage_save(const alarm_db_t *db);
int alarm_storage_add(const alarm_entry_t *entry);
int alarm_storage_delete(uint8_t index);
int alarm_storage_update(uint8_t index, const alarm_entry_t *entry);
int alarm_storage_get(uint8_t index, alarm_entry_t *entry);
uint8_t alarm_storage_get_count(void);

#endif
