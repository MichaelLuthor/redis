/*
 * Copyright (c) 2009-2012, Salvatore Sanfilippo <antirez at gmail dot com>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 *   * Redistributions of source code must retain the above copyright notice,
 *     this list of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 *   * Neither the name of Redis nor the names of its contributors may be used
 *     to endorse or promote products derived from this software without
 *     specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include "fmacros.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <limits.h>
#include <math.h>
#include <unistd.h>
#include <sys/time.h>
#include <float.h>
#include <stdint.h>
#include <errno.h>

#include "util.h"
#include "sha1.h"

/**
 * 一个Glob风格的字符串匹配函数
 * * 匹配0个或多个任意字符
 * ? 匹配一个任意字符
 * [ABC] 匹配中括号内的任意字符
 * [^ABC] 匹配非中括号中的任意字符
 * [A-Z] 匹配中括号中指定范围的字符
 * @param pattern 匹配模式
 * @param patternLen 匹配模式长度
 * @param string 待匹配字符串
 * @param stringLen 待匹配字符串长度
 * @param nocase 是否忽略大小写 1：忽略 0：不忽略
 * @return 1：匹配成功  0：匹配失败
 */
int stringmatchlen(const char *pattern, int patternLen,
        const char *string, int stringLen, int nocase)
{
    // 匹配采用的是逐字游走的方式进行匹配,
    // 一旦游走的过程中存在不匹配的字符串则表示匹配失败。
    // 如果游走到匹配模式的末尾， 则说明该字符串完全匹配。
    // 例如： 匹配模式  ABCDEF?12345
    //           ^ 当前位置， 向后游走
    //     字符串       ABCDEFX12345
    //           ^ 当前位置， 向后游走
    while(patternLen) {
        switch(pattern[0]) {
        case '*':
            while (pattern[1] == '*') { // 因为*是匹配一个或多个， 所以处理掉多余的*
                pattern++;              // 后，继续匹配。
                patternLen--;           // TODO : 如果匹配模式字符串为"**", 这段内存后面又存着"*", 这不就错了么? 应该检查下patternLen吧。
            }
            if (patternLen == 1) // 除了第一个*， 后面的*都被清理掉了， 所以这时候匹配模式就成了 "*",
                return 1;        // 这样后面的字符串就不用再匹配了。
            while(stringLen) {                              // 由于*匹配的是一个或多个，所以没有办法确定这个多个到底是几个，
                if (stringmatchlen(pattern+1, patternLen-1, // 所以， 从1开始一个个尝试， 直到后面的模式被匹配上。
                            string, stringLen, nocase))     // 如果整个字符串都匹配完了都没有匹配到后面的模式， 则该字符串匹配失败。
                    return 1; /* match */
                string++;
                stringLen--;
            }
            return 0; /* no match */
            break;
        case '?':
            if (stringLen == 0) // 由于？表示匹配任意一个字符， 所以如果字符串已经匹配完了，那就匹配失败了。
                return 0;
            string++;     // 如果字符串还没有匹配玩，那继续游走一个字符
            stringLen--;
            break;
        case '[':
        {
            int not, match;

            pattern++; // 走过符号 [
            patternLen--;
            not = pattern[0] == '^'; // 如果中括号内的第一个字符为^, 表示不匹配中括号中的字符。
            if (not) {
                pattern++;
                patternLen--;
            }
            match = 0;
            while(1) {
                if (pattern[0] == '\\') { // 匹配转义字符， 例如： \*,\?这种, 忽略过反斜线， 匹配反斜线后面的内容。
                    pattern++;
                    patternLen--;
                    if (pattern[0] == string[0]) // 这里没有判断大小写敏感唉~~~， 如果我的模式是"123\ABC", 并且大小写不敏感， 这样的话就匹配不到"123abc"了。
                        match = 1;               // TODO 待验证
                } else if (pattern[0] == ']') {  // 中括号匹配结束
                    break;
                } else if (patternLen == 0) { // 如果中括号没有结束符，也就是找不到], 则回走到上一个字符，
                    pattern--;                // 用来接下来统一的游走。
                    patternLen++;
                    break;
                } else if (pattern[1] == '-' && patternLen >= 3) { // 匹配指定范围， - 前面和后面的字符指定开始和结束。
                    int start = pattern[0];
                    int end = pattern[2];
                    int c = string[0];
                    if (start > end) { // 如果范围从大到小， 则转为从小到大。
                        int t = start;
                        start = end;
                        end = t;
                    }
                    if (nocase) {
                        start = tolower(start);
                        end = tolower(end);
                        c = tolower(c);
                    }
                    pattern += 2; // 游走过范围指示符和后面的范围值
                    patternLen -= 2;
                    if (c >= start && c <= end)
                        match = 1;
                } else {                              // 这里正常匹配每个中括号里面的可能， 如果发现就标记match。
                    if (!nocase) {
                        if (pattern[0] == string[0])
                            match = 1;
                    } else {
                        if (tolower((int)pattern[0]) == tolower((int)string[0]))
                            match = 1;
                    }
                }
                pattern++;
                patternLen--;
            }
            if (not) // 如果开始为^, 表示不匹配中括号中的任何内容，所以这里如果匹配到了， 那就失败了。
                match = !match;
            if (!match)
                return 0; /* no match */
            string++;
            stringLen--;
            break;
        }
        case '\\': // 匹配转义字符， 直接游走过转移标记， 后面作为正常符号处理就行了。 注意这里没有break， 字符匹配交给了default
            if (patternLen >= 2) {
                pattern++;
                patternLen--;
            }
            /* fall through */ // <= 这种模式为："abc123\\"
        default: // 正常比较字符， 注意大小写是否敏感。
            if (!nocase) {
                if (pattern[0] != string[0])
                    return 0; /* no match */
            } else {
                if (tolower((int)pattern[0]) != tolower((int)string[0]))
                    return 0; /* no match */
            }
            string++;
            stringLen--;
            break;
        }
        pattern++; // 游走到下一个字符。
        patternLen--;
        if (stringLen == 0) {        // 如果字符串都匹配完了， 但是模式没有完， 那么将剩下的模式尝试匹配掉*.
            while(*pattern == '*') { // 如果*消耗完后，匹配模式还有剩余，那就表示匹配失败。
                pattern++;
                patternLen--;
            }
            break;
        }
    }
    if (patternLen == 0 && stringLen == 0) // 如果匹配模式和字符串都消耗完了， 则说明完全匹配， 否则匹配失败。
        return 1;
    return 0;
}

