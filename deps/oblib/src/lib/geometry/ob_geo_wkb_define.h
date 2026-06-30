/*
 * Copyright (c) 2025 OceanBase.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
// WKB(Well-Known Binary)geometry wire-format constants/macros。independent of geometry algorithm logic, kept in lib so geo and protocol serialization can share it,
// after geo moves up to src, rpc/protocol code can still use these fixed format constants without depending on geo。
#ifndef OCEANBASE_LIB_GEOMETRY_OB_GEO_WKB_DEFINE_H_
#define OCEANBASE_LIB_GEOMETRY_OB_GEO_WKB_DEFINE_H_
#include <stdint.h>
namespace oceanbase
{
namespace common
{
static const uint32_t WKB_GEO_SRID_SIZE = sizeof(uint32_t);
static const uint32_t WKB_VERSION_SIZE = sizeof(uint8_t);
static const uint32_t WKB_OFFSET = WKB_GEO_SRID_SIZE + WKB_VERSION_SIZE;
static const uint32_t WKB_GEO_BO_SIZE = sizeof(uint8_t);
static const uint32_t WKB_GEO_TYPE_SIZE = sizeof(uint32_t);
static const uint32_t WKB_GEO_ELEMENT_NUM_SIZE = sizeof(uint32_t);
static const uint32_t WKB_GEO_DOUBLE_STORED_SIZE = sizeof(double);
static const uint32_t WKB_COMMON_WKB_HEADER_LEN = WKB_GEO_BO_SIZE + WKB_GEO_TYPE_SIZE + WKB_GEO_ELEMENT_NUM_SIZE;
static const uint32_t EWKB_COMMON_WKB_HEADER_LEN = WKB_GEO_BO_SIZE + WKB_GEO_TYPE_SIZE;
static const uint32_t EWKB_WITH_SRID_LEN = WKB_COMMON_WKB_HEADER_LEN;
static const uint32_t WKB_POINT_DATA_SIZE = WKB_GEO_DOUBLE_STORED_SIZE + WKB_GEO_DOUBLE_STORED_SIZE;
static const uint32_t WKB_DATA_OFFSET = WKB_OFFSET + WKB_GEO_BO_SIZE;
static const uint32_t WKB_INNER_POINT = WKB_DATA_OFFSET + WKB_GEO_TYPE_SIZE;
static const uint8_t GEO_VER_MASK = 0x40;
#define IS_GEO_VERSION(ver) (((ver) & GEO_VER_MASK) != 0)
#define ENCODE_GEO_VERSION(ver) ((ver) | GEO_VER_MASK)
} // namespace common
} // namespace oceanbase
#endif
