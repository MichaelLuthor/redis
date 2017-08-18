/* String -> String Map data structure optimized for size.
 * This file implements a data structure mapping strings to other strings
 * implementing an O(n) lookup data structure designed to be very memory
 * efficient.
 *
 * The Redis Hash type uses this data structure for hashes composed of a small
 * number of elements, to switch to a hash table once a given number of
 * elements is reached.
 *
 * Given that many times Redis Hashes are used to represent objects composed
 * of few fields, this is a very big win in terms of used memory.
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

/* Memory layout of a zipmap, for the map "foo" => "bar", "hello" => "world":
 *
 * <zmlen><len>"foo"<len><free>"bar"<len>"hello"<len><free>"world"
 *
 * <zmlen> is 1 byte length that holds the current size of the zipmap.
 * When the zipmap length is greater than or equal to 254, this value
 * is not used and the zipmap needs to be traversed to find out the length.
 *
 * <len> is the length of the following string (key or value).
 * <len> lengths are encoded in a single value or in a 5 bytes value.
 * If the first byte value (as an unsigned 8 bit value) is between 0 and
 * 253, it's a single-byte length. If it is 254 then a four bytes unsigned
 * integer follows (in the host byte ordering). A value of 255 is used to
 * signal the end of the hash.
 *
 * <free> is the number of free unused bytes after the string, resulting
 * from modification of values associated to a key. For instance if "foo"
 * is set to "bar", and later "foo" will be set to "hi", it will have a
 * free byte to use if the value will enlarge again later, or even in
 * order to add a key/value pair if it fits.
 *
 * <free> is always an unsigned 8 bit number, because if after an
 * update operation there are more than a few free bytes, the zipmap will be
 * reallocated to make sure it is as small as possible.
 *
 * The most compact representation of the above two elements hash is actually:
 *
 * "\x02\x03foo\x03\x00bar\x05hello\x05\x00world\xff"
 *
 * Note that because keys and values are prefixed length "objects",
 * the lookup will take O(N) where N is the number of elements
 * in the zipmap and *not* the number of bytes needed to represent the zipmap.
 * This lowers the constant times considerably.
 */

/**
 * map "foo" => "bar", "hello" => "world" 的内存布局：
 * <zmlen><len>"foo"<len><free>"bar"<len>"hello"<len><free>"world"
 *
 * <zmlen> 大小为一个字节， 保存当前map的大小。当map的长度大于等于254个元素的时候，该值不再使用，
 * 而是通过遍历该map来获取map长度。
 *
 * <len> len是其所跟随的键名或值得长度，长度可能是一个字节或者5个字节的子都， 当第一个字节的值在
 * 0~253之间的时候， len仅仅使用一个字节，如果第一个字节是254，那么剩余的4个字节将用户来存储长度
 *（大端模式）， 255用来作为map的结束标记。
 *
 * <free> 未使用字节数， 当通过一个键名需改其对应的键值的时候会出现空余，举个例子来说，比如现在foo的值
 * 为bar，那么，当我们将foo的值改为hi的时候，就会出现一个字节的空余， 这样当我们以后要再次增长到个字节的
 * 时候，不必申请内存。
 *
 * <free>始终是一个1个字节的无符号数值，因为如果更新的时候，如果空余空间太多，zipmap会重新申请空间来
 * 保持整个map的紧凑性。
 *
 * 上面举例的键值在内存中的安排：
 *
 * "\x02\x03foo\x03\x00bar\x05hello\x05\x00world\xff"
 *
 * 注意， 由于键值对都有一个长度的前缀， 所以在查找元素时的复杂度O(n)中的n表示的是元素数量，而不是字节数量。
 * 这样就节省了相当一部分时间。
 */

#include <stdio.h>
#include <string.h>
#include "zmalloc.h"
#include "endianconv.h"

/* 采用4个字节表示长度的标记 */
#define ZIPMAP_BIGLEN 254
/* map 结束标记 */
#define ZIPMAP_END 255

/* The following defines the max value for the <free> field described in the
 * comments above, that is, the max number of trailing bytes in a value. */
#define ZIPMAP_VALUE_MAX_FREE 4

/* The following macro returns the number of bytes needed to encode the length
 * for the integer value _l, that is, 1 byte for lengths < ZIPMAP_BIGLEN and
 * 5 bytes for all the other lengths. */
