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

#include <stdio.h>
#include <stdlib.h>

/* This function provide us access to the original libc free(). This is useful
 * for instance to free results obtained by backtrace_symbols(). We need
 * to define this function before including zmalloc.h that may shadow the
 * free implementation if we use jemalloc or another non standard allocator. */
// 该函数使我们能够直接访问libc的free()函数，主要是帮助实例在释放由backtrace_symbols()结果所产生的空间。
// 我们需要在包含zmalloc.h之前定义该函数， 因为free()有可能会被其他的的malloc实现所覆盖，例如jemalloc等
/**
 * 调用libc中的free()来释放申请的内存
 */
void zlibc_free(void *ptr) {
    free(ptr);
}

#include <string.h>
#include <pthread.h>
#include "config.h"
#include "zmalloc.h"
#include "atomicvar.h"

// PREFIX_SIZE 是指在使用原始的malloc的时候，申请的每一块内存都预留一部分空间用来存储
// 其他信息， 这里PREFIX_SIZE大小的预留空间主要是用来存储申请的内存大小。
// HAVE_MALLOC_SIZE 用来确定系统是否有函数malloc_size
#ifdef HAVE_MALLOC_SIZE
#define PREFIX_SIZE (0)
#else
// Sun微处理器 https://baike.baidu.com/item/SPARC%E5%A4%84%E7%90%86%E5%99%A8/3028578
#if defined(__sun) || defined(__sparc) || defined(__sparc__)
#define PREFIX_SIZE (sizeof(long long))
#else
#define PREFIX_SIZE (sizeof(size_t))
#endif
#endif

/* Explicitly override malloc/free etc when using tcmalloc. */
/** 根据使用不同的内存管理工具映射内存管理函数 */
#if defined(USE_TCMALLOC)
// 将malloc函数映射到tc_malloc
#define malloc(size) tc_malloc(size)
#define calloc(count,size) tc_calloc(count,size)
#define realloc(ptr,size) tc_realloc(ptr,size)
#define free(ptr) tc_free(ptr)
#elif defined(USE_JEMALLOC)
// 将malloc函数映射到jemalloc
#define malloc(size) je_malloc(size)
#define calloc(count,size) je_calloc(count,size)
#define realloc(ptr,size) je_realloc(ptr,size)
#define free(ptr) je_free(ptr)
#define mallocx(size,flags) je_mallocx(size,flags)
#define dallocx(ptr,flags) je_dallocx(ptr,flags)
#endif

/**
 * 原子增加内存使用统计量
 * @see https://gcc.gnu.org/onlinedocs/gcc/_005f_005fatomic-Builtins.html
 * Built-in Functions for Memory Model Aware Atomic Operations
 * Built-in Function: type __atomic_add_fetch (type *ptr, type val, int memorder)
 * Built-in Function: type __atomic_sub_fetch (type *ptr, type val, int memorder)
 * Built-in Function: type __atomic_and_fetch (type *ptr, type val, int memorder)
 * Built-in Function: type __atomic_xor_fetch (type *ptr, type val, int memorder)
 * Built-in Function: type __atomic_or_fetch (type *ptr, type val, int memorder)
 * Built-in Function: type __atomic_nand_fetch (type *ptr, type val, int memorder)
 *
 * These built-in functions perform the operation suggested by the name, and return
 * the result of the operation. Operations on pointer arguments are performed as if
 * the operands were of the uintptr_t type. That is, they are not scaled by the size
 * of the type to which the pointer points.
 * { *ptr op= val; return *ptr; }
 */
#define update_zmalloc_stat_alloc(__n) do { \
    size_t _n = (__n); \
    if (_n&(sizeof(long)-1)) _n += sizeof(long)-(_n&(sizeof(long)-1)); \
    atomicIncr(used_memory,__n); \
} while(0)
/**
 * 原子减少内存使用统计量
 */
#define update_zmalloc_stat_free(__n) do { \
    size_t _n = (__n); \
    if (_n&(sizeof(long)-1)) _n += sizeof(long)-(_n&(sizeof(long)-1)); \
    atomicDecr(used_memory,__n); \
} while(0)

