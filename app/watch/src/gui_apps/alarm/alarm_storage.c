/*********************
 *      INCLUDES
 *********************/
#include <rtthread.h>
#include <string.h>
#include <stdbool.h>

#include "alarm_storage.h"
#include "alarm_manager.h"
#include "share_prefs.h"

#define DBG_TAG "alarm.storage"
#define DBG_LVL DBG_INFO
#include <rtdbg.h>

#define ALARM_PREF_NAME  "alarm"
#define ALARM_PREF_KEY   "alarms_v1"

static alarm_db_t alarm_db;
static bool db_loaded = false;

static share_prefs_t *_open_alarm_pref(void)
{
    return share_prefs_open(ALARM_PREF_NAME, SHAREPREFS_MODE_PRIVATE);
}

int alarm_storage_init(void)
{
    memset(&alarm_db, 0, sizeof(alarm_db));
    db_loaded = false;

    /* 尝试从 share_prefs 加载 */
    share_prefs_t *pf = _open_alarm_pref();
    if (pf) {
        int32_t n = share_prefs_get_block(pf, ALARM_PREF_KEY, &alarm_db, sizeof(alarm_db));
        share_prefs_close(pf);
        if (n == sizeof(alarm_db) && alarm_db.count <= ALARM_MAX_COUNT) {
            db_loaded = true;
            LOG_I("Loaded %d alarms from share_prefs", alarm_db.count);
            return 0;
        }
        LOG_W("Alarm data corrupt or empty, reset");
    }

    /* 初始化默认 */
    memset(&alarm_db, 0, sizeof(alarm_db));
    db_loaded = true;
    return 0;
}

int alarm_storage_load(alarm_db_t *db)
{
    if (!db_loaded)
        alarm_storage_init();
    if (db)
        memcpy(db, &alarm_db, sizeof(alarm_db));
    return 0;
}

int alarm_storage_save(const alarm_db_t *db)
{
    if (db)
        memcpy(&alarm_db, db, sizeof(alarm_db));

    share_prefs_t *pf = _open_alarm_pref();
    if (!pf) {
        LOG_E("Failed to open alarm prefs");
        return -1;
    }

    rt_err_t ret = share_prefs_set_block(pf, ALARM_PREF_KEY, &alarm_db, sizeof(alarm_db));
    share_prefs_close(pf);

    if (ret != RT_EOK) {
        LOG_E("Alarm prefs write failed: %d", (int)ret);
        return -1;
    }
    LOG_I("Saved %d alarms", alarm_db.count);
    return 0;
}

int alarm_storage_add(const alarm_entry_t *entry)
{
    if (alarm_db.count >= ALARM_MAX_COUNT) {
        LOG_W("alarm_storage_add failed: count=%d >= max=%d", alarm_db.count, ALARM_MAX_COUNT);
        return -1;
    }

    /* 先保存到存储，成功后再更新内存，保证原子性 */
    alarm_db_t backup;
    memcpy(&backup, &alarm_db, sizeof(alarm_db));

    memcpy(&alarm_db.alarms[alarm_db.count], entry, sizeof(alarm_entry_t));
    alarm_db.count++;

    if (alarm_storage_save(NULL) != 0) {
        /* 写入失败，回滚内存状态 */
        memcpy(&alarm_db, &backup, sizeof(alarm_db));
        LOG_E("alarm_storage_add: write failed, rolled back");
        return -1;
    }
    LOG_I("alarm_storage_add: id=%d time=%02d:%02d state=%d repeat=0x%02x snooze=%d",
          alarm_db.count - 1, entry->hour, entry->minute,
          entry->state, entry->repeat_days, entry->snooze);
    alarm_manager_sync();
    return 0;
}

int alarm_storage_delete(uint8_t index)
{
    if (index >= alarm_db.count) {
        LOG_W("alarm_storage_delete failed: index=%d >= count=%d", index, alarm_db.count);
        return -1;
    }

    LOG_I("alarm_storage_delete: id=%d time=%02d:%02d", index,
          alarm_db.alarms[index].hour, alarm_db.alarms[index].minute);

    /* 移动后续元素 */
    for (uint8_t i = index; i < alarm_db.count - 1; i++)
        memcpy(&alarm_db.alarms[i], &alarm_db.alarms[i + 1],
               sizeof(alarm_entry_t));
    alarm_db.count--;
    memset(&alarm_db.alarms[alarm_db.count], 0, sizeof(alarm_entry_t));
    int ret = alarm_storage_save(NULL);
    if (ret == 0)
        alarm_manager_sync();
    return ret;
}

int alarm_storage_update(uint8_t index, const alarm_entry_t *entry)
{
    if (index >= alarm_db.count) {
        LOG_W("alarm_storage_update failed: index=%d >= count=%d", index, alarm_db.count);
        return -1;
    }

    LOG_I("alarm_storage_update: id=%d time=%02d:%02d state=%d repeat=0x%02x snooze=%d",
          index, entry->hour, entry->minute,
          entry->state, entry->repeat_days, entry->snooze);

    memcpy(&alarm_db.alarms[index], entry, sizeof(alarm_entry_t));
    int ret = alarm_storage_save(NULL);
    if (ret == 0)
        alarm_manager_sync();
    return ret;
}

int alarm_storage_get(uint8_t index, alarm_entry_t *entry)
{
    if (index >= alarm_db.count || !entry)
        return -1;

    memcpy(entry, &alarm_db.alarms[index], sizeof(alarm_entry_t));
    return 0;
}

uint8_t alarm_storage_get_count(void)
{
    if (!db_loaded)
        alarm_storage_init();
    return alarm_db.count;
}
