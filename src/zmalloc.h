/* zmalloc - total amount of allocated memory aware version of malloc()
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

#ifndef __ZMALLOC_H
#define __ZMALLOC_H

/* Double expansion needed for stringification of macro values. */
#define __xstr(s) __str(s)
#define __str(s) #s

// Redis内存管理使用的库，
// http://www.360doc.com/content/13/0915/09/8363527_314549128.shtml
#if defined(USE_TCMALLOC) // tcmalloc
#define ZMALLOC_LIB ("tcmalloc-" __xstr(TC_VERSION_MAJOR) "." __xstr(TC_VERSION_MINOR))
#include <google/tcmalloc.h>
#if (TC_VERSION_MAJOR == 1 && TC_VERSION_MINOR >= 6) || (TC_VERSION_MAJOR > 1)
#define HAVE_MALLOC_SIZE 1
#define zmalloc_size(p) tc_malloc_size(p)
#else
#error "Newer version of tcmalloc required"
#endif

#elif defined(USE_JEMALLOC) // jemalloc
#define ZMALLOC_LIB ("jemalloc-" __xstr(JEMALLOC_VERSION_MAJOR) "." __xstr(JEMALLOC_VERSION_MINOR) "." __xstr(JEMALLOC_VERSION_BUGFIX))
#include <jemalloc/jemalloc.h>
#if (JEMALLOC_VERSION_MAJOR == 2 && JEMALLOC_VERSION_MINOR >= 1) || (JEMALLOC_VERSION_MAJOR > 2)
#define HAVE_MALLOC_SIZE 1
#define zmalloc_size(p) je_malloc_usable_size(p)
#else
#error "Newer version of jemalloc required"
#endif

#elif defined(__APPLE__)
#include <malloc/malloc.h>
#define HAVE_MALLOC_SIZE 1
#define zmalloc_size(p) malloc_size(p)
#endif

#ifndef ZMALLOC_LIB
#define ZMALLOC_LIB "libc"
#endif

/* We can enable the Redis defrag capabilities only if we are using Jemalloc
 * and the version used is our special version modified for Redis having
 * the ability to return per-allocation fragmentation hints. */
// 如果想使用redis的碎片回收功能，只能使用我们修改过的Jemalloc版本， 它能够获取以前申请内存遗留下来的内存碎片。
#if defined(USE_JEMALLOC) && defined(JEMALLOC_FRAG_HINT)
#define HAVE_DEFRAG
#endif

void *zmalloc(size_t size);
void *zcalloc(size_t size);
void *zrealloc(void *ptr, size_t size);
/**
 * 释放掉申请的内存空间。
 * @param ptr 待释放空间地址
 */
void zfree(void *ptr);
/**
 * 将传入的字符串复制然后返回复制后的字符串。
 * @param s 源字符串地址
 * @return 复制后的字符串地址
 */
char *zstrdup(const char *s);
/**
 * 原子方式获取已使用内存大小。
 * 该方法使用 __atomic_load_n 来进行操作
 * @see https://gcc.gnu.org/onlinedocs/gcc/_005f_005fatomic-Builtins.html
 * 内建函数: type __atomic_load_n (type *ptr, int memorder)
 *   该内建函数实现原子加载操作，他返回指针ptr中的内容。
 *   可用的memorder取值为： __ATOMIC_RELAXED, __ATOMIC_SEQ_CST, __ATOMIC_ACQUIRE, and __ATOMIC_CONSUME.
 * @return 已申请内容大小
 */
size_t zmalloc_used_memory(void);
/**
 * 设置内存溢出时的处理函数
 * @param callable 溢出处理函数
 */
void zmalloc_set_oom_handler(void (*oom_handler)(size_t));
/**
 * 获取指定大写与已申请内存的比例
 * @return RSS / allocated-bytes
 */
float zmalloc_get_fragmentation_ratio(size_t rss);
/**
 * 获取内存使用量
 * @return 返回实际使用的内存字节数
 */
size_t zmalloc_get_rss(void);
/**
 * 获取写时拷贝的内存大小
 * 在 RDB 持久化过程中，父进程继续提供服务，子进程进行 RDB 持久化。持久化完毕后，会调用 zmalloc_get_private_dirty()
 * 获取写时拷贝的内存大小，此值实际为子进程在 RDB 持久化操作过程中所消耗的内存。
 * @see http://wiki.jikexueyuan.com/project/redis/memory-data-management.html
 * @return 空间大小
 */
size_t zmalloc_get_private_dirty(long pid);
/**
 * 根据pid值获取指定或自己的/proc/xxxx/smaps文件的输出获取指定属性的总和。
 * 例如：zmalloc_get_smap_bytes_by_field("Rss:",-1)
 * @see https://zhidao.baidu.com/question/2077299873435988108.html
 * @param field 属性名称
 * @param pid 进程id， -1表示当前进程
 * @return 指定属性值总和
 */
size_t zmalloc_get_smap_bytes_by_field(char *field, long pid);
/**
 * 获取物理内存容量
 * @return 成功时返回内存容量，否则返回0
 */
size_t zmalloc_get_memory_size(void);
/**
 * 调用libc中的free()来释放申请的内存
 */
void zlibc_free(void *ptr);

#ifdef HAVE_DEFRAG
void zfree_no_tcache(void *ptr);
void *zmalloc_no_tcache(size_t size);
#endif

#ifndef HAVE_MALLOC_SIZE
size_t zmalloc_size(void *ptr);
#endif

#endif /* __ZMALLOC_H */