// 已申请内存空间总大小
static size_t used_memory = 0;
// 之前这个变量是用来作为线程锁的，
// 现在搜不到哪里使用了，应该是被__atomic_fetch_XXX这些给代替了吧。
// 之前是这样：
// pthread_mutex_lock(&used_memory_mutex);  \
// used_memory += _n; \
// pthread_mutex_unlock(&used_memory_mutex);
pthread_mutex_t used_memory_mutex = PTHREAD_MUTEX_INITIALIZER;

/**
 * 默认的内存溢出处理函数
 * 显示内存溢出信息后， 直接退出程序。
 * @param size 溢出的空间大小
 */
static void zmalloc_default_oom(size_t size) {
    fprintf(stderr, "zmalloc: Out of memory trying to allocate %zu bytes\n",
        size);
    fflush(stderr);
    abort();
}

/** 内存溢出处理函数， 这里是默认的处理方法。 */
static void (*zmalloc_oom_handler)(size_t) = zmalloc_default_oom;

/**
 * 申请指定大小的内存空间
 * @param size 空间大小
 * @return 返回申请空间的收地址
 */
void *zmalloc(size_t size) {
    // 申请指定大小空间
    void *ptr = malloc(size+PREFIX_SIZE);
    // 如果申请失败， 进行内存溢出处理
    if (!ptr) zmalloc_oom_handler(size);
#ifdef HAVE_MALLOC_SIZE
    // 如果你能够直接获取到申请的内存大小， 则使用提供的函数更新内存统计量
    update_zmalloc_stat_alloc(zmalloc_size(ptr));
    return ptr;
#else
    // 否则手动封信内存使用量。
    // 在空间头部加入空间头部信息， 这里为空间大小
    *((size_t*)ptr) = size;
    // 更新内存使用量
    update_zmalloc_stat_alloc(size+PREFIX_SIZE);
    // 返回去掉头部信息后，能够用来存储数据的空间的起始地址
    return (char*)ptr+PREFIX_SIZE;
#endif
}

/* Allocation and free functions that bypass the thread cache
 * and go straight to the allocator arena bins.
 * Currently implemented only for jemalloc. Used for online defragmentation. */
// 绕过malloc和free的线程缓存， 直接操作arena bins。
/**
 * @see http://www.cnblogs.com/alisecurity/p/5486458.html
 * 这就说明它是通过brk系统调用实现的。并且，还可以看出虽然我们只申请了1000字节的数据，但是系统却分配了132KB大小的堆，
 * 这是为什么呢？原来这132KB的堆空间叫做arena，此时因为是主线程分配的，所以叫做main arena(每个arena中含有多个chunk，
 * 这些chunk以链表的形式加以组织)。由于132KB比1000字节大很多，所以主线程后续再声请堆空间的话，就会先从这132KB的剩余部分中申请，
 * 直到用完或不够用的时候，再通过增加program break location的方式来增加main arena的大小。同理，当main
 * arena中有过多空闲内存的时候，也会通过减小program break location的方式来缩小main arena的大小。
 *
 * 在主线程调用free之后：从内存布局可以看出程序的堆空间并没有被释放掉，原来调用free函数释放已经分配了的空间并非直接“返还”给系统，
 * 而是由glibc 的malloc库函数加以管理。它会将释放的chunk添加到main arenas的bin(这是一种用于存储同类型free chunk
 * 的双链表数据结构，后问会加以详细介绍)中。在这里，记录空闲空间的freelist数据结构称之为bins。之后当用户再次调用malloc申请堆
 * 空间的时候，glibc malloc会先尝试从bins中找到一个满足要求的chunk，如果没有才会向操作系统申请新的堆空间。
 */
#ifdef HAVE_DEFRAG
/**
 * 从arena bins申请指定大小的内存空间
 * @param size
 * @return 返回申请空间的数据地址
 */
void *zmalloc_no_tcache(size_t size) {
    // 调用mallocx来从arena bins申请空间。
    void *ptr = mallocx(size+PREFIX_SIZE, MALLOCX_TCACHE_NONE);
    // 如果申请失败， 则进行内存溢出处理
    if (!ptr) zmalloc_oom_handler(size);
    // 更新内存使用统计量
    update_zmalloc_stat_alloc(zmalloc_size(ptr));
    // 返回空间地址
    return ptr;
}
/**
 * 从arena bins释放指定的内存空间
 * @param ptr 空间指针首地址
 */
