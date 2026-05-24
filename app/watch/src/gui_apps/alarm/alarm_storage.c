/*********************
 *      INCLUDES
 *********************/
#include <rtthread.h>
#include <dfs_posix.h>
#include <string.h>

#include "alarm_storage.h"

#define DBG_TAG "alarm.storage"
#define DBG_LVL DBG_INFO
#include <rtdbg.h>

static alarm_db_t alarm_db;
static bool db_loaded = false;

static void ensure_dir(const char *path)
{
    char dir[64];
    const char *last_slash = strrchr(path, '/');
    if (last_slash && last_slash != path) {
        int len = last_slash - path;
        if (len < (int)sizeof(dir)) {
            strncpy(dir, path, len);
            dir[len] = '\0';
            mkdir(dir, 0);
        }
    }
}

int alarm_storage_init(void)
{
    memset(&alarm_db, 0, sizeof(alarm_db));
    db_loaded = false;

    ensure_dir(ALARM_FILE_PATH);

    /* 尝试从文件加载 */
    int fd = open(ALARM_FILE_PATH, O_RDONLY, 0);
    if (fd >= 0) {
        int rd = read(fd, &alarm_db, sizeof(alarm_db));
        close(fd);
        if (rd == sizeof(alarm_db) && alarm_db.count <= ALARM_MAX_COUNT) {
            db_loaded = true;
            LOG_I("Loaded %d alarms from file", alarm_db.count);
            return 0;
        }
        LOG_W("Alarm file corrupt, reset");
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

    int fd = open(ALARM_FILE_PATH, O_WRONLY | O_CREAT | O_TRUNC, 0);
    if (fd < 0) {
        LOG_E("Failed to open alarm file for writing");
        return -1;
    }
    int wr = write(fd, &alarm_db, sizeof(alarm_db));
    close(fd);
    if (wr != sizeof(alarm_db)) {
        LOG_E("Alarm file write incomplete: %d/%d", wr, (int)sizeof(alarm_db));
        return -1;
    }
    LOG_I("Saved %d alarms", alarm_db.count);
    return 0;
}

int alarm_storage_add(const alarm_entry_t *entry)
{
    if (alarm_db.count >= ALARM_MAX_COUNT)
        return -1;

    /* 先保存到磁盘，成功后再更新内存，保证原子性 */
    alarm_db_t backup;
    memcpy(&backup, &alarm_db, sizeof(alarm_db));

    memcpy(&alarm_db.alarms[alarm_db.count], entry, sizeof(alarm_entry_t));
    alarm_db.count++;

    if (alarm_storage_save(NULL) != 0) {
        /* 写入失败，回滚内存状态 */
        memcpy(&alarm_db, &backup, sizeof(alarm_db));
        return -1;
    }
    return 0;
}

int alarm_storage_delete(uint8_t index)
{
    if (index >= alarm_db.count)
        return -1;

    /* 移动后续元素 */
    for (uint8_t i = index; i < alarm_db.count - 1; i++)
        memcpy(&alarm_db.alarms[i], &alarm_db.alarms[i + 1],
               sizeof(alarm_entry_t));
    alarm_db.count--;
    memset(&alarm_db.alarms[alarm_db.count], 0, sizeof(alarm_entry_t));
    return alarm_storage_save(NULL);
}

int alarm_storage_update(uint8_t index, const alarm_entry_t *entry)
{
    if (index >= alarm_db.count)
        return -1;

    memcpy(&alarm_db.alarms[index], entry, sizeof(alarm_entry_t));
    return alarm_storage_save(NULL);
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
