/* endinconv.c -- Endian conversions utilities.
 *
 * This functions are never called directly, but always using the macros
 * defined into endianconv.h, this way we define everything is a non-operation
 * if the arch is already little endian.
 *
 * Redis tries to encode everything as little endian (but a few things that need
 * to be backward compatible are still in big endian) because most of the
 * production environments are little endian, and we have a lot of conversions
 * in a few places because ziplists, intsets, zipmaps, need to be endian-neutral
 * even in memory, since they are serialied on RDB files directly with a single
 * write(2) without other additional steps.
 *
 * ----------------------------------------------------------------------------
 *
 * Copyright (c) 2011-2012, Salvatore Sanfilippo <antirez at gmail dot com>
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


/**
 * 在网络上传输数据时，由于数据传输的两端可能对应不同的硬件平台，采用的存储字节顺序也可能不一致，
 * 因此 TCP/IP 协议规定了在网络上必须采用网络字节顺序(也就是大端模式) 。
 */
#include <stdint.h>

/* Toggle the 16 bit unsigned integer pointed by *p from little endian to
 * big endian */
/**
 * 将指针p所指向的16为无符号整形转换为大端模式。
 * @param P 带转换的数值地址
 */
void memrev16(void *p) {
    unsigned char *x = p, t;

    t = x[0];
    x[0] = x[1];
    x[1] = t;
}

/* Toggle the 32 bit unsigned integer pointed by *p from little endian to
 * big endian */
/**
 * 将指针p所指向的32为无符号整形转换为大端模式。
 * @param P 带转换的数值地址
 */
void memrev32(void *p) {
    unsigned char *x = p, t;

    t = x[0];
    x[0] = x[3];
    x[3] = t;
    t = x[1];
    x[1] = x[2];
    x[2] = t;
}

/* Toggle the 64 bit unsigned integer pointed by *p from little endian to
 * big endian */
/**
 * 将指针p所指向的64为无符号整形转换为大端模式。
 * @param P 带转换的数值地址
 */
void memrev64(void *p) {
    unsigned char *x = p, t;

    t = x[0];
    x[0] = x[7];
    x[7] = t;
    t = x[1];
    x[1] = x[6];
    x[6] = t;
    t = x[2];
    x[2] = x[5];
    x[5] = t;
    t = x[3];
    x[3] = x[4];
    x[4] = t;
}

/**
 * 将16位无符号整形转化为大端模式
 * @param v 待转换的数值
 * @return 转换为大端模式后的数值
 */
uint16_t intrev16(uint16_t v) {
    memrev16(&v);
    return v;
}

/**
 * 将32位无符号整形转化为大端模式
 * @param v 待转换的数值
 * @return 转换为大端模式后的数值
 */
uint32_t intrev32(uint32_t v) {
    memrev32(&v);
    return v;
}

/**
 * 将64位无符号整形转化为大端模式
 * @param v 待转换的数值
 * @return 转换为大端模式后的数值
 */
uint64_t intrev64(uint64_t v) {
    memrev64(&v);
    return v;
}

#ifdef REDIS_TEST
#include <stdio.h>

#define UNUSED(x) (void)(x)
int endianconvTest(int argc, char *argv[]) {
    char buf[32];

    UNUSED(argc);
    UNUSED(argv);

    sprintf(buf,"ciaoroma");
    memrev16(buf);
    printf("%s\n", buf);

    sprintf(buf,"ciaoroma");
    memrev32(buf);
    printf("%s\n", buf);

    sprintf(buf,"ciaoroma");
    memrev64(buf);
    printf("%s\n", buf);

    return 0;
}
#endif