// 计算指定长度所占用的字节数， 如果长度在0~253之间则使用1个字节， 否则采用4个字节
#define ZIPMAP_LEN_BYTES(_l) (((_l) < ZIPMAP_BIGLEN) ? 1 : sizeof(unsigned int)+1)

/* Create a new empty zipmap. */
/**
 * 创建一个新的zip map
 * @return 返回map首地址
 */
unsigned char *zipmapNew(void) {
    unsigned char *zm = zmalloc(2);

    // 一个空的map， 需要在头部设置长度为0，在尾部增加结束标记.
    zm[0] = 0; /* Length */
    zm[1] = ZIPMAP_END;
    return zm;
}

/* Decode the encoded length pointed by 'p' */
/**
 * 通过给定的指针计算该项目的长度
 * @param p 长度标记的起始地址
 * @return 该项目占用的字节数
 */
static unsigned int zipmapDecodeLength(unsigned char *p) {
    unsigned int len = *p;

    // 如果长度在0~253之间， 则直接返回长度
    if (len < ZIPMAP_BIGLEN) return len;

    // 长度超过253个字节， 随后的四个字节表示长度。
    memcpy(&len,p+1,sizeof(unsigned int));
    // 将该数值转换为大端存储获取真是大小。
    memrev32ifbe(&len);
    return len;
}

/* Encode the length 'l' writing it in 'p'. If p is NULL it just returns
 * the amount of bytes required to encode such a length. */
/**
 * 将给定的长度编码到p指向的内存中
 * @param p 长度保存位置
 * @param len 待编码的长度
 * @param 返回长度所占的字节数
 */
static unsigned int zipmapEncodeLength(unsigned char *p, unsigned int len) {
    if (p == NULL) {
        // 如果p为空，则不用进行赋值， 直接返回长度的字节数
        return ZIPMAP_LEN_BYTES(len);
    } else {
        if (len < ZIPMAP_BIGLEN) {
            // 如果小于大空间标记， 则仅使用一个字节
            p[0] = len;
            return 1;
        } else {
            // 第一个字节打上大空间标记
            p[0] = ZIPMAP_BIGLEN;
            // 将长度值写入4个字节的空间中
            memcpy(p+1,&len,sizeof(len));
            // 将内存中数据转换为大端模式
            memrev32ifbe(p+1);
            // 返回len所占空间的字节数
            return 1+sizeof(len);
        }
    }
}

/* Search for a matching key, returning a pointer to the entry inside the
 * zipmap. Returns NULL if the key is not found.
 *
 * If NULL is returned, and totlen is not NULL, it is set to the entire
 * size of the zimap, so that the calling function will be able to
 * reallocate the original zipmap to make room for more entries. */
/**
 * 在给定的map中查询匹配的键名，并返回键名的指针， 如果没有找到对应的键名，则返回NULL。
 * @param zm map地址
 * @param key 键名
 * @param klen 键名长度
 * @param totlen 用于存储map的字节长度
 * @return 键名地址
 */
static unsigned char *zipmapLookupRaw(unsigned char *zm, unsigned char *key, unsigned int klen, unsigned int *totlen) {
    unsigned char *p = zm+1, *k = NULL;
    unsigned int l,llen;

    while(*p != ZIPMAP_END) {
        unsigned char free;

        /* Match or skip the key */
        l = zipmapDecodeLength(p); // 获取键名长度
        llen = zipmapEncodeLength(NULL,l); // 获取键名len长度用于计算去内容时的计算
        // k 用来临时存储找到的键名地址， 初始为NULL， 找到后不为NULL
        if (key != NULL && k == NULL && l == klen && !memcmp(p+llen,key,l)) {
            /* Only return when the user doesn't care
             * for the total length of the zipmap. */
            /** 如果toolen不为null， 那么表示用户不关心map的字节数， 那么直接返回键名的地址 */
            if (totlen != NULL) {
                // 将找到的键名放入到临时的k中。
                k = p;
            } else {
                return p;
            }
        }

        // 将p推到该元素的键值地址。
        p += llen+l;
        /* Skip the value as well */
        l = zipmapDecodeLength(p); // 获取键值的长度
        p += zipmapEncodeLength(NULL,l); // 获取键值的长度字节数, 然后将p推过该字节数量，指向<free>标记
        free = p[0];
        // 将p 推到下一个键名的位置
        p += l+1+free; /* +1 to skip the free byte */
    }
    // 如果totlen不为空， 则设置totlen为当前map的总字节数。
    if (totlen != NULL) *totlen = (unsigned int)(p-zm)+1;
    // 返回找到的键名的地址
    return k;
}