void zfree_no_tcache(void *ptr) {
    // 如果为null， 怒进行处理
    if (ptr == NULL) return;
    // 更新内存使用统计量
    update_zmalloc_stat_free(zmalloc_size(ptr));
    // 调用dallocx来从arena bins释放空间。
    dallocx(ptr, MALLOCX_TCACHE_NONE);
}
#endif

/**
 * 申请一段内存空间， 并初始化空间值。
 * @param size 申请空间大小
 * @return 申请空间的首地址
 */
void *zcalloc(size_t size) {
    // 调用calloc函数申请内存
    void *ptr = calloc(1, size+PREFIX_SIZE);

    // 如果失败， 进行内存溢出处理
    if (!ptr) zmalloc_oom_handler(size);
#ifdef HAVE_MALLOC_SIZE
    // 更新内存使用率
    update_zmalloc_stat_alloc(zmalloc_size(ptr));
    // 返回空间地址
    return ptr;
#else
    // 将空间大小写到空间头部
    *((size_t*)ptr) = size;
    //更新内存使用空间大小
    update_zmalloc_stat_alloc(size+PREFIX_SIZE);
    // 将内存数据区域返回
    return (char*)ptr+PREFIX_SIZE;
#endif
}

/**
 * 将申请的空间大小改为新的大小
 * @param ptr 原始空间指针
 * @param size 新空间大小
 * @return 空间大小变更后的空间指针
 */
void *zrealloc(void *ptr, size_t size) {
#ifndef HAVE_MALLOC_SIZE
    void *realptr; // 真实空间地址
#endif
    size_t oldsize;
    void *newptr;

    // 如果prt为null， 那么直接申请空间就好。
    if (ptr == NULL) return zmalloc(size);
#ifdef HAVE_MALLOC_SIZE
    // 获取老的空间大小用于内存统计用。
    oldsize = zmalloc_size(ptr);
    // 调用relloc重新申请内存。
    newptr = realloc(ptr,size);
    // 如果申请失败， 则内存溢出处理
    if (!newptr) zmalloc_oom_handler(size);

    // 从内存统计中去掉老的内存信息
    update_zmalloc_stat_free(oldsize);
    // 从内存统计中加上新的内存信息
    update_zmalloc_stat_alloc(zmalloc_size(newptr));
    return newptr;
#else

    // 真是地址由指定定时前去前缀信息获得。
    realptr = (char*)ptr-PREFIX_SIZE;
    // 从头信息中获取老的空间大小
    oldsize = *((size_t*)realptr);
    // 重新申请新的空间
    newptr = realloc(realptr,size+PREFIX_SIZE);
    // 如果申请失败， 则内存溢出处理
    if (!newptr) zmalloc_oom_handler(size);

    // 在新空间按的头部加上新的空间大小
    *((size_t*)newptr) = size;
    // 从内存统计中去掉老的内存信息
    update_zmalloc_stat_free(oldsize);
    // 从内存统计中加上新的内存信息
    update_zmalloc_stat_alloc(size);
    // 返回空间的数据区域
    return (char*)newptr+PREFIX_SIZE;
#endif
}

/* Provide zmalloc_size() for systems where this function is not provided by
 * malloc itself, given that in that case we store a header with this
 * information as the first bytes of every allocation. */
#ifndef HAVE_MALLOC_SIZE
/**
 * 如果没有HAVE_MALLOC_SIZE， 那么我们需要手动实现一个zmalloc_size。
 * 这样， 由于我们在每次申请的内存空间的头部加入了该空间的大小， 所以我们直接取这个值。
 * @param ptr 空间指针
 * @return 返回该指针所指向的空间的大小
 */
size_t zmalloc_size(void *ptr) {
    // 获取空间的真实地址。
    void *realptr = (char*)ptr-PREFIX_SIZE;
    // 从头部获取该空间的真是大小。
    size_t size = *((size_t*)realptr);
    /* Assume at least that all the allocations are padded at sizeof(long) by
     * the underlying allocator. */
    // 由于在 update_zmalloc_stat_alloc 中存在内存对齐到long的大小的操作， 所以我们在获取申请空间
    // 大小的时候要把实际统计的部分计算出来。
    // 假设 申请的大小为50，sizeof(long)=4
    // 50 & (4-1) = 2
    // 50 + 4 -2 = 52
    // 所以，如果申请50个字节， 我们实际上统计的是52个字节的大小
    if (size&(sizeof(long)-1)) size += sizeof(long)-(size&(sizeof(long)-1));
    // 将使用空间的大小加上前缀大小获得申请的空间的大小
    return size+PREFIX_SIZE;
}
#endif