/**
 * 测试字符串是否与匹配模式相匹配，
 * @see stringmatchlen()
 * @param pattern 匹配模式
 * @param string 待匹配字符串
 * @param nocase 是否忽略大小写 1：忽略 0：不忽略
 * @return1：匹配成功  0：匹配失败
 */
int stringmatch(const char *pattern, const char *string, int nocase) {
    return stringmatchlen(pattern,strlen(pattern),string,strlen(string),nocase);
}

/**
 * 将可读的空间大小转换为长整形字节数。 例如， "1Gb" 转换 为1073741824, 也就是 1024*1024*1024
 * @param p 待转换的字符串
 * @param err 错误代码， 1 成功 0 失败， 如果err为null， 则不赋值。
 * @return 成功时返回字节数，失败时返回0.
 * */
long long memtoll(const char *p, int *err) {
    const char *u;
    char buf[128];
    long mul; /* unit multiplier */
    long long val;
    unsigned int digits;

    if (err) *err = 0;

    /* Search the first non digit character. */
    u = p;
    if (*u == '-') u++; // 去掉正负符号
    while(*u && isdigit(*u)) u++; // 找到第一个非数值的字符
    if (*u == '\0' || !strcasecmp(u,"b")) { // 判断字符获取基数。
        mul = 1;
    } else if (!strcasecmp(u,"k")) {
        mul = 1000;
    } else if (!strcasecmp(u,"kb")) {
        mul = 1024;
    } else if (!strcasecmp(u,"m")) {
        mul = 1000*1000;
    } else if (!strcasecmp(u,"mb")) {
        mul = 1024*1024;
    } else if (!strcasecmp(u,"g")) {
        mul = 1000L*1000*1000;
    } else if (!strcasecmp(u,"gb")) {
        mul = 1024L*1024*1024;
    } else {// 如果单位不正确， 设置错误标记。
        if (err) *err = 1;
        return 0;
    }

    /* Copy the digits into a buffer, we'll use strtoll() to convert
     * the digit (without the unit) into a number. */
    /** 将数字字符串复制到buf中，如果数值长度超过buf， 则不进行处理， 返回错误。 */
    digits = u-p;
    if (digits >= sizeof(buf)) {
        if (err) *err = 1;
        return 0;
    }
    memcpy(buf,p,digits);
    buf[digits] = '\0';

    char *endptr;
    errno = 0; // 这是一个全局变量， 大部分标准库中的函数使用该变量来设置错误码， 使用前需要先初始化， 应为他很有可能存这上一个错误的错误码。
    val = strtoll(buf,&endptr,10); // 调用标准库的函数将字符串转换为long long.
    if ((val == 0 && errno == EINVAL) || *endptr != '\0') {
        if (err) *err = 1;
        return 0;
    }
    return val*mul;

    // EINVAL Invalid argument (POSIX.1). 参数错误
    // errno 相关的资料查看 ： http://man7.org/linux/man-pages/man3/errno.3.html
}

