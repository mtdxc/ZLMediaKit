﻿/*
 * Copyright (c) 2016 The ZLMediaKit project authors. All Rights Reserved.
 *
 * This file is part of ZLMediaKit(https://github.com/xia-chu/ZLMediaKit).
 *
 * Use of this source code is governed by MIT license that can be found in the
 * LICENSE file in the root of the source tree. All contributing project authors
 * may be found in the AUTHORS file in the root of the source tree.
 */

#include "utils.h"

/*
 * Used to do unaligned loads on archs that don't support them. GCC can mostly
 * optimize these away.
 */
uint32_t load_be32(const void *p)
{
    const uint8_t *data = (const uint8_t *) p;
    return ((uint32_t)data[0] << 24) | ((uint32_t)data[1] << 16) | ((uint32_t)data[2] << 8) | data[3];
}

uint16_t load_be16(const void *p)
{
    const uint8_t *data = (const uint8_t *) p;
    return ((uint16_t)data[0] << 8) | data[1];
}

uint32_t load_le32(const void *p)
{
    const uint8_t *data = (const uint8_t *) p;
    return data[0] | ((uint32_t) data[1] << 8) |
        ((uint32_t) data[2] << 16) | ((uint32_t) data[3] << 24);
}

uint32_t load_be24(const void *p)
{
    const uint8_t *data = (const uint8_t *) p;
    return data[2] | ((uint32_t) data[1] << 8) | ((uint32_t) data[0] << 16);
}

void set_be16(void *p, uint16_t val){
    uint8_t *data = (uint8_t *) p;
    data[0] = val >> 8;
    data[1] = val;
}

void set_be24(void *p, uint32_t val)
{
    uint8_t *data = (uint8_t *) p;
    data[0] = val >> 16;
    data[1] = val >> 8;
    data[2] = val;
}

void set_le32(void *p, uint32_t val)
{
    uint8_t *data = (uint8_t *) p;
    data[0] = val;
    data[1] = val >> 8;
    data[2] = val >> 16;
    data[3] = val >> 24;
}

void set_be32(void *p, uint32_t val)
{
    uint8_t *data = (uint8_t *) p;
    data[3] = val;
    data[2] = val >> 8;
    data[1] = val >> 16;
    data[0] = val >> 24;
}