/**
 * 计算键值对存储的空间大小
 * @param klen 键名长度
 * @param vlen 键值长度
 * @return 实际空间大小
 */
static unsigned long zipmapRequiredLength(unsigned int klen, unsigned int vlen) {
    unsigned int l;

    // 长度 = 键名长度 + 键值长度 + free标记 + 键长标记 + 值长标记
    l = klen+vlen+3;
    // 如果长度超过254，则需要使用4字节来存储长度
    if (klen >= ZIPMAP_BIGLEN) l += 4;
    if (vlen >= ZIPMAP_BIGLEN) l += 4;
    return l;
}

/* Return the total amount used by a key (encoded length + payload) */
/**
 * 获取一个键值包含标记的长度
 * @param p 键名地址
 * @return 获取map中键名部分的长度
 */
static unsigned int zipmapRawKeyLength(unsigned char *p) {
    unsigned int l = zipmapDecodeLength(p);
    return zipmapEncodeLength(NULL,l) + l;
}

/* Return the total amount used by a value
 * (encoded length + single byte free count + payload) */
/**
 * 通过指针p获取当前指向的值的长度，包含标记，空闲，数据区域。
 * @param p 指向值得指针
 * @return 返回值完整长度
 */
static unsigned int zipmapRawValueLength(unsigned char *p) {
    // 解析出值有效值的长度
    unsigned int l = zipmapDecodeLength(p);
    unsigned int used;

    // 计算出值长度所占的空间
    used = zipmapEncodeLength(NULL,l);
    // 使用量增加 <= free字节数 + free标记 + 值长度
    used += p[used] + 1 + l;
    return used;
}

/* If 'p' points to a key, this function returns the total amount of
 * bytes used to store this entry (entry = key + associated value + trailing
 * free space if any). */
/**
 * 获取一个键值对所占用的空间大小
 * @param p 键值对的起始地址，也就是键名的起始地址
 * @return 占用空间大小
 */
static unsigned int zipmapRawEntryLength(unsigned char *p) {
    // 计算出键名的原始长度
    unsigned int l = zipmapRawKeyLength(p);
    // 加上键值的原始长度
    return l + zipmapRawValueLength(p+l);
}

/**
 * 重新规划zipmap的大小
 * @param zm zipmap地址
 * @param len 新长度
 * @param resize之后的zipmap地址
 */
