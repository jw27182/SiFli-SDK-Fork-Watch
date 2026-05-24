/*********************
 *      INCLUDES
 *********************/
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include "bignum_calc.h"

/*
 * 定点数内部表示：
 * 所有值存储为整数形式：internal_value = actual_value * 10^SCALE_FACTOR
 * digits[0..num_digits-1] 是 internal_value 的十进制数字，digits[0] 是最高位
 *
 * 例如 SCALE_FACTOR=10：
 *   实际值 0       → internal = 0              → digits=[0], num_digits=1
 *   实际值 1       → internal = 10000000000    → digits=[1,0,0,0,0,0,0,0,0,0,0], num_digits=11
 *   实际值 123     → internal = 1230000000000  → digits=[1,2,3,0,0,0,0,0,0,0,0,0], num_digits=12
 *   实际值 0.1     → internal = 1000000000     → digits=[1,0,0,0,0,0,0,0,0,0], num_digits=10
 *   实际值 1.5     → internal = 15000000000    → digits=[1,5,0,0,0,0,0,0,0,0,0], num_digits=11
 *
 * 关键不变量：内部值始终 = actual_value * 10^SCALE_FACTOR
 * 加减法直接操作 internal_value（整数加减）
 * 乘法：result = (a * b) / 10^SCALE_FACTOR
 * 除法：result = (a * 10^SCALE_FACTOR) / b
 */

/* 初始化和释放 */
void bignum_val_init(bignum_val_t *val)
{
    memset(val->digits, 0, sizeof(val->digits));
    val->num_digits = 0;
    val->negative = false;
    val->is_valid = false;
}

void bignum_val_free(bignum_val_t *val)
{
    memset(val->digits, 0, sizeof(val->digits));
    val->num_digits = 0;
    val->negative = false;
    val->is_valid = false;
}

/* 设置为零 */
void bignum_val_set_zero(bignum_val_t *val)
{
    memset(val->digits, 0, sizeof(val->digits));
    val->num_digits = 1;
    val->digits[0] = 0;
    val->negative = false;
    val->is_valid = true;
}

/* 复制 */
bignum_err_t bignum_val_copy(bignum_val_t *dest, const bignum_val_t *src)
{
    memcpy(dest->digits, src->digits, sizeof(src->digits));
    dest->num_digits = src->num_digits;
    dest->negative = src->negative;
    dest->is_valid = src->is_valid;
    return BIGNUM_OK;
}

/*
 * 去除前导零
 * 保留至少 SCALE_FACTOR+1 位（确保定点数整数部分至少有1位"0"）
 * 例如 0.1 的内部值为 [0,1,0,0,0,0,0,0,0,0,0]（11位），不能去掉前导零
 */
static void remove_leading_zeros(bignum_val_t *val)
{
    int min_digits = BIGNUM_SCALE_FACTOR + 1;
    while (val->num_digits > min_digits && val->digits[0] == 0) {
        memmove(val->digits, val->digits + 1, val->num_digits - 1);
        val->num_digits--;
    }
    if (val->num_digits == 1 && val->digits[0] == 0) {
        val->negative = false;
    }
}

/* 检查是否为零 */
bool bignum_val_is_zero(const bignum_val_t *val)
{
    if (!val->is_valid) return true;
    for (int i = 0; i < val->num_digits; i++) {
        if (val->digits[i] != 0) return false;
    }
    return true;
}

/*
 * 左移 n 位（乘以 10^n，在末尾补零）
 * digits[0] 是最高位，所以乘以 10^n 就是在末尾（低位）添加 n 个零
 * 例如: [1,2,3] × 10^2 = [1,2,3,0,0]
 */
static void shift_left(bignum_val_t *val, int n)
{
    if (n <= 0 || val->num_digits == 0) return;
    int new_len = val->num_digits + n;
    if (new_len > BIGNUM_MAX_DIGITS) {
        new_len = BIGNUM_MAX_DIGITS;
        n = new_len - val->num_digits;
    }
    if (n > 0) {
        memset(val->digits + val->num_digits, 0, n);
        val->num_digits = new_len;
    }
}

/* 比较绝对值：返回 1, 0, -1 */
static int compare_abs(const bignum_val_t *a, const bignum_val_t *b)
{
    if (a->num_digits != b->num_digits) {
        return (a->num_digits > b->num_digits) ? 1 : -1;
    }
    for (int i = 0; i < a->num_digits; i++) {
        if (a->digits[i] != b->digits[i]) {
            return (a->digits[i] > b->digits[i]) ? 1 : -1;
        }
    }
    return 0;
}