/**
 * 释放掉申请的内存空间。
 * @param ptr 待释放空间地址
 */
void zfree(void *ptr) {
#ifndef HAVE_MALLOC_SIZE
    void *realptr; // 空间真是起始地址
    size_t oldsize; // 空间大小
    // TODO ： 为什么这两行不写到 #else里面？
#endif

    if (ptr == NULL) return;
#ifdef HAVE_MALLOC_SIZE
    // 如果存在zmalloc_size， 使用该函数计算空间到校
    update_zmalloc_stat_free(zmalloc_size(ptr));
    free(ptr);
#else
    // 否则手动计算空间大小。
    // 在内容申请时，每块空间的头部被放入了该空间的真正大小，所以空间位置减去前缀的长度才能
    // 得到真实的空间地址。
    realptr = (char*)ptr-PREFIX_SIZE;
    // 由头部信息获取空间真是大小
    oldsize = *((size_t*)realptr);
    // 将总内存使用大小减去这次释放的大小。
    update_zmalloc_stat_free(oldsize+PREFIX_SIZE);
    // 释放申请空间。
    free(realptr);
#endif
}

/**
 * 将传入的字符串复制然后返回复制后的字符串。
 * @param s 源字符串地址
 * @return 复制后的字符串地址
 */
char *zstrdup(const char *s) {
    size_t l = strlen(s)+1;
    char *p = zmalloc(l);

    memcpy(p,s,l);
    return p;
}

/**
 * 原子方式获取已使用内存大小。
 * 该方法使用 __atomic_load_n 来进行操作
 * @see https://gcc.gnu.org/onlinedocs/gcc/_005f_005fatomic-Builtins.html
 * 内建函数: type __atomic_load_n (type *ptr, int memorder)
 *   该内建函数实现原子加载操作，他返回指针ptr中的内容。
 *   可用的memorder取值为： __ATOMIC_RELAXED, __ATOMIC_SEQ_CST, __ATOMIC_ACQUIRE, and __ATOMIC_CONSUME.
 * @return 已申请内容大小
 */
size_t zmalloc_used_memory(void) {
    size_t um;
    atomicGet(used_memory,um);
    return um;
}

/**
 * 设置内存溢出时的处理函数
 * @param callable 溢出处理函数
 */
void zmalloc_set_oom_handler(void (*oom_handler)(size_t)) {
    zmalloc_oom_handler = oom_handler;
}

/* Get the RSS information in an OS-specific way.
 *
 * WARNING: the function zmalloc_get_rss() is not designed to be fast
 * and may not be called in the busy loops where Redis tries to release
 * memory expiring or swapping out objects.
 *
 * For this kind of "fast RSS reporting" usages use instead the
 * function RedisEstimateRSS() that is a much faster (and less precise)
 * version of the function. */

#if defined(HAVE_PROC_STAT)
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
/**
 * 通过 /proc/pid/stat 获取内存使用量。
 * pid为当前进程的进程号。读取到的不是byte数，而是内存页数。
 * 通过系统调用sysconf(_SC_PAGESIZE)可以获得当前系统的内存页大小。
 * @see http://blog.csdn.net/cybertan/article/details/7596633
 * @return 返回实际使用的内存字节数
 */
