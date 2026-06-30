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
#include "ob_geo_wkb_visitor.h"


namespace oceanbase {
namespace common {

template<typename T>
int ObGeoWkbVisitor::write_head_info(T *geo)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(write_to_buffer( ObGeoWkbByteOrder::LittleEndian, 1))) {
  } else if (OB_FAIL(write_to_buffer(geo->type(), sizeof(uint32_t)))) {
  } else if (OB_FAIL(write_to_buffer(geo->size(), sizeof(uint32_t)))) {
  }
  return ret;
}

int ObGeoWkbVisitor::write_cartesian_point(double x, double y)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(write_to_buffer(x, sizeof(double)))) {
  } else if (OB_FAIL(write_to_buffer(y, sizeof(double)))) {
  }
  return ret;  
}

int ObGeoWkbVisitor::write_geograph_point(double x, double y)
{
  int ret = OB_SUCCESS;
  double val_x = x;
  double val_y = y;
  if (OB_FAIL(need_convert_ && srs_->longtitude_convert_from_radians(x, val_x))) {
  } else if (OB_FAIL(write_to_buffer(val_x, sizeof(double)))) {
  } else if (need_convert_ && OB_FAIL(srs_->latitude_convert_from_radians(y, val_y))) {
    LOG_WARN("failed to convert from latitude value", K(ret), K(y));
  } else if (OB_FAIL(write_to_buffer(val_y, sizeof(double)))) {
  }
  return ret;
}

int ObGeoWkbVisitor::visit(ObPoint *geo)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(write_to_buffer(ObGeoWkbByteOrder::LittleEndian, 1))) {
  } else if (OB_FAIL(write_to_buffer(geo->type(), sizeof(uint32_t)))) {
  } else if (srs_ == NULL || srs_->srs_type() == ObSrsType::PROJECTED_SRS) {
    if (OB_FAIL(write_cartesian_point(geo->x(), geo->y()))) {
    }
  } else if (OB_FAIL(write_geograph_point(geo->x(), geo->y()))) {
  }
  return ret;
}

int ObGeoWkbVisitor::visit(ObCartesianLineString *geo)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(write_head_info(geo))) {
  } else {
    for (uint32_t i = 0; i < geo->size() && OB_SUCC(ret); i++) {
      if (OB_FAIL(write_cartesian_point((*geo)[i].get<0>(), (*geo)[i].get<1>()))) {
      }
    }
  }
  return ret;
}

int ObGeoWkbVisitor::visit(ObGeographLineString *geo)
{
  int ret = OB_SUCCESS;
  if (srs_ == NULL || srs_->srs_type() == ObSrsType::PROJECTED_SRS) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid srs info", K(ret), K(srs_));
  } else if (OB_FAIL(write_head_info(geo))) {
  } else {
    for (uint32_t i = 0; i < geo->size() && OB_SUCC(ret); i++) {
      if (OB_FAIL(write_geograph_point((*geo)[i].get<0>(), (*geo)[i].get<1>()))) {
      }
    }
  }
  return ret;
}

int ObGeoWkbVisitor::visit(ObGeographPolygon *geo)
{
  int ret = OB_SUCCESS;
  if (srs_ == NULL || srs_->srs_type() == ObSrsType::PROJECTED_SRS) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid srs info", K(ret), K(srs_));
  } else if (OB_FAIL(write_head_info(geo))) {
  } else {
    const ObGeographLinearring& ring = geo->exterior_ring();
    if (OB_FAIL(write_to_buffer(ring.size(), sizeof(uint32_t)))) {
    }    
    for (uint32_t i = 0; i < ring.size() && OB_SUCC(ret); i++) {
      if (OB_FAIL(write_geograph_point(ring[i].get<0>(), ring[i].get<1>()))) {
      }
    }
    for (uint32_t i = 0; i < geo->inner_ring_size() && OB_SUCC(ret); i++) {
      const ObGeographLinearring& inner_ring = geo->inner_ring(i);
      if (OB_FAIL(write_to_buffer(inner_ring.size(), sizeof(uint32_t)))) {
      }
      for (uint32_t j = 0; j < inner_ring.size() && OB_SUCC(ret); j++) {
        if (OB_FAIL(write_geograph_point(inner_ring[j].get<0>(), inner_ring[j].get<1>()))) {
        }
      }
    }
  }
  return ret;
}

int ObGeoWkbVisitor::visit(ObCartesianPolygon *geo)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(write_head_info(geo))) {
  } else {
    const ObCartesianLinearring& ring = geo->exterior_ring();
    if (OB_FAIL(write_to_buffer(ring.size(), sizeof(uint32_t)))) {
    }
    for (uint32_t i = 0; i < ring.size() && OB_SUCC(ret); i++) {
      if (OB_FAIL(write_cartesian_point(ring[i].get<0>(), ring[i].get<1>()))) {
      }
    }
    for (uint32_t i = 0; i < geo->inner_ring_size() && OB_SUCC(ret); i++) {
      const ObCartesianLinearring& inner_ring = geo->inner_ring(i);
      if (OB_FAIL(write_to_buffer(inner_ring.size(), sizeof(uint32_t)))) {
      }
      for (uint32_t j = 0; j < inner_ring.size() && OB_SUCC(ret); j++) {
        if (OB_FAIL(write_cartesian_point(inner_ring[j].get<0>(), inner_ring[j].get<1>()))) {
        } 
      }
    }
  }
  return ret;
}

int ObGeoWkbVisitor::visit(ObGeometrycollection *geo)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(write_head_info(geo))) {
  }
  return ret;
}

int ObGeoWkbVisitor::visit(ObIWkbGeometry *geo)
{
  int ret = OB_SUCCESS;
  const char *wkb_no_srid = geo->val();
  uint32_t wkb_no_srid_len = geo->length();
  if (buffer_->write(wkb_no_srid, wkb_no_srid_len) != wkb_no_srid_len) {
    ret = OB_BUF_NOT_ENOUGH;
    LOG_WARN("failed to write buffer", K(ret));
  }
  return ret;

}

} // namespace common
} // namespace oceanbase