/* 绝对值加法: result = |a| + |b| */
static void add_abs(bignum_val_t *result, const bignum_val_t *a, const bignum_val_t *b)
{
    int max_len = (a->num_digits > b->num_digits) ? a->num_digits : b->num_digits;
    int carry = 0;

    memset(result->digits, 0, sizeof(result->digits));
    result->num_digits = max_len;

    for (int i = max_len - 1; i >= 0; i--) {
        int da = (i >= (max_len - a->num_digits)) ? a->digits[i - (max_len - a->num_digits)] : 0;
        int db = (i >= (max_len - b->num_digits)) ? b->digits[i - (max_len - b->num_digits)] : 0;
        int sum = da + db + carry;
        result->digits[i] = sum % 10;
        carry = sum / 10;
    }

    if (carry) {
        memmove(result->digits + 1, result->digits, max_len);
        result->digits[0] = carry;
        result->num_digits = max_len + 1;
    }
}

/* 绝对值减法: result = |a| - |b|，要求 |a| >= |b| */
static void sub_abs(bignum_val_t *result, const bignum_val_t *a, const bignum_val_t *b)
{
    memset(result->digits, 0, sizeof(result->digits));
    result->num_digits = a->num_digits;
    int borrow = 0;

    for (int i = a->num_digits - 1; i >= 0; i--) {
        int da = a->digits[i];
        int db = (i >= (a->num_digits - b->num_digits)) ? b->digits[i - (a->num_digits - b->num_digits)] : 0;
        int diff = da - db - borrow;
        if (diff < 0) {
            diff += 10;
            borrow = 1;
        } else {
            borrow = 0;
        }
        result->digits[i] = diff;
    }
    remove_leading_zeros(result);
}

/* 绝对值乘法: result = |a| * |b|（作为整数） */
static void mul_abs_raw(bignum_val_t *result, const bignum_val_t *a, const bignum_val_t *b)
{
    memset(result->digits, 0, sizeof(result->digits));
    int total = a->num_digits + b->num_digits;
    if (total > BIGNUM_MAX_DIGITS) total = BIGNUM_MAX_DIGITS;
    result->num_digits = total;

    for (int i = a->num_digits - 1; i >= 0; i--) {
        int carry = 0;
        for (int j = b->num_digits - 1; j >= 0; j--) {
            int pos = i + j + 1;
            if (pos >= BIGNUM_MAX_DIGITS) continue;
            int product = a->digits[i] * b->digits[j] + result->digits[pos] + carry;
            result->digits[pos] = product % 10;
            carry = product / 10;
        }
        int pos = i;
        if (pos < BIGNUM_MAX_DIGITS) {
            result->digits[pos] += carry;
        }
    }
    remove_leading_zeros(result);
}

/*
 * 绝对值除法: result = |a| / |b|，返回余数到 remainder
 * a 和 b 都是整数表示
 */
static void div_abs(bignum_val_t *result, const bignum_val_t *a, const bignum_val_t *b,
                    bignum_val_t *remainder)
{
    if (bignum_val_is_zero(b)) {
        bignum_val_set_zero(result);
        if (remainder) bignum_val_set_zero(remainder);
        return;
    }

    memset(result->digits, 0, sizeof(result->digits));
    result->num_digits = a->num_digits;
    result->negative = false;

    bignum_val_t rem;
    bignum_val_init(&rem);
    rem.num_digits = 1;
    rem.digits[0] = 0;
    rem.is_valid = true;

    for (int i = 0; i < a->num_digits && i < BIGNUM_MAX_DIGITS; i++) {
        /* rem = rem * 10 + a->digits[i] */
        shift_left(&rem, 1);
        rem.digits[rem.num_digits - 1] = a->digits[i];
        remove_leading_zeros(&rem);

        /* 找商的当前位 */
        int q = 0;
        while (compare_abs(&rem, b) >= 0) {
            bignum_val_t tmp;
            sub_abs(&tmp, &rem, b);
            memcpy(rem.digits, tmp.digits, sizeof(tmp.digits));
            rem.num_digits = tmp.num_digits;
            remove_leading_zeros(&rem);
            q++;
        }
        result->digits[i] = q;
    }
    remove_leading_zeros(result);

    if (remainder) {
        memcpy(remainder->digits, rem.digits, sizeof(rem.digits));
        remainder->num_digits = rem.num_digits;
        remainder->negative = false;
        remainder->is_valid = true;
    }
}