/* Return the number of digits of 'v' when converted to string in radix 10.
 * See ll2string() for more information. */
/**
 * 返回一个十进制数值的位数，例如10, 返回2, 100返回3
 * @param v 数值
 * @return 十进制数值的位数
 */
uint32_t digits10(uint64_t v) {
    if (v < 10) return 1;
    if (v < 100) return 2;
    if (v < 1000) return 3;
    if (v < 1000000000000UL) {
        if (v < 100000000UL) {
            if (v < 1000000) {
                if (v < 10000) return 4;
                return 5 + (v >= 100000);
            }
            return 7 + (v >= 10000000UL);
        }
        if (v < 10000000000UL) {
            return 9 + (v >= 1000000000UL);
        }
        return 11 + (v >= 100000000000UL);
    }
    return 12 + digits10(v / 1000000000000UL);
}

/* Like digits10() but for signed values. */
/**
 * 返回一个带符号的十进制数值的位数，例如10, 返回2, 100返回3
 * @param v 数值
 * @return 十进制数值的位数
 */
uint32_t sdigits10(int64_t v) {
    if (v < 0) {
        // 使用溢出的方式获取LLONG_MIN的绝对值
        // @see https://stackoverflow.com/questions/11243014/why-the-absolute-value-of-the-max-negative-integer-2147483648-is-still-2147483
        /* Abs value of LLONG_MIN requires special handling. */
        uint64_t uv = (v != LLONG_MIN) ?
                      (uint64_t)-v : ((uint64_t) LLONG_MAX)+1;
        return digits10(uv)+1; /* +1 for the minus. */
    } else {
        return digits10(v);
    }
}

/* Convert a long long into a string. Returns the number of
 * characters needed to represent the number.
 * If the buffer is not big enough to store the string, 0 is returned.
 *
 * Based on the following article (that apparently does not provide a
 * novel approach but only publicizes an already used technique):
 *
 * https://www.facebook.com/notes/facebook-engineering/three-optimization-tips-for-c/10151361643253920
 *
 * Modified in order to handle signed integers since the original code was
 * designed for unsigned integers. */
/**
 * 将数值准换成字符串。
 *@param dst 保存字符串的空间
 *@param dstlen 空间长度
 *@param svalue 待转换成字符串的数值
 *@return 成功返回目标字符串长度， 失败返回0
 */
int ll2string(char *dst, size_t dstlen, long long svalue) {
    static const char digits[201] =
        "0001020304050607080910111213141516171819"      // 这个字符串很牛B， 举个例子来说. 我们有数值：45
        "2021222324252627282930313233343536373839"      // 45 * 2 = 90 就找到字符“45”的起始位置。
        "4041424344454647484950515253545556575859"
        "6061626364656667686970717273747576777879"
        "8081828384858687888990919293949596979899";
    int negative;
    unsigned long long value;

    /* The main loop works with 64bit unsigned integers for simplicity, so
     * we convert the number here and remember if it is negative. */
    /** 将有符号数值转为无符号， 并且记录符号信息来备用。 */
    if (svalue < 0) {
        if (svalue != LLONG_MIN) {
            value = -svalue;
        } else {
            value = ((unsigned long long) LLONG_MAX)+1;
        }
        negative = 1;
    } else {
        value = svalue;
        negative = 0;
    }

    /* Check length. */
    /** 获取目标字符串长度， 如果超过保存值用的字符串大小， 则直接返回错误。 */
    uint32_t const length = digits10(value)+negative;
    if (length >= dstlen) return 0;

    /* Null term. */
    uint32_t next = length;
    dst[next] = '\0';
    next--;
    while (value >= 100) {
        int const i = (value % 100) * 2; // 每次取最后两位来处理
        value /= 100;
        dst[next] = digits[i + 1];  // 根据映射表来获取相应的字符数值
        dst[next - 1] = digits[i];
        next -= 2;
    }

    /* Handle last 1-2 digits. */
    // 处理最后两位， 小于10的直接赋值，大于10的按照上面的映射表来获取
    if (value < 10) {
        dst[next] = '0' + (uint32_t) value;
    } else {
        int i = (uint32_t) value * 2;
        dst[next] = digits[i + 1];
        dst[next - 1] = digits[i];
    }

    /* Add sign. */
    /** 如果是负数，在前面加上负数符号。 */
    if (negative) dst[0] = '-';
    return length;
}

