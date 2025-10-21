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

#define USING_LOG_PREFIX LIB
#include "ob_geo_wkb_size_visitor.h"


namespace oceanbase {
namespace common {

bool ObGeoWkbSizeVisitor::prepare(ObIWkbGeometry *geo)
{
  geo_size_ += geo->length();
  return false;
}

int ObGeoWkbSizeVisitor::visit(ObIWkbGeometry *geo)
{
  geo_size_ += geo->length();
  return OB_SUCCESS;
}

int ObGeoWkbSizeVisitor::visit(ObPoint *geo)
{
  // [bo][type][X][Y]
  UNUSED(geo);
  geo_size_ += sizeof(uint8_t) + sizeof(uint32_t)
               + 2 * sizeof(double);
  return OB_SUCCESS;
}

int ObGeoWkbSizeVisitor::visit(ObLineString *geo)
{
  // [bo][type][num][X][Y][...]
  geo_size_ += WKB_COMMON_WKB_HEADER_LEN + geo->size() * 2 * sizeof(double);
  return OB_SUCCESS;
}

int ObGeoWkbSizeVisitor::visit(ObLinearring *geo)
{
  // [num][X][Y][...]
  geo_size_ += sizeof(uint32_t) + geo->size() * 2 * sizeof(double);
  return OB_SUCCESS;
}

int ObGeoWkbSizeVisitor::visit(ObPolygon *geo)
{
  // [bo][type][num][ex][inner_rings]
  UNUSED(geo);
  geo_size_ += WKB_COMMON_WKB_HEADER_LEN;
  return OB_SUCCESS;
}

int ObGeoWkbSizeVisitor::visit(ObGeometrycollection *geo)
{
  // [bo][type][num][ex][inner_rings]
  UNUSED(geo);
  geo_size_ += WKB_COMMON_WKB_HEADER_LEN;
  return OB_SUCCESS;
}

} // namespace common
} // namespace oceanbase