/* 从字符串设置值（支持整数和小数） */
bignum_err_t bignum_val_from_str(bignum_val_t *val, const char *str)
{
    if (!str || str[0] == '\0') {
        bignum_val_set_zero(val);
        return BIGNUM_OK;
    }

    bignum_val_init(val);

    const char *p = str;
    bool negative = false;

    /* 处理符号 */
    if (*p == '-') {
        negative = true;
        p++;
    } else if (*p == '+') {
        p++;
    }

    /* 查找小数点 */
    const char *dot = strchr(p, '.');
    size_t int_len, frac_len;

    if (dot == NULL) {
        int_len = strlen(p);
        frac_len = 0;
    } else {
        int_len = dot - p;
        frac_len = strlen(dot + 1);
        if (frac_len > BIGNUM_SCALE_FACTOR) {
            frac_len = BIGNUM_SCALE_FACTOR;
        }
    }

    /* 构造不含小数点的数字字符串 */
    size_t num_len = int_len + frac_len;
    if (num_len == 0) {
        bignum_val_set_zero(val);
        val->negative = negative;
        return BIGNUM_OK;
    }

    /* 去除整数部分前导零（但保留至少一位） */
    while (int_len > 1 && p[0] == '0') {
        p++;
        int_len--;
    }

    /* 重新计算有效小数位数（去除尾随零） */
    if (dot != NULL) {
        const char *fend = dot + 1 + frac_len - 1;
        while (frac_len > 0 && *fend == '0') {
            frac_len--;
            fend--;
        }
    }

    num_len = int_len + frac_len;
    if (num_len == 0) {
        bignum_val_set_zero(val);
        val->negative = negative;
        return BIGNUM_OK;
    }

    if (num_len > BIGNUM_MAX_DIGITS) {
        num_len = BIGNUM_MAX_DIGITS;
    }

    /* 读取整数部分 */
    val->num_digits = (int)num_len;
    size_t int_copy = (int_len <= num_len) ? int_len : num_len;
    for (size_t i = 0; i < int_copy; i++) {
        val->digits[i] = p[i] - '0';
    }

    /* 读取小数部分 */
    if (frac_len > 0 && dot != NULL && num_len > int_copy) {
        size_t frac_copy = num_len - int_copy;
        const char *frac_start = dot + 1;
        for (size_t i = 0; i < frac_copy; i++) {
            val->digits[int_copy + i] = frac_start[i] - '0';
        }
    }

    /*
     * 现在 digits 保存的是 actual_value 去掉小数点后的数字串
     * 例如 "123.45" → digits=[1,2,3,4,5], num_digits=5
     * 需要转换为 internal_value = actual_value * 10^SCALE_FACTOR
     *
     * internal_value = 12345 * 10^(SCALE_FACTOR - frac_len)
     * 需要在尾部补 (SCALE_FACTOR - frac_len) 个零
     */
    int trailing_zeros = BIGNUM_SCALE_FACTOR - (int)frac_len;
    if (trailing_zeros > 0) {
        shift_left(val, trailing_zeros);
    } else if (trailing_zeros < 0) {
        /* 小数位数超过 SCALE_FACTOR，截断 */
        int remove = -trailing_zeros;
        if (remove < val->num_digits) {
            val->num_digits -= remove;
        } else {
            bignum_val_set_zero(val);
            val->negative = negative;
            return BIGNUM_OK;
        }
    }

    remove_leading_zeros(val);
    val->negative = negative;
    val->is_valid = true;
    return BIGNUM_OK;
}