/* Convert a string into a long long. Returns 1 if the string could be parsed
 * into a (non-overflowing) long long, 0 otherwise. The value will be set to
 * the parsed value when appropriate.
 *
 * Note that this function demands that the string strictly represents
 * a long long: no spaces or other characters before or after the string
 * representing the number are accepted, nor zeroes at the start if not
 * for the string "0" representing the zero number.
 *
 * Because of its strictness, it is safe to use this function to check if
 * you can convert a string into a long long, and obtain back the string
 * from the number without any loss in the string representation. */
/**
 * 将字符串转换为long long型数值。 转换时采取严格模式，字符串开始或者末尾不能包含空格或者其他不用来表示数值的字符。
 * 如果字符串数值一开始就是0，并且不是0，则解析失败。 例如:"0123"， 将解析失败。
 * @param s 待转换字符串
 * @param slen 字符串长度
 * @param value 用于存储准桓侯的数值
 * @return 成功时返回1， 否则返回0
 */
int string2ll(const char *s, size_t slen, long long *value) {
    const char *p = s; // 处理字符串的游走指针
    size_t plen = 0; // 已处理字符串长度
    int negative = 0; // 正负符号，0：正数，1：负数
    unsigned long long v;

    // 如果字符串为空， 则解析失败
    if (plen == slen)
        return 0;

    /* Special case: first and only digit is 0. */
    /* 如果只有一个字符，并且是0， 则转换后数值为0， 并且不再继续处理。 */
    if (slen == 1 && p[0] == '0') {
        if (value != NULL) *value = 0;
        return 1;
    }

    if (p[0] == '-') { // 检查正负号
        negative = 1;
        p++; plen++;

        /* Abort on only a negative sign. */
        /* 如果字符串仅仅包含一个负号， 解析失败。 */
        if (plen == slen)
            return 0;
    }

    /* First digit should be 1-9, otherwise the string should just be 0. */
    /* 第一个数值应该在1-9之间， 如果为0，并且字符串数值不是0， 那么解析失败。 */
    if (p[0] >= '1' && p[0] <= '9') {
        v = p[0]-'0';
        p++; plen++;
    } else if (p[0] == '0' && slen == 1) {
        *value = 0;
        return 1;
    } else {
        return 0;
    }

    /* 游走剩余的字符， 每次将已解析的数值*10， 然后把当前字符作为个位数加上去。 */
    while (plen < slen && p[0] >= '0' && p[0] <= '9') {
        if (v > (ULLONG_MAX / 10)) /* Overflow. */ // 检查是否进位溢出
            return 0;
        v *= 10;

        if (v > (ULLONG_MAX - (p[0]-'0'))) /* Overflow. */ // 检查是否加上个位数后溢出。
            return 0;
        v += p[0]-'0';

        p++; plen++;
    }

    /* Return if not all bytes were used. */
    /* 检查是否所有的字符都处理完成， 例如 "123abc"，这样，将会只处理123， abc将会剩余。 */
    if (plen < slen)
        return 0;

    /* 将正负号添加到数值上。 */
    if (negative) {
        if (v > ((unsigned long long)(-(LLONG_MIN+1))+1)) /* Overflow. */
            return 0;
        if (value != NULL) *value = -v;
    } else {
        if (v > LLONG_MAX) /* Overflow. */
            return 0;
        if (value != NULL) *value = v;
    }
    return 1;
}

/* Convert a string into a long. Returns 1 if the string could be parsed into a
 * (non-overflowing) long, 0 otherwise. The value will be set to the parsed
 * value when appropriate. */
/**
 * 转换字符串为long型数值
 * @param s 待转换字符串
 * @param slen 字符串长度
 * @param lval 用于存储准桓侯的数值
 * @return 成功时返回1， 否则返回0
 */
