/*
 * Lagged Fibonacci PRNG
 * Copyright (c) 2008 Michael Niedermayer
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

#ifndef AVUTIL_LFG_H
#define AVUTIL_LFG_H

#include <stdint.h>

/**
 * Context structure for the Lagged Fibonacci PRNG.
 * The exact layout, types and content of this struct may change and should
 * not be accessed directly. Only its `sizeof()` is guaranteed to stay the same
 * to allow easy instanciation.
 */
typedef struct AVLFG
{
    unsigned int state[64];
    int index;
} AVLFG;

void av_lfg_init(AVLFG* c, unsigned int seed);

/**
 * Seed the state of the ALFG using binary data.
 *
 * @return 0 on success, negative value (AVERROR) on failure.
 */
int av_lfg_init_from_data(AVLFG* c, const uint8_t* data, unsigned int length);

/**
 * Get the next random unsigned 32-bit number using an ALFG.
 *
 * Please also consider a simple LCG like state= state*1664525+1013904223,
 * it may be good enough and faster for your specific use case.
 */
static inline unsigned int av_lfg_get(AVLFG* c)
{
    unsigned a = c->state[c->index & 63] =
        c->state[(c->index - 24) & 63] + c->state[(c->index - 55) & 63];
    c->index += 1U;
    return a;
}

/**
 * Get the next random unsigned 32-bit number using a MLFG.
 *
 * Please also consider av_lfg_get() above, it is faster.
 */
static inline unsigned int av_mlfg_get(AVLFG* c)
{
    unsigned int a = c->state[(c->index - 55) & 63];
    unsigned int b = c->state[(c->index - 24) & 63];
    a = c->state[c->index & 63] = 2 * a * b + a + b;
    c->index += 1U;
    return a;
}

/**
 * Get the next two numbers generated by a Box-Muller Gaussian
 * generator using the random numbers issued by lfg.
 *
 * @param lfg pointer to the contex structure
 * @param out array where the two generated numbers are placed
 */
void av_bmg_get(AVLFG* lfg, double out[2]);

#endif /* AVUTIL_LFG_H */