/* 转换为字符串 */
bignum_err_t bignum_val_to_str(const bignum_val_t *val, char *buf, size_t buf_size)
{
    if (!val->is_valid || !buf || buf_size == 0) {
        return BIGNUM_ERR_INVALID;
    }

    char *dst = buf;
    size_t remaining = buf_size;

    /* 零值特殊处理 */
    if (bignum_val_is_zero(val)) {
        if (remaining < 2) return BIGNUM_ERR_OVERFLOW;
        *dst++ = '0';
        *dst = '\0';
        return BIGNUM_OK;
    }

    /* 处理负号 */
    if (val->negative) {
        if (remaining < 2) return BIGNUM_ERR_OVERFLOW;
        *dst++ = '-';
        remaining--;
    }

    /*
     * internal_value = actual_value * 10^SCALE_FACTOR
     * digits[0..num_digits-1] = internal_value 的十进制表示
     *
     * 整数部分位数 = num_digits - SCALE_FACTOR（可能为0或负数）
     * 小数部分在 digits[num_digits-SCALE_FACTOR .. num_digits-1]
     */
    int int_digits = val->num_digits - BIGNUM_SCALE_FACTOR;

    if (int_digits <= 0) {
        /* 值 < 1，输出 "0." + 前导零 + 有效数字 */
        if (remaining < 3) return BIGNUM_ERR_OVERFLOW;
        *dst++ = '0';
        *dst++ = '.';
        remaining -= 2;

        /* 前导零的个数 = SCALE_FACTOR - num_digits */
        int leading_zeros = BIGNUM_SCALE_FACTOR - val->num_digits;
        while (leading_zeros > 0 && remaining > 1) {
            *dst++ = '0';
            remaining--;
            leading_zeros--;
        }

        /* 输出有效数字 */
        for (int i = 0; i < val->num_digits && remaining > 1; i++) {
            *dst++ = '0' + val->digits[i];
            remaining--;
        }
    } else {
        /* 输出整数部分 */
        for (int i = 0; i < int_digits && i < val->num_digits && remaining > 1; i++) {
            *dst++ = '0' + val->digits[i];
            remaining--;
        }

        /* 检查小数部分是否有非零值 */
        bool has_frac = false;
        for (int i = int_digits; i < val->num_digits; i++) {
            if (val->digits[i] != 0) {
                has_frac = true;
                break;
            }
        }

        if (has_frac && remaining > 1) {
            *dst++ = '.';
            remaining--;

            /* 找到最后一个非零小数位 */
            int last_nonzero = val->num_digits - 1;
            while (last_nonzero >= int_digits && val->digits[last_nonzero] == 0) {
                last_nonzero--;
            }

            /* 输出小数部分（去除尾随零） */
            for (int i = int_digits; i <= last_nonzero && remaining > 1; i++) {
                *dst++ = '0' + val->digits[i];
                remaining--;
            }
        }
    }

    *dst = '\0';
    return BIGNUM_OK;
}

/* 加法 */
bignum_err_t bignum_val_add(bignum_val_t *result, const bignum_val_t *a, const bignum_val_t *b)
{
    bignum_val_init(result);
    result->is_valid = true;

    if (a->negative == b->negative) {
        add_abs(result, a, b);
        result->negative = a->negative;
    } else {
        int cmp = compare_abs(a, b);
        if (cmp == 0) {
            bignum_val_set_zero(result);
        } else if (cmp > 0) {
            sub_abs(result, a, b);
            result->negative = a->negative;
        } else {
            sub_abs(result, b, a);
            result->negative = b->negative;
        }
    }

    remove_leading_zeros(result);
    return BIGNUM_OK;
}

/* 减法 */
bignum_err_t bignum_val_sub(bignum_val_t *result, const bignum_val_t *a, const bignum_val_t *b)
{
    bignum_val_t neg_b;
    bignum_val_copy(&neg_b, b);
    neg_b.negative = !b->negative;
    if (bignum_val_is_zero(&neg_b)) neg_b.negative = false;
    return bignum_val_add(result, a, &neg_b);
}