static inline unsigned char *zipmapResize(unsigned char *zm, unsigned int len) {
    // 重新申请内存
    zm = zrealloc(zm, len);
    // 将新的空间最后一个字节标记为map的结束位置。
    zm[len-1] = ZIPMAP_END;
    return zm;
}

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
unsigned char *zipmapSet(unsigned char *zm, unsigned char *key, unsigned int klen, unsigned char *val, unsigned int vlen, int *update) {
    unsigned int zmlen, offset;
    unsigned int freelen, reqlen = zipmapRequiredLength(klen,vlen);
    unsigned int empty, vempty;
    unsigned char *p;

    // reqlen = 键值对存储所需要的空间大小
    freelen = reqlen;
    if (update) *update = 0; // 默认先将是否更新标记置0

    // 查找键名是否存在并计算zm的总长度
    p = zipmapLookupRaw(zm,key,klen,&zmlen);
    if (p == NULL) {
        /* Key not found: enlarge */
        // 键名不存在， 扩大zm空间。
        zm = zipmapResize(zm, zmlen+reqlen);
        // p 将指向空余的空间
        p = zm+zmlen-1;
        // zm长度加长新空间的长度
        zmlen = zmlen+reqlen;

        /* Increase zipmap length (this is an insert) */
        // 如果zm元素个数小于254， 则增加一个元素技术， 如果超过， 则不用设置， 需要手工遍历来获取长度
        if (zm[0] < ZIPMAP_BIGLEN) zm[0]++;
    } else {
        /* Key found. Is there enough space for the new value? */
        /* Compute the total length: */
        if (update) *update = 1; // 将是否更新标记置1
        // 计算出当前键值对所占用的空间大小
        freelen = zipmapRawEntryLength(p);
        if (freelen < reqlen) {
            // 如果已经存在的键值对的空间不足以存储新值，则需要重新分配空间。
            /* Store the offset of this key within the current zipmap, so
             * it can be resized. Then, move the tail backwards so this
             * pair fits at the current position. */
            // 记下偏移地址，因为重新申请大小后，地址会改变， 需要重新计算当前p的地址
            offset = p-zm;
            // 重新申请zm的大小， 仅仅申请缺少的空间大小
            zm = zipmapResize(zm, zmlen-freelen+reqlen);
            // 按照之前计算的偏移， 计算出新的键值对地址
            p = zm+offset;

            /* The +1 in the number of bytes to be moved is caused by the
             * end-of-zipmap byte. Note: the *original* zmlen is used. */
            // 将当前键值对后面的数据往后挪出不足的空间。
            // p+reqlen = 新的键值对末尾位置
            // p+freelen = 老的键值对的末尾位置
            // zmlen-(offset+freelen+1) = 当前键值对后面的元素总大小
            memmove(p+reqlen, p+freelen, zmlen-(offset+freelen+1));
            // 重新计算zm的长度
            zmlen = zmlen-freelen+reqlen;
            // 这里将保证不会有free空间
            freelen = reqlen;
        }
    }

    /* We now have a suitable block where the key/value entry can
     * be written. If there is too much free space, move the tail
     * of the zipmap a few bytes to the front and shrink the zipmap,
     * as we want zipmaps to be very space efficient. */
    // 到目前为止， 我们已经有足够的空间开存储键值对。 但是有可能存在free的空间太大的问题， 此时我们
    // 需要将后面的数据往前面挪动，来保证zm的空间紧凑。
    empty = freelen-reqlen;
    if (empty >= ZIPMAP_VALUE_MAX_FREE) {
        /* First, move the tail <empty> bytes to the front, then resize
         * the zipmap to be <empty> bytes smaller. */
        offset = p-zm;
        // 这里将填充所有的多余空间，使free的空间为0
        // p+reqlen  将要写键值对的末尾
        // p+freelen 原始键值对的末尾
        // zmlen-(offset+freelen+1) = 原始键值对后面的数据大小
        memmove(p+reqlen, p+freelen, zmlen-(offset+freelen+1));
        // 重新计算长度
        zmlen -= empty;
        // 重新申请空间
        zm = zipmapResize(zm, zmlen);
        // 重新计算写键值对
        p = zm+offset;
        // free 改为 0
        vempty = 0;
    } else {
        // 如果free的空间不是很大，则将该值写在free标记中，由下次分配使用
        vempty = empty;
    }

    /* Just write the key + value and we are done. */
    // 现在只需要将键名和值写到内存就可以了
    /* Key: */
    // 编码键名长度，并且将p推到键名数据区
    p += zipmapEncodeLength(p,klen);
    memcpy(p,key,klen); // 将键名复制到键名的数据区域
    p += klen; // 将p推到键值位置
    /* Value: */
    p += zipmapEncodeLength(p,vlen); // 编码键值长度，并将p推到free标记
    *p++ = vempty; // 写入free标记
    memcpy(p,val,vlen); // 赋值键值到内存
    return zm;
}

/* Remove the specified key. If 'deleted' is not NULL the pointed integer is
 * set to 0 if the key was not found, to 1 if it was found and deleted. */
/**
 * 从zipmap中删除元素。
 * @param zm zipmap地址
 * @param key 待删除的键名
 * @param klen 待删除键名长度
 * @param deleted 如果不为NULL，则如果指定元素被删除则设置为1，否则设置为0.
 * @param 删除键值后的zipmap
 */
unsigned char *zipmapDel(unsigned char *zm, unsigned char *key, unsigned int klen, int *deleted) {
    unsigned int zmlen, freelen;

    // 查找键名锁在位置，并且计算map的总长度
    unsigned char *p = zipmapLookupRaw(zm,key,klen,&zmlen);
    if (p) {
        // 找到后计算出该键值对所占用的空间大小
        freelen = zipmapRawEntryLength(p);

        // 将待删除键值对后面的数据向前移动，覆盖掉删除的键值对
        // 最后一个size ： 总长度 - ( 删除键值对之前的长度 + 键值对的长度 + 1 )
        memmove(p, p+freelen, zmlen-((p-zm)+freelen+1));
        // 重新申请地址
        zm = zipmapResize(zm, zmlen-freelen);

        /* Decrease zipmap length */
        // 重新计算zipmap的元素个数
        // 这里只在当元素总数少于254个的情况， 应为超过254个之后， 需要遍历一遍才能计算出总数
        if (zm[0] < ZIPMAP_BIGLEN) zm[0]--;

        // 为删除标记赋值 已删除
        if (deleted) *deleted = 1;
    } else {
        // 为删除标记赋值 未找到
        if (deleted) *deleted = 0;
    }
    return zm;
}