int string2l(const char *s, size_t slen, long *lval) {
    long long llval;

    // 将数值转为long long. 如果失败则返回0
    if (!string2ll(s,slen,&llval))
        return 0;

    // 如果转换后的数值超过了long的表示范围， 则失败返回0
    if (llval < LONG_MIN || llval > LONG_MAX)
        return 0;

    // 将转换成功的数值强制转换成long。
    *lval = (long)llval;
    return 1;
}

/* Convert a string into a double. Returns 1 if the string could be parsed
 * into a (non-overflowing) double, 0 otherwise. The value will be set to
 * the parsed value when appropriate.
 *
 * Note that this function demands that the string strictly represents
 * a double: no spaces or other characters before or after the string
 * representing the number are accepted. */
/**
 * 将字符串转换为long double数值。
 * @param s 待转换字符串
 * @param slen 待转换字符串长度
 * @param dp 存储转换后的数值。
 * @return 成功时返回1， 失败返回0
 */
int string2ld(const char *s, size_t slen, long double *dp) {
    char buf[256];
    long double value;
    char *eptr;

    if (slen >= sizeof(buf)) return 0;
    memcpy(buf,s,slen);
    buf[slen] = '\0';

    // 调用标准库的函数进行准转换
    errno = 0;
    value = strtold(buf, &eptr);
    if (isspace(buf[0]) || eptr[0] != '\0' ||
        (errno == ERANGE &&
            (value == HUGE_VAL || value == -HUGE_VAL || value == 0)) ||
        errno == EINVAL ||
        isnan(value))
        return 0;

    if (dp) *dp = value;
    return 1;

    // ERANGE Result too large (POSIX.1, C99).
}

/* Convert a double to a string representation. Returns the number of bytes
 * required. The representation should always be parsable by strtod(3).
 * This function does not support human-friendly formatting like ld2string
 * does. It is intented mainly to be used inside t_zset.c when writing scores
 * into a ziplist representing a sorted set. */
/**
 * 将double类型转换为字符串
 * @param buf 用于存放转换后的字符串
 * @param len buf的大小
 * @param value 待转换的数值
 * @return 返回转换后的字符串的长度
 */
int d2string(char *buf, size_t len, double value) {
    // 主要是通过标准库函数sprintf来进行转换。
    if (isnan(value)) {
        len = snprintf(buf,len,"nan");
    } else if (isinf(value)) {
        if (value < 0)
            len = snprintf(buf,len,"-inf");
        else
            len = snprintf(buf,len,"inf");
    } else if (value == 0) {
        /* See: http://en.wikipedia.org/wiki/Signed_zero, "Comparisons". */
        if (1.0/value < 0)
            len = snprintf(buf,len,"-0");
        else
            len = snprintf(buf,len,"0");
    } else {
#if (DBL_MANT_DIG >= 52) && (LLONG_MAX == 0x7fffffffffffffffLL)
        /* Check if the float is in a safe range to be casted into a
         * long long. We are assuming that long long is 64 bit here.
         * Also we are assuming that there are no implementations around where
         * double has precision < 52 bit.
         *
         * Under this assumptions we test if a double is inside an interval
         * where casting to long long is safe. Then using two castings we
         * make sure the decimal part is zero. If all this is true we use
         * integer printing function that is much faster. */
        double min = -4503599627370495; /* (2^52)-1 */
        double max = 4503599627370496; /* -(2^52) */
        if (value > min && value < max && value == ((double)((long long)value)))
            len = ll2string(buf,len,(long long)value);
        else
#endif
            len = snprintf(buf,len,"%.17g",value);
    }

    // DBL_MANT_DIG double类型的尾数位数
    return len;
}

/* Convert a long double into a string. If humanfriendly is non-zero
 * it does not use exponential format and trims trailing zeroes at the end,
 * however this results in loss of precision. Otherwise exp format is used
 * and the output of snprintf() is not modified.
 *
 * The function returns the length of the string or zero if there was not
 * enough buffer room to store it. */
/**
 * long double 转换为 字符串
 * @param buf 用于存放转换后的字符串
 * @param len buf的大小
 * @param value 待转换的数值
 * @param humanfriendly 是否为科学计数法
 * @return 返回转换后的字符串的长度， 失败返回0
 */