/* 乘法 */
bignum_err_t bignum_val_mul(bignum_val_t *result, const bignum_val_t *a, const bignum_val_t *b)
{
    bignum_val_init(result);
    result->is_valid = true;

    /* 两个 internal_value 相乘得到 a_int * b_int = (a_real * 10^S) * (b_real * 10^S) = result_real * 10^(2S)
     * 我们需要 result_real * 10^S，所以要除以 10^S */
    bignum_val_t raw;
    bignum_val_init(&raw);
    mul_abs_raw(&raw, a, b);

    /* 从 raw 中截取高位（除以 10^SCALE_FACTOR） */
    int frac_remove = BIGNUM_SCALE_FACTOR;
    if (raw.num_digits > frac_remove) {
        /* 取前 num_digits - SCALE_FACTOR 位作为结果 */
        int new_len = raw.num_digits - frac_remove;

        /* 四舍五入：看被截掉的第一位 */
        int round_digit = raw.digits[raw.num_digits - frac_remove];
        if (round_digit >= 5 && new_len > 0) {
            /* 需要进位 */
            raw.digits[raw.num_digits - frac_remove] = 0;
            /* 从最后一位开始向前进位 */
            for (int i = raw.num_digits - frac_remove - 1; i >= 0; i--) {
                raw.digits[i] += 1;
                if (raw.digits[i] < 10) break;
                raw.digits[i] = 0;
            }
            /* 如果最高位进位溢出，需要重新检查 */
            if (raw.digits[0] == 0 && raw.num_digits > 1) {
                /* 前导零已产生，需要重新对齐 */
                new_len = raw.num_digits - frac_remove;
                /* 去除前导零后重新计算 */
                remove_leading_zeros(&raw);
                new_len = raw.num_digits - frac_remove;
                if (new_len <= 0) {
                    /* 结果溢出到小数部分以下，取最接近值 */
                    bignum_val_set_zero(result);
                    result->negative = (a->negative != b->negative);
                    if (bignum_val_is_zero(result)) result->negative = false;
                    return BIGNUM_OK;
                }
            }
        }

        memcpy(result->digits, raw.digits, new_len);
        result->num_digits = new_len;
    } else {
        /* 乘积太小，结果为 0 */
        bignum_val_set_zero(result);
    }

    result->negative = (a->negative != b->negative);
    if (bignum_val_is_zero(result)) result->negative = false;
    remove_leading_zeros(result);
    return BIGNUM_OK;
}

/* 除法 */
bignum_err_t bignum_val_div(bignum_val_t *result, const bignum_val_t *a, const bignum_val_t *b)
{
    bignum_val_init(result);
    result->is_valid = true;

    if (bignum_val_is_zero(b)) {
        result->is_valid = false;
        return BIGNUM_ERR_DIV_ZERO;
    }

    /*
     * (a_int * 10^0) / (b_int * 10^0) = (a_real / b_real) * 10^0
     * 但我们需要 a_real/b_real * 10^SCALE_FACTOR
     * 所以 result = (a_int * 10^SCALE_FACTOR) / b_int
     */

    /* 将被除数左移 SCALE_FACTOR 位 */
    bignum_val_t scaled_a;
    bignum_val_init(&scaled_a);
    memcpy(scaled_a.digits, a->digits, a->num_digits);
    scaled_a.num_digits = a->num_digits;
    scaled_a.is_valid = true;
    scaled_a.negative = false;

    shift_left(&scaled_a, BIGNUM_SCALE_FACTOR);

    /* 执行整数除法 */
    bignum_val_t rem;
    div_abs(result, &scaled_a, b, &rem);

    /* 四舍五入 */
    if (!bignum_val_is_zero(&rem)) {
        /* 比较 rem * 2 和 |b| */
        bignum_val_t rem2;
        bignum_val_init(&rem2);
        add_abs(&rem2, &rem, &rem);

        if (compare_abs(&rem2, b) >= 0) {
            /* 进位 */
            bignum_val_t one;
            bignum_val_init(&one);
            one.digits[0] = 1;
            one.num_digits = 1;
            one.is_valid = true;
            one.negative = false;

            bignum_val_t tmp;
            add_abs(&tmp, result, &one);
            memcpy(result->digits, tmp.digits, sizeof(tmp.digits));
            result->num_digits = tmp.num_digits;
        }
    }

    result->negative = (a->negative != b->negative);
    if (bignum_val_is_zero(result)) result->negative = false;
    remove_leading_zeros(result);
    return BIGNUM_OK;
}

/* 取反 */
bignum_err_t bignum_val_neg(bignum_val_t *result, const bignum_val_t *a)
{
    bignum_val_copy(result, a);
    if (!bignum_val_is_zero(result)) {
        result->negative = !result->negative;
    }
    return BIGNUM_OK;
}

/* 百分比（除以100） */
bignum_err_t bignum_val_percent(bignum_val_t *result, const bignum_val_t *a)
{
    bignum_val_t hundred;
    bignum_val_init(&hundred);
    bignum_val_from_str(&hundred, "100");

    bignum_err_t err = bignum_val_div(result, a, &hundred);
    bignum_val_free(&hundred);
    return err;
}
