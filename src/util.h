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
uint32_t digits10(uint64_t v);
uint32_t sdigits10(int64_t v);
int ll2string(char *s, size_t len, long long value);
int string2ll(const char *s, size_t slen, long long *value);
int string2l(const char *s, size_t slen, long *value);
int string2ld(const char *s, size_t slen, long double *dp);
int d2string(char *buf, size_t len, double value);
int ld2string(char *buf, size_t len, long double value, int humanfriendly);
sds getAbsolutePath(char *filename);
int pathIsBaseName(char *path);

#ifdef REDIS_TEST
int utilTest(int argc, char **argv);
#endif

#endif