size_t zmalloc_get_rss(void) {
    int page = sysconf(_SC_PAGESIZE); // 内存页大小
    size_t rss; // rss值
    char buf[4096]; // stat文件内容
    char filename[256];// stat文件名称
    int fd, count; // stat文件描述符，rss列索引号
    char *p, *x;

    snprintf(filename,256,"/proc/%d/stat",getpid());
    if ((fd = open(filename,O_RDONLY)) == -1) return 0;
    if (read(fd,buf,4096) <= 0) {
        close(fd);
        return 0;
    }
    close(fd);
    p = buf;
    /**
     * stat 文件格式示例：(文件仅有一行，这里为了方便阅读添加了换行)
     * 12373 (php) R 12360 12373 12360 34816 12373 4210688 127951 0 0 0 33314 3574 0 0
     * 20 0 1 0 132914262 53088256 10313 4294967295 134512640 138181712 3213781136 0 0
     * 0 0 4096 67108864 0 0 0 17 0 0 0 0 0 0 138185808 138520526 162045952 3213789414
     * 3213789446 3213789446 3213791215 0
     * count =23 是因为23列就是该任务当前驻留物理地址空间的大小
     */
    count = 23; /* RSS is the 24th field in /proc/<pid>/stat */
    /** 将指针推到指向RSS的地方。 */
    while(p && count--) {
        p = strchr(p,' ');
        if (p) p++;
    }
    if (!p) return 0;
    x = strchr(p,' '); // 将rss数值的结尾赋值给x
    if (!x) return 0;
    *x = '\0'; // 这个时候， p 到 x之间的内容就是rss

    rss = strtoll(p,NULL,10);
    rss *= page;
    return rss;
}
#elif defined(HAVE_TASKINFO)
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/sysctl.h>
#include <mach/task.h>
#include <mach/mach_init.h>
/**
 * 由task_info方法获取rss
 * @return 返回实际使用的内存字节数
 */
size_t zmalloc_get_rss(void) {
    task_t task = MACH_PORT_NULL;
    struct task_basic_info t_info;
    mach_msg_type_number_t t_info_count = TASK_BASIC_INFO_COUNT;

    if (task_for_pid(current_task(), getpid(), &task) != KERN_SUCCESS)
        return 0;
    task_info(task, TASK_BASIC_INFO, (task_info_t)&t_info, &t_info_count);

    return t_info.resident_size;
}
#else
/**
 * 如果无法使用系统提供的方法获取rss， 那么我们将zmalloc中统计的内容使用量作为rss。
 * @return 返回实际使用的内存字节数
 */
size_t zmalloc_get_rss(void) {
    /* If we can't get the RSS in an OS-specific way for this system just
     * return the memory usage we estimated in zmalloc()..
     *
     * Fragmentation will appear to be always 1 (no fragmentation)
     * of course... */
    return zmalloc_used_memory();
}
#endif

/* Fragmentation = RSS / allocated-bytes */
/**
 * 获取指定大写与已申请内存的比例
 * @return RSS / allocated-bytes
 */
float zmalloc_get_fragmentation_ratio(size_t rss) {
    return (float)rss/zmalloc_used_memory();
}

/* Get the sum of the specified field (converted form kb to bytes) in
 * /proc/self/smaps. The field must be specified with trailing ":" as it
 * apperas in the smaps output.
 *
 * If a pid is specified, the information is extracted for such a pid,
 * otherwise if pid is -1 the information is reported is about the
 * current process.
 *
 * Example: zmalloc_get_smap_bytes_by_field("Rss:",-1);
 */
#if defined(HAVE_PROC_SMAPS)
/**
 * 根据pid值获取指定或自己的/proc/xxxx/smaps文件的输出获取指定属性的总和。
 * 例如：zmalloc_get_smap_bytes_by_field("Rss:",-1)
 * @see https://zhidao.baidu.com/question/2077299873435988108.html
 * @param field 属性名称
 * @param pid 进程id， -1表示当前进程
 * @return 指定属性值总和
 */
size_t zmalloc_get_smap_bytes_by_field(char *field, long pid) {
    char line[1024]; // smaps文件的行缓存
    size_t bytes = 0; // 总和
    int flen = strlen(field);
    FILE *fp;

    if (pid == -1) {
        fp = fopen("/proc/self/smaps","r");
    } else {
        char filename[128];
        snprintf(filename,sizeof(filename),"/proc/%ld/smaps",pid);
        fp = fopen(filename,"r");
    }

    if (!fp) return 0;

    // 循环读取文件每一行， 匹配行首， 如果匹配。 将对应的值加到合计里面。
    // 每行格式 举例：Private_Clean:        44 kB
    while(fgets(line,sizeof(line),fp) != NULL) {
        if (strncmp(line,field,flen) == 0) {
            char *p = strchr(line,'k');
            if (p) {
                *p = '\0';
                bytes += strtol(line+flen,NULL,10) * 1024;
            }
        }
    }
    fclose(fp);
    return bytes;
}
#else
/**
 * 如果没有 /proc/xxxx/smaps 文件， 直接返回0.
 * @param field 属性名称
 * @param pid 进程id， -1表示当前进程
 * @return 指定属性值总和
 */