int ld2string(char *buf, size_t len, long double value, int humanfriendly) {
    size_t l;

    if (isinf(value)) {
        /* Libc in odd systems (Hi Solaris!) will format infinite in a
         * different way, so better to handle it in an explicit way. */
        if (len < 5) return 0; /* No room. 5 is "-inf\0" */
        if (value > 0) {
            memcpy(buf,"inf",3);
            l = 3;
        } else {
            memcpy(buf,"-inf",4);
            l = 4;
        }
    } else if (humanfriendly) {
        /* We use 17 digits precision since with 128 bit floats that precision
         * after rounding is able to represent most small decimal numbers in a
         * way that is "non surprising" for the user (that is, most small
         * decimal numbers will be represented in a way that when converted
         * back into a string are exactly the same as what the user typed.) */
        l = snprintf(buf,len,"%.17Lf", value);
        if (l+1 > len) return 0; /* No room. */
        /* Now remove trailing zeroes after the '.' */
        if (strchr(buf,'.') != NULL) {
            char *p = buf+l-1;
            while(*p == '0') {
                p--;
                l--;
            }
            if (*p == '.') l--;
        }
    } else {
        l = snprintf(buf,len,"%.17Lg", value);
        if (l+1 > len) return 0; /* No room. */
    }
    buf[l] = '\0';
    return l;
}

/* Generate the Redis "Run ID", a SHA1-sized random number that identifies a
 * given execution of Redis, so that if you are talking with an instance
 * having run_id == A, and you reconnect and it has run_id == B, you can be
 * sure that it is either a different instance or it was restarted. */
/**
 * 为运行的Redis生成一个长度同SHA1的“Run ID”，这样当你链接一个实例的时候，这个实例的ID为A， 那么当你重新连接
 * 这个实例的时候，他的run_id 变成了B， 这样你就能确定你连到了另外一个实例， 或者这个实例被重启。
 */
/**
 * 生成一个随机的二进制字符串。
 * @param p 用于存储生成的随机二进制字符串
 * @param len 参数p的空间长度.
 */
void getRandomHexChars(char *p, unsigned int len) {
    char *charset = "0123456789abcdef";
    unsigned int j;

    /* Global state. */
    static int seed_initialized = 0;
    static unsigned char seed[20]; /* The SHA1 seed, from /dev/urandom. */
    static uint64_t counter = 0; /* The counter we hash with the seed. */

     // 默认情况下使用/dev/urandom来获取seed。
    if (!seed_initialized) {
        /* Initialize a seed and use SHA1 in counter mode, where we hash
         * the same seed with a progressive counter. For the goals of this
         * function we just need non-colliding strings, there are no
         * cryptographic security needs. */
        FILE *fp = fopen("/dev/urandom","r");
        if (fp && fread(seed,sizeof(seed),1,fp) == 1)
            seed_initialized = 1;
        if (fp) fclose(fp);
    }


    if (seed_initialized) {
        // 如果seed有效， 则使用sha1来生成随机串。
        // @see http://blog.csdn.net/knowledgeaaa/article/details/32703317
        while(len) {
            unsigned char digest[20];
            SHA1_CTX ctx;
            unsigned int copylen = len > 20 ? 20 : len;

            /**
             * SHA1_Init() 是一个初始化参数，它用来初始化一个 SHA_CTX 结构，该结构存放弄了生成 SHA1
             * 散列值的一些参数，在应用中可以不用关系该结构的内容。
             */
            SHA1Init(&ctx);
            /**
             * SHA1_Update() 函数正是可以处理大文件的关键。它可以反复调用，比如说我们要计算一个 5G 文件的散列值，
             * 我们可以将该文件分割成多个小的数据块，对每个数据块分别调用一次该函数，这样在最后就能够应用 SHA1_Final()
             * 函数正确计算出这个大文件的 sha1 散列值。
             */
            SHA1Update(&ctx, seed, sizeof(seed));
            SHA1Update(&ctx, (unsigned char*)&counter,sizeof(counter));
            SHA1Final(digest, &ctx);
            counter++;

            memcpy(p,digest,copylen);
            /* Convert to hex digits. */
            for (j = 0; j < copylen; j++) p[j] = charset[p[j] & 0x0F];
            len -= copylen;
            p += copylen;
        }
    } else {
        // 如果/dev/urandom无法使用， 那么将使用当前时间和pid来生成随机ID。
        /* If we can't read from /dev/urandom, do some reasonable effort
         * in order to create some entropy, since this function is used to
         * generate run_id and cluster instance IDs */
        char *x = p;
        unsigned int l = len;
        struct timeval tv;
        pid_t pid = getpid();

        /* Use time and PID to fill the initial array. */
        gettimeofday(&tv,NULL);
        if (l >= sizeof(tv.tv_usec)) {
            memcpy(x,&tv.tv_usec,sizeof(tv.tv_usec));
            l -= sizeof(tv.tv_usec);
            x += sizeof(tv.tv_usec);
        }
        if (l >= sizeof(tv.tv_sec)) {
            memcpy(x,&tv.tv_sec,sizeof(tv.tv_sec));
            l -= sizeof(tv.tv_sec);
            x += sizeof(tv.tv_sec);
        }
        if (l >= sizeof(pid)) {
            memcpy(x,&pid,sizeof(pid));
            l -= sizeof(pid);
            x += sizeof(pid);
        }
        /* Finally xor it with rand() output, that was already seeded with
         * time() at startup, and convert to hex digits. */
        for (j = 0; j < len; j++) {
            p[j] ^= rand();
            p[j] = charset[p[j] & 0x0F];
        }
    }
}