/* Call before iterating through elements via zipmapNext() */
/**
 * 将map游标地址推送到第一个元素的地址，在调用next的时候需要执行该操作
 * @return 第一个元素的地址
 */
unsigned char *zipmapRewind(unsigned char *zm) {
    return zm+1;
}

/* This function is used to iterate through all the zipmap elements.
 * In the first call the first argument is the pointer to the zipmap + 1.
 * In the next calls what zipmapNext returns is used as first argument.
 * Example:
 *
 * unsigned char *i = zipmapRewind(my_zipmap);
 * while((i = zipmapNext(i,&key,&klen,&value,&vlen)) != NULL) {
 *     printf("%d bytes key at $p\n", klen, key);
 *     printf("%d bytes value at $p\n", vlen, value);
 * }
 */
/**
 * 遍历zipmap，并将当前键值保存在对应的变量
 * @param zm zipmap地址，不是首地址，第一次是rewind之后的地址， 之后是每次next返回的地址
 * @param key 当前键名
 * @param klen 当前键名长度
 * @param value 当前值
 * @param vlen 当前value的长度
 * @return 下一次调用next时应该传的map的地址
 */
unsigned char *zipmapNext(unsigned char *zm, unsigned char **key, unsigned int *klen, unsigned char **value, unsigned int *vlen) {
    // 如果遍历结束则返回NULL
    if (zm[0] == ZIPMAP_END) return NULL;

    if (key) {
        *key = zm; // 赋值当前键名指向键名开始处
        *klen = zipmapDecodeLength(zm); // 赋值当前的长度
        *key += ZIPMAP_LEN_BYTES(*klen); // 将键名的指针推到指向数据的地址
    }
    zm += zipmapRawKeyLength(zm); // 将zm指针推过键名部分，指向value开始处
    if (value) {
        *value = zm+1; // 跳过free字节
        *vlen = zipmapDecodeLength(zm); //获取变量长度
        *value += ZIPMAP_LEN_BYTES(*vlen); // 将value数据指向数据区区域
    }
    zm += zipmapRawValueLength(zm); // 将指针推过value部分， 将指针指向下一个键名
    return zm;
}

/* Search a key and retrieve the pointer and len of the associated value.
 * If the key is found the function returns 1, otherwise 0. */
/**
 * 通过键名获取map中对应的值
 * @param zm map地址
 * @param key 键名
 * @param klen 键名长度
 * @param value 用于保存键值
 * @param vlen 用于保存键值长度
 * @return 如果找到键名则返回1， 否则返回0
 */
int zipmapGet(unsigned char *zm, unsigned char *key, unsigned int klen, unsigned char **value, unsigned int *vlen) {
    unsigned char *p;

    // 如果键名没有找到则直接返回0.
    if ((p = zipmapLookupRaw(zm,key,klen,NULL)) == NULL) return 0;

    // 将p推到键值元素地址
    p += zipmapRawKeyLength(p);
    // 设置键值长度
    *vlen = zipmapDecodeLength(p);
    // 设置键值指针
    *value = p + ZIPMAP_LEN_BYTES(*vlen) + 1;
    // 返回查找成功
    return 1;
}

/* Return 1 if the key exists, otherwise 0 is returned. */
/**
 * 检查键名是否存在
 * @param zm map地址
 * @param key 键名
 * @param klen 键名长度
 * @return 1：存在 0：不存在
 */
int zipmapExists(unsigned char *zm, unsigned char *key, unsigned int klen) {
    return zipmapLookupRaw(zm,key,klen,NULL) != NULL;
}

/* Return the number of entries inside a zipmap */
/**
 * 获取map元素个数
 * @param zm map地址
 * @return 元素个数
 */
unsigned int zipmapLen(unsigned char *zm) {
    unsigned int len = 0;
    if (zm[0] < ZIPMAP_BIGLEN) {
        // 如果map中元素数量少于254个， 则直接取map的第一个字节返回
        len = zm[0];
    } else {
        // 否则遍历该map进行元素数量统计
        unsigned char *p = zipmapRewind(zm);
        while((p = zipmapNext(p,NULL,NULL,NULL,NULL)) != NULL) len++;

        /* Re-store length if small enough */
        if (len < ZIPMAP_BIGLEN) zm[0] = len;
    }
    return len;
}