size_t zmalloc_get_smap_bytes_by_field(char *field, long pid) {
    ((void) field);
    ((void) pid);
    return 0;
}
#endif

/**
 * 获取写时拷贝的内存大小
 * 在 RDB 持久化过程中，父进程继续提供服务，子进程进行 RDB 持久化。持久化完毕后，会调用 zmalloc_get_private_dirty()
 * 获取写时拷贝的内存大小，此值实际为子进程在 RDB 持久化操作过程中所消耗的内存。
 * @see http://wiki.jikexueyuan.com/project/redis/memory-data-management.html
 * @return 空间大小
 */
size_t zmalloc_get_private_dirty(long pid) {
    return zmalloc_get_smap_bytes_by_field("Private_Dirty:",pid);
}

/* Returns the size of physical memory (RAM) in bytes.
 * It looks ugly, but this is the cleanest way to achive cross platform results.
 * Cleaned up from:
 *
 * http://nadeausoftware.com/articles/2012/09/c_c_tip_how_get_physical_memory_size_system
 *
 * Note that this function:
 * 1) Was released under the following CC attribution license:
 *    http://creativecommons.org/licenses/by/3.0/deed.en_US.
 * 2) Was originally implemented by David Robert Nadeau.
 * 3) Was modified for Redis by Matt Stancliff.
 * 4) This note exists in order to comply with the original license.
 */
/**
 * 获取物理内存容量
 * @see sysctl 检索系统信息和允许适当的进程设置系统信息
 * @see sysconf 用来获取系统执行的配置信息。例如页大小、最大页数、cpu个数、打开句柄的最大个数等等
 * @return 成功时返回内存容量，否则返回0
 */
size_t zmalloc_get_memory_size(void) {
#if defined(__unix__) || defined(__unix) || defined(unix) || \
    (defined(__APPLE__) && defined(__MACH__))
#if defined(CTL_HW) && (defined(HW_MEMSIZE) || defined(HW_PHYSMEM64))
    int mib[2];
    mib[0] = CTL_HW;
#if defined(HW_MEMSIZE)
    mib[1] = HW_MEMSIZE;            /* OSX. --------------------- */
#elif defined(HW_PHYSMEM64)
    mib[1] = HW_PHYSMEM64;          /* NetBSD, OpenBSD. --------- */
#endif
    int64_t size = 0;               /* 64-bit */
    size_t len = sizeof(size);
    if (sysctl( mib, 2, &size, &len, NULL, 0) == 0)
        return (size_t)size;
    return 0L;          /* Failed? */
    /**
     *  int sysctl(const int *name, u_int namelen, void *oldp, size_t *oldlenp, const void *newp, size_t newlen);
     *  @param name 参数路径， 例如：{CTL_HW, HW_MEMSIZE}
     *  @param namelen 参数路径中元素的个数
     *  @param oldp 指定参数的当前数值
     *  @param oldlenp 指定参数的当前数值的长度
     *  @param newp 指定参数新的数值
     *  @param newlen 指定参数新数值长度
     *  如果不需要的话，可以把相应的值置成NULL
     */
#elif defined(_SC_PHYS_PAGES) && defined(_SC_PAGESIZE)
    /* FreeBSD, Linux, OpenBSD, and Solaris. -------------------- */
    return (size_t)sysconf(_SC_PHYS_PAGES) * (size_t)sysconf(_SC_PAGESIZE);

#elif defined(CTL_HW) && (defined(HW_PHYSMEM) || defined(HW_REALMEM))
    /* DragonFly BSD, FreeBSD, NetBSD, OpenBSD, and OSX. -------- */
    int mib[2];
    mib[0] = CTL_HW;
#if defined(HW_REALMEM)
    mib[1] = HW_REALMEM;        /* FreeBSD. ----------------- */
#elif defined(HW_PYSMEM)
    mib[1] = HW_PHYSMEM;        /* Others. ------------------ */
#endif
    unsigned int size = 0;      /* 32-bit */
    size_t len = sizeof(size);
    if (sysctl(mib, 2, &size, &len, NULL, 0) == 0)
        return (size_t)size;
    return 0L;          /* Failed? */
#else
    return 0L;          /* Unknown method to get the data. */
#endif
#else
    return 0L;          /* Unknown OS. */
#endif
}