/* Given the filename, return the absolute path as an SDS string, or NULL
 * if it fails for some reason. Note that "filename" may be an absolute path
 * already, this will be detected and handled correctly.
 *
 * The function does not try to normalize everything, but only the obvious
 * case of one or more "../" appearning at the start of "filename"
 * relative path. */
/**
 * 将指定的文件名转换为绝对路径，如果已经是绝对路径了，则直接返回，否则将当前目录作为基目录来计算绝对路径。
 * @param filename 待转换的路径
 * @return 成功时返回SDS结构字符串，失败时返回NULL
 */
sds getAbsolutePath(char *filename) {
    char cwd[1024];
    sds abspath;
    sds relpath = sdsnew(filename);

    relpath = sdstrim(relpath," \r\n\t");
    if (relpath[0] == '/') return relpath; /* Path is already absolute. */

    /* If path is relative, join cwd and relative path. */
    if (getcwd(cwd,sizeof(cwd)) == NULL) {
        sdsfree(relpath);
        return NULL;
    }
    abspath = sdsnew(cwd);
    if (sdslen(abspath) && abspath[sdslen(abspath)-1] != '/')
        abspath = sdscat(abspath,"/");

    /* At this point we have the current path always ending with "/", and
     * the trimmed relative path. Try to normalize the obvious case of
     * trailing ../ elements at the start of the path.
     *
     * For every "../" we find in the filename, we remove it and also remove
     * the last element of the cwd, unless the current cwd is "/". */
    /**
     * 我们假设转换路径为"../../path"， 当前路径为"/home/m/test/"
     * 这样我们循环去掉转换路径中的"../", 同时去掉当前路径后面的一个目录。
     * 例如：
     *  /home/m/test/     ../../path
     *  /home/m/             ../path
     *  /home/                  path
     * 然后将路径进行拼接就得到了绝对路径:/home/path
     */
    while (sdslen(relpath) >= 3 &&
           relpath[0] == '.' && relpath[1] == '.' && relpath[2] == '/')
    {
        sdsrange(relpath,3,-1);
        if (sdslen(abspath) > 1) {
            char *p = abspath + sdslen(abspath)-2;
            int trimlen = 1;

            while(*p != '/') {
                p--;
                trimlen++;
            }
            sdsrange(abspath,0,-(trimlen+1));
        }
    }

    /* Finally glue the two parts together. */
    /* 拼接处理后的路径，得到绝对路径 */
    abspath = sdscatsds(abspath,relpath);
    sdsfree(relpath);
    return abspath;
}

/* Return true if the specified path is just a file basename without any
 * relative or absolute path. This function just checks that no / or \
 * character exists inside the specified path, that's enough in the
 * environments where Redis runs. */
/**
 * 检查指定路径是不是一个文件名，并且不包含任何路径信息。 该函数仅仅检查路径中是否包含路常用的径分隔符.
 * @param path
 * @return 1是 0否
 */
int pathIsBaseName(char *path) {
    return strchr(path,'/') == NULL && strchr(path,'\\') == NULL;
}

#ifdef REDIS_TEST
#include <assert.h>

