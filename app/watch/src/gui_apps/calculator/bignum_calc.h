#ifndef __BIGNUM_CALC_H__
#define __BIGNUM_CALC_H__

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/*
 * 大数计算器模块 - 自包含实现，不依赖外部库
 * 使用十进制数字数组实现定点大数运算
 *
 * 定点数表示：内部使用整数 = 原始值 * 10^SCALE_FACTOR
 * SCALE_FACTOR = 10，支持10位小数精度
 * 最大支持 MAX_DIGITS 位整数部分
 */

#define BIGNUM_SCALE_FACTOR  10    /* 小数精度：10位 */
#define BIGNUM_MAX_DIGITS   80    /* 最大有效数字位数 */

/* 大数计算器状态 */
typedef struct {
    uint8_t digits[BIGNUM_MAX_DIGITS]; /* 十进制数字数组，每个元素0-9 */
    int num_digits;                     /* 有效数字位数 */
    bool negative;                      /* 是否为负数 */
    bool is_valid;                      /* 数据是否有效 */
} bignum_val_t;

/* 运算结果 */
typedef enum {
    BIGNUM_OK = 0,
    BIGNUM_ERR_OVERFLOW,        /* 溢出 */
    BIGNUM_ERR_DIV_ZERO,        /* 除零 */
    BIGNUM_ERR_INVALID,         /* 无效输入 */
    BIGNUM_ERR_MEMORY,          /* 内存分配失败 */
} bignum_err_t;

#define BIGNUM_MAX_STR_LEN   128   /* 最大字符串长度 */

/* 初始化和释放 */
void bignum_val_init(bignum_val_t *val);
void bignum_val_free(bignum_val_t *val);

/* 从字符串设置值（支持整数和小数） */
bignum_err_t bignum_val_from_str(bignum_val_t *val, const char *str);

/* 转换为字符串（自动格式化，去除尾随零） */
bignum_err_t bignum_val_to_str(const bignum_val_t *val, char *buf, size_t buf_size);

/* 四则运算 */
bignum_err_t bignum_val_add(bignum_val_t *result, const bignum_val_t *a, const bignum_val_t *b);
bignum_err_t bignum_val_sub(bignum_val_t *result, const bignum_val_t *a, const bignum_val_t *b);
bignum_err_t bignum_val_mul(bignum_val_t *result, const bignum_val_t *a, const bignum_val_t *b);
bignum_err_t bignum_val_div(bignum_val_t *result, const bignum_val_t *a, const bignum_val_t *b);

/* 取反 */
bignum_err_t bignum_val_neg(bignum_val_t *result, const bignum_val_t *a);

/* 百分比（除以100） */
bignum_err_t bignum_val_percent(bignum_val_t *result, const bignum_val_t *a);

/* 检查是否为零 */
bool bignum_val_is_zero(const bignum_val_t *val);

/* 复制 */
bignum_err_t bignum_val_copy(bignum_val_t *dest, const bignum_val_t *src);

/* 设置为零 */
void bignum_val_set_zero(bignum_val_t *val);

#endif /* __BIGNUM_CALC_H__ */
