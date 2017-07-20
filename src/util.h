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

#ifndef __REDIS_UTIL_H
#define __REDIS_UTIL_H

#include <stdint.h>
#include "sds.h"

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
int stringmatchlen(const char *p, int plen, const char *s, int slen, int nocase);
/**
 * 测试字符串是否与匹配模式相匹配，
 * @see stringmatchlen()
 * @param pattern 匹配模式
 * @param string 待匹配字符串
 * @param nocase 是否忽略大小写 1：忽略 0：不忽略
 * @return1：匹配成功  0：匹配失败
 */
int stringmatch(const char *p, const char *s, int nocase);
/**
 * 将可读的空间大小转换为长整形字节数。 例如， "1Gb" 转换 为1073741824, 也就是 1024*1024*1024
 * @param p 待转换的字符串
 * @param err 错误代码， 1 成功 0 失败， 如果err为null， 则不赋值。
 * @return 成功时返回字节数，失败时返回0.
 * */
long long memtoll(const char *p, int *err);
/**
 * 返回一个十进制数值的位数，例如10, 返回2, 100返回3
 * @param v 数值
 * @return 十进制数值的位数
 */
uint32_t digits10(uint64_t v);
/**
 * 返回一个带符号的十进制数值的位数，例如10, 返回2, 100返回3
 * @param v 数值
 * @return 十进制数值的位数
 */
uint32_t sdigits10(int64_t v);
/**
 * 将数值准换成字符串。
 *@param dst 保存字符串的空间
 *@param dstlen 空间长度
 *@param svalue 待转换成字符串的数值
 *@return 成功返回目标字符串长度， 失败返回0
 */
int ll2string(char *s, size_t len, long long value);
/**
 * 将字符串转换为long long型数值。 转换时采取严格模式，字符串开始或者末尾不能包含空格或者其他不用来表示数值的字符。
 * 如果字符串数值一开始就是0，并且不是0，则解析失败。 例如:"0123"， 将解析失败。
 * @param s 待转换字符串
 * @param slen 字符串长度
 * @param value 用于存储准桓侯的数值
 * @return 成功时返回1， 否则返回0
 */
int string2ll(const char *s, size_t slen, long long *value);
/**
 * 转换字符串为long型数值
 * @param s 待转换字符串
 * @param slen 字符串长度
 * @param lval 用于存储准桓侯的数值
 * @return 成功时返回1， 否则返回0
 */
int string2l(const char *s, size_t slen, long *value);
/**
 * 将字符串转换为long double数值。
 * @param s 待转换字符串
 * @param slen 待转换字符串长度
 * @param dp 存储转换后的数值。
 * @return 成功时返回1， 失败返回0
 */
int string2ld(const char *s, size_t slen, long double *dp);
/**
 * 将double类型转换为字符串
 * @param buf 用于存放转换后的字符串
 * @param len buf的大小
 * @param value 待转换的数值
 * @return 返回转换后的字符串的长度
 */
int d2string(char *buf, size_t len, double value);
/**
 * long double 转换为 字符串
 * @param buf 用于存放转换后的字符串
 * @param len buf的大小
 * @param value 待转换的数值
 * @param humanfriendly 是否为科学计数法
 * @return 返回转换后的字符串的长度， 失败返回0
 */
int ld2string(char *buf, size_t len, long double value, int humanfriendly);
/**
 * 将指定的文件名转换为绝对路径，如果已经是绝对路径了，则直接返回，否则将当前目录作为基目录来计算绝对路径。
 * @param filename 待转换的路径
 * @return 成功时返回SDS结构字符串，失败时返回NULL
 */
sds getAbsolutePath(char *filename);
/**
 * 检查指定路径是不是一个文件名，并且不包含任何路径信息。 该函数仅仅检查路径中是否包含路常用的径分隔符.
 * @param path
 * @return 1是 0否
 */
int pathIsBaseName(char *path);

#ifdef REDIS_TEST
/**
 * Util 的单元测试函数
 * */
int utilTest(int argc, char **argv);
#endif

#endif
