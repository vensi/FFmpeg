/*
 * Copyright (c) 2012 Nicolas George
 *
 * This file is part of FFmpeg.
 *
 * FFmpeg is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * FFmpeg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with FFmpeg; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "bprint.h"
#include "common.h"
#include "error.h"
#include "mem.h"

#if defined(_WIN32)

static int vsnprintf_fixed(char *s, size_t n, const char *format, va_list va)
{
    va_list va2;
    int r;

    va_copy(va2, va);
    r = vsnprintf(s, n, format, va2);
    va_end(va2);
    if (r == -1)
        r = _vscprintf(format, va);
    return r;
}

#define vsnprintf vsnprintf_fixed

#endif

#define av_bprint_room(buf) ((buf)->size - FFMIN((buf)->len, (buf)->size))
#define av_bprint_is_allocated(buf) ((buf)->str != (buf)->reserved_internal_buffer)

static int av_bprint_alloc(AVBPrint *buf, unsigned room)
{
    char *old_str, *new_str;
    unsigned min_size, new_size;

    if (buf->size == buf->size_max)
        return AVERROR(EIO);
    if (!av_bprint_is_complete(buf))
        return AVERROR_INVALIDDATA; /* it is already truncated anyway */
    min_size = buf->len + 1 + FFMIN(UINT_MAX - buf->len - 1, room);
    new_size = buf->size > buf->size_max / 2 ? buf->size_max : buf->size * 2;
    if (new_size < min_size)
        new_size = FFMIN(buf->size_max, min_size);
    old_str = av_bprint_is_allocated(buf) ? buf->str : NULL;
    new_str = av_realloc(old_str, new_size);
    if (!new_str)
        return AVERROR(ENOMEM);
    if (!old_str)
        memcpy(new_str, buf->str, buf->len + 1);
    buf->str  = new_str;
    buf->size = new_size;
    return 0;
}

static void av_bprint_grow(AVBPrint *buf, unsigned extra_len)
{
    /* arbitrary margin to avoid small overflows */
    extra_len = FFMIN(extra_len, UINT_MAX - 5 - buf->len);
    buf->len += extra_len;
    if (buf->size)
        buf->str[FFMIN(buf->len, buf->size - 1)] = 0;
}

void av_bprint_init(AVBPrint *buf, unsigned size_init, unsigned size_max)
{
    unsigned size_auto = (char *)buf + sizeof(*buf) -
                         buf->reserved_internal_buffer;

    if (size_max == 1)
        size_max = size_auto;
    buf->str      = buf->reserved_internal_buffer;
    buf->len      = 0;
    buf->size     = FFMIN(size_auto, size_max);
    buf->size_max = size_max;
    *buf->str = 0;
    if (size_init > buf->size)
        av_bprint_alloc(buf, size_init - 1);
}

void av_bprint_init_for_buffer(AVBPrint *buf, char *buffer, unsigned size)
{
    buf->str      = buffer;
    buf->len      = 0;
    buf->size     = size;
    buf->size_max = size;
    *buf->str = 0;
}

void av_bprintf(AVBPrint *buf, const char *fmt, ...)
{
    unsigned room;
    char *dst;
    va_list vl;
    int extra_len;

    while (1) {
        room = av_bprint_room(buf);
        dst = room ? buf->str + buf->len : NULL;
        va_start(vl, fmt);
        extra_len = vsnprintf(dst, room, fmt, vl);
        va_end(vl);
        if (extra_len <= 0)
            return;
        if (extra_len < room)
            break;
        if (av_bprint_alloc(buf, extra_len))
            break;
    }
    av_bprint_grow(buf, extra_len);
}

void av_bprint_chars(AVBPrint *buf, char c, unsigned n)
{
    unsigned room, real_n;

    while (1) {
        room = av_bprint_room(buf);
        if (n < room)
            break;
        if (av_bprint_alloc(buf, n))
            break;
    }
    if (room) {
        real_n = FFMIN(n, room - 1);
        memset(buf->str + buf->len, c, real_n);
    }
    av_bprint_grow(buf, n);
}

void av_bprint_clear(AVBPrint *buf)
{
    if (buf->len) {
        *buf->str = 0;
        buf->len  = 0;
    }
}

int av_bprint_finalize(AVBPrint *buf, char **ret_str)
{
    unsigned real_size = FFMIN(buf->len + 1, buf->size);
    char *str;
    int ret = 0;

    if (ret_str) {
        if (av_bprint_is_allocated(buf)) {
            str = av_realloc(buf->str, real_size);
            if (!str)
                str = buf->str;
            buf->str = NULL;
        } else {
            str = av_malloc(real_size);
            if (str)
                memcpy(str, buf->str, real_size);
            else
                ret = AVERROR(ENOMEM);
        }
        *ret_str = str;
    } else {
        if (av_bprint_is_allocated(buf))
            av_freep(&buf->str);
    }
    buf->size = real_size;
    return ret;
}

#ifdef TEST

#undef printf

static void bprint_pascal(AVBPrint *b, unsigned size)
{
    unsigned p[size + 1], i, j;

    p[0] = 1;
    av_bprintf(b, "%8d\n", 1);
    for (i = 1; i <= size; i++) {
        p[i] = 1;
        for (j = i - 1; j > 0; j--)
            p[j] = p[j] + p[j - 1];
        for (j = 0; j <= i; j++)
            av_bprintf(b, "%8d", p[j]);
        av_bprintf(b, "\n");
    }
}

int main(void)
{
    AVBPrint b;
    char buf[256];

    av_bprint_init(&b, 0, -1);
    bprint_pascal(&b, 5);
    printf("Short text in unlimited buffer: %u/%u\n", (unsigned)strlen(b.str), b.len);
    printf("%s\n", b.str);
    av_bprint_finalize(&b, NULL);

    av_bprint_init(&b, 0, -1);
    bprint_pascal(&b, 25);
    printf("Long text in unlimited buffer: %u/%u\n", (unsigned)strlen(b.str), b.len);
    av_bprint_finalize(&b, NULL);

    av_bprint_init(&b, 0, 2048);
    bprint_pascal(&b, 25);
    printf("Long text in limited buffer: %u/%u\n", (unsigned)strlen(b.str), b.len);
    av_bprint_finalize(&b, NULL);

    av_bprint_init(&b, 0, 1);
    bprint_pascal(&b, 5);
    printf("Short text in automatic buffer: %u/%u\n", (unsigned)strlen(b.str), b.len);

    av_bprint_init(&b, 0, 1);
    bprint_pascal(&b, 25);
    printf("Long text in automatic buffer: %u/%u\n", (unsigned)strlen(b.str)/8*8, b.len);
    /* Note that the size of the automatic buffer is arch-dependant. */

    av_bprint_init(&b, 0, 0);
    bprint_pascal(&b, 25);
    printf("Long text count only buffer: %u/%u\n", (unsigned)strlen(b.str), b.len);

    av_bprint_init_for_buffer(&b, buf, sizeof(buf));
    bprint_pascal(&b, 25);
    printf("Long text count only buffer: %u/%u\n", (unsigned)strlen(buf), b.len);

    return 0;
}

#endif
