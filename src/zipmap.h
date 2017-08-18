/* String -> String Map data structure optimized for size.
 *
 * See zipmap.c for more info.
 *
 * --------------------------------------------------------------------------
 *
 * Copyright (c) 2009-2010, Salvatore Sanfilippo <antirez at gmail dot com>
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

#ifndef _ZIPMAP_H
#define _ZIPMAP_H

/**
 * 创建一个新的zip map
 * @return 返回map首地址
 */
unsigned char *zipmapNew(void);
/* Set key to value, creating the key if it does not already exist.
 * If 'update' is not NULL, *update is set to 1 if the key was
 * already preset, otherwise to 0. */
/**
 * 根据键名设置对应的键值
 * @param zm zipmap 地址
 * @param key 键名
 * @param klen 键名长度
 * @param val 值
 * @param vlen 值长度
 * @param update 是否更新标记， 如果不为null， 则如果设置的键名已经存在则为1，不存在，新建的键名则为0
 * @return 返回该zipmap
 */
unsigned char *zipmapSet(unsigned char *zm, unsigned char *key, unsigned int klen, unsigned char *val, unsigned int vlen, int *update);
/**
 * 从zipmap中删除元素。
 * @param zm zipmap地址
 * @param key 待删除的键名
 * @param klen 待删除键名长度
 * @param deleted 如果不为NULL，则如果指定元素被删除则设置为1，否则设置为0.
 * @param 删除键值后的zipmap
 */
unsigned char *zipmapDel(unsigned char *zm, unsigned char *key, unsigned int klen, int *deleted);
/**
 * 将map游标地址推送到第一个元素的地址, 在调用next的时候需要执行该操作
 * @return 第一个元素的地址
 */
unsigned char *zipmapRewind(unsigned char *zm);
/**
 * 遍历zipmap，并将当前键值保存在对应的变量
 * @param zm zipmap地址，不是首地址，第一次是rewind之后的地址， 之后是每次next返回的地址
 * @param key 当前键名
 * @param klen 当前键名长度
 * @param value 当前值
 * @param vlen 当前value的长度
 * @return 下一次调用next时应该传的map的地址
 */
unsigned char *zipmapNext(unsigned char *zm, unsigned char **key, unsigned int *klen, unsigned char **value, unsigned int *vlen);
/**
 * 通过键名获取map中对应的值
 * @param zm map地址
 * @param key 键名
 * @param klen 键名长度
 * @param value 用于保存键值
 * @param vlen 用于保存键值长度
 * @return 如果找到键名则返回1， 否则返回0
 */
int zipmapGet(unsigned char *zm, unsigned char *key, unsigned int klen, unsigned char **value, unsigned int *vlen);
/**
 * 检查键名是否存在
 * @param zm map地址
 * @param key 键名
 * @param klen 键名长度
 * @return 1：存在 0：不存在
 */
int zipmapExists(unsigned char *zm, unsigned char *key, unsigned int klen);
/**
 * 获取map元素个数
 * @param zm map地址
 * @return 元素个数
 */
unsigned int zipmapLen(unsigned char *zm);
/**
 * 获取该map的字节长度，用于序列化到磁盘或者其他用户。
 * @param zm map首地址
 * @return map的字节数
 */
size_t zipmapBlobLen(unsigned char *zm);
/**
 * 循环打印出map的结构, Repr => repeat print ?
 * 类似于：{status 2}{key 4}name{value 4}micl[......]{key 3}age{value 2}25{end}
 * @param p zipmap收地址
 */
void zipmapRepr(unsigned char *p);

#ifdef REDIS_TEST
int zipmapTest(int argc, char *argv[]);
#endif

#endif