/* Return the raw size in bytes of a zipmap, so that we can serialize
 * the zipmap on disk (or everywhere is needed) just writing the returned
 * amount of bytes of the C array starting at the zipmap pointer. */
/**
 * 获取该map的字节长度，用于序列化到磁盘或者其他用户。
 * @param zm map首地址
 * @return map的字节数
 */
size_t zipmapBlobLen(unsigned char *zm) {
    unsigned int totlen;
    zipmapLookupRaw(zm,NULL,0,&totlen);
    return totlen;
}

#ifdef REDIS_TEST
/**
 * 循环打印出map的结构
 * 类似于：{status 2}{key 4}name{value 10}micl[......]{key 3}age{value 2}25{end}
 * @param p zipmap收地址
 */
static void zipmapRepr(unsigned char *p) {
    unsigned int l;

    printf("{status %u}",*p++);
    while(1) {
        if (p[0] == ZIPMAP_END) {
            printf("{end}");
            break;
        } else {
            unsigned char e;

            l = zipmapDecodeLength(p);
            printf("{key %u}",l);
            p += zipmapEncodeLength(NULL,l);
            if (l != 0 && fwrite(p,l,1,stdout) == 0) perror("fwrite");
            p += l;

            l = zipmapDecodeLength(p);
            printf("{value %u}",l);
            p += zipmapEncodeLength(NULL,l);
            e = *p++;
            if (l != 0 && fwrite(p,l,1,stdout) == 0) perror("fwrite");
            p += l+e;
            if (e) {
                printf("[");
                while(e--) printf(".");
                printf("]");
            }
        }
    }
    printf("\n");
}

#define UNUSED(x) (void)(x)
int zipmapTest(int argc, char *argv[]) {
    unsigned char *zm;

    UNUSED(argc);
    UNUSED(argv);

    zm = zipmapNew();

    zm = zipmapSet(zm,(unsigned char*) "name",4, (unsigned char*) "foo",3,NULL);
    zm = zipmapSet(zm,(unsigned char*) "surname",7, (unsigned char*) "foo",3,NULL);
    zm = zipmapSet(zm,(unsigned char*) "age",3, (unsigned char*) "foo",3,NULL);
    zipmapRepr(zm);

    zm = zipmapSet(zm,(unsigned char*) "hello",5, (unsigned char*) "world!",6,NULL);
    zm = zipmapSet(zm,(unsigned char*) "foo",3, (unsigned char*) "bar",3,NULL);
    zm = zipmapSet(zm,(unsigned char*) "foo",3, (unsigned char*) "!",1,NULL);
    zipmapRepr(zm);
    zm = zipmapSet(zm,(unsigned char*) "foo",3, (unsigned char*) "12345",5,NULL);
    zipmapRepr(zm);
    zm = zipmapSet(zm,(unsigned char*) "new",3, (unsigned char*) "xx",2,NULL);
    zm = zipmapSet(zm,(unsigned char*) "noval",5, (unsigned char*) "",0,NULL);
    zipmapRepr(zm);
    zm = zipmapDel(zm,(unsigned char*) "new",3,NULL);
    zipmapRepr(zm);

    printf("\nLook up large key:\n");
    {
        unsigned char buf[512];
        unsigned char *value;
        unsigned int vlen, i;
        for (i = 0; i < 512; i++) buf[i] = 'a';

        zm = zipmapSet(zm,buf,512,(unsigned char*) "long",4,NULL);
        if (zipmapGet(zm,buf,512,&value,&vlen)) {
            printf("  <long key> is associated to the %d bytes value: %.*s\n",
                vlen, vlen, value);
        }
    }

    printf("\nPerform a direct lookup:\n");
    {
        unsigned char *value;
        unsigned int vlen;

        if (zipmapGet(zm,(unsigned char*) "foo",3,&value,&vlen)) {
            printf("  foo is associated to the %d bytes value: %.*s\n",
                vlen, vlen, value);
        }
    }
    printf("\nIterate through elements:\n");
    {
        unsigned char *i = zipmapRewind(zm);
        unsigned char *key, *value;
        unsigned int klen, vlen;

        while((i = zipmapNext(i,&key,&klen,&value,&vlen)) != NULL) {
            printf("  %d:%.*s => %d:%.*s\n", klen, klen, key, vlen, vlen, value);
        }
    }
    return 0;
}
#endif