static void test_string2ll(void) {
    char buf[32];
    long long v;

    /* May not start with +. */
    strcpy(buf,"+1");
    assert(string2ll(buf,strlen(buf),&v) == 0);

    /* Leading space. */
    strcpy(buf," 1");
    assert(string2ll(buf,strlen(buf),&v) == 0);

    /* Trailing space. */
    strcpy(buf,"1 ");
    assert(string2ll(buf,strlen(buf),&v) == 0);

    /* May not start with 0. */
    strcpy(buf,"01");
    assert(string2ll(buf,strlen(buf),&v) == 0);

    strcpy(buf,"-1");
    assert(string2ll(buf,strlen(buf),&v) == 1);
    assert(v == -1);

    strcpy(buf,"0");
    assert(string2ll(buf,strlen(buf),&v) == 1);
    assert(v == 0);

    strcpy(buf,"1");
    assert(string2ll(buf,strlen(buf),&v) == 1);
    assert(v == 1);

    strcpy(buf,"99");
    assert(string2ll(buf,strlen(buf),&v) == 1);
    assert(v == 99);

    strcpy(buf,"-99");
    assert(string2ll(buf,strlen(buf),&v) == 1);
    assert(v == -99);

    strcpy(buf,"-9223372036854775808");
    assert(string2ll(buf,strlen(buf),&v) == 1);
    assert(v == LLONG_MIN);

    strcpy(buf,"-9223372036854775809"); /* overflow */
    assert(string2ll(buf,strlen(buf),&v) == 0);

    strcpy(buf,"9223372036854775807");
    assert(string2ll(buf,strlen(buf),&v) == 1);
    assert(v == LLONG_MAX);

    strcpy(buf,"9223372036854775808"); /* overflow */
    assert(string2ll(buf,strlen(buf),&v) == 0);
}

static void test_string2l(void) {
    char buf[32];
    long v;

    /* May not start with +. */
    strcpy(buf,"+1");
    assert(string2l(buf,strlen(buf),&v) == 0);

    /* May not start with 0. */
    strcpy(buf,"01");
    assert(string2l(buf,strlen(buf),&v) == 0);

    strcpy(buf,"-1");
    assert(string2l(buf,strlen(buf),&v) == 1);
    assert(v == -1);

    strcpy(buf,"0");
    assert(string2l(buf,strlen(buf),&v) == 1);
    assert(v == 0);

    strcpy(buf,"1");
    assert(string2l(buf,strlen(buf),&v) == 1);
    assert(v == 1);

    strcpy(buf,"99");
    assert(string2l(buf,strlen(buf),&v) == 1);
    assert(v == 99);

    strcpy(buf,"-99");
    assert(string2l(buf,strlen(buf),&v) == 1);
    assert(v == -99);

#if LONG_MAX != LLONG_MAX
    strcpy(buf,"-2147483648");
    assert(string2l(buf,strlen(buf),&v) == 1);
    assert(v == LONG_MIN);

    strcpy(buf,"-2147483649"); /* overflow */
    assert(string2l(buf,strlen(buf),&v) == 0);

    strcpy(buf,"2147483647");
    assert(string2l(buf,strlen(buf),&v) == 1);
    assert(v == LONG_MAX);

    strcpy(buf,"2147483648"); /* overflow */
    assert(string2l(buf,strlen(buf),&v) == 0);
#endif
}

static void test_ll2string(void) {
    char buf[32];
    long long v;
    int sz;

    v = 0;
    sz = ll2string(buf, sizeof buf, v);
    assert(sz == 1);
    assert(!strcmp(buf, "0"));

    v = -1;
    sz = ll2string(buf, sizeof buf, v);
    assert(sz == 2);
    assert(!strcmp(buf, "-1"));

    v = 99;
    sz = ll2string(buf, sizeof buf, v);
    assert(sz == 2);
    assert(!strcmp(buf, "99"));

    v = -99;
    sz = ll2string(buf, sizeof buf, v);
    assert(sz == 3);
    assert(!strcmp(buf, "-99"));

    v = -2147483648;
    sz = ll2string(buf, sizeof buf, v);
    assert(sz == 11);
    assert(!strcmp(buf, "-2147483648"));

    v = LLONG_MIN;
    sz = ll2string(buf, sizeof buf, v);
    assert(sz == 20);
    assert(!strcmp(buf, "-9223372036854775808"));

    v = LLONG_MAX;
    sz = ll2string(buf, sizeof buf, v);
    assert(sz == 19);
    assert(!strcmp(buf, "9223372036854775807"));
}

#define UNUSED(x) (void)(x)
int utilTest(int argc, char **argv) {
    UNUSED(argc);
    UNUSED(argv);

    test_string2ll();
    test_string2l();
    test_ll2string();
    return 0;
}
#endif
