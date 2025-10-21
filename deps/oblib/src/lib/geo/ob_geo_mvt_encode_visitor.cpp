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
#include "ob_geo_mvt_encode_visitor.h"

namespace oceanbase {
namespace common {

int ObGeoMvtEncodeVisitor::visit(ObIWkbGeomPoint *geo)
{
  int ret = OB_SUCCESS;
  int32_t x = static_cast<int32_t>(geo->x()) - curr_x_;
  int32_t y = static_cast<int32_t>(geo->y()) - curr_y_;
  if (type_ == ObMVTType::MVT_POINT) {
    if (point_idx_ == 0 && OB_FAIL(encode_buffer_.push_back(encode_command(ObMVTCommand::CMD_MOVE_TO, point_num_ == 0 ? 1 : point_num_)))) {
      LOG_WARN("failed to push back move to cmd", K(ret));
    } else if (OB_FAIL(encode_buffer_.push_back(encode_param(x)))) {
      LOG_WARN("failed to push back x value", K(ret));
    } else if (OB_FAIL(encode_buffer_.push_back(encode_param(y)))) {
      LOG_WARN("failed to push back y value", K(ret));
    } else {
      curr_x_ = static_cast<int32_t>(geo->x());
      curr_y_ = static_cast<int32_t>(geo->y());
    }
  } else if (type_ == ObMVTType::MVT_LINE || type_ == ObMVTType::MVT_RING) {
    if (type_ == ObMVTType::MVT_RING && point_idx_ + 1 == point_num_) {
      if (OB_FAIL(encode_buffer_.push_back(encode_command(ObMVTCommand::CMD_CLOSE_PATH, 1)))) {
        LOG_WARN("failed to push back move to cmd", K(ret));
      }
    } else {
      if (value_offset_ < line_to_offset_) {
        encode_buffer_[value_offset_++] = encode_param(x);
        encode_buffer_[value_offset_++] = encode_param(y);
      } else if (OB_FAIL(encode_buffer_.push_back(encode_param(x)))) {
        LOG_WARN("failed to push back x value", K(ret));
      } else if (OB_FAIL(encode_buffer_.push_back(encode_param(y)))) {
        LOG_WARN("failed to push back y value", K(ret));
      }
      curr_x_ = static_cast<int32_t>(geo->x());
      curr_y_ = static_cast<int32_t>(geo->y());
    }
  }
  if (OB_SUCC(ret)) {
    point_idx_++;
  }
  return ret;
}
int ObGeoMvtEncodeVisitor::visit(ObIWkbGeomMultiPoint *geo)
{
  int ret = OB_SUCCESS;
  type_ = ObMVTType::MVT_POINT;
  point_num_ = geo->size();
  return ret;
}
int ObGeoMvtEncodeVisitor::visit(ObIWkbGeomLineString *geo)
{
  int ret = OB_SUCCESS;
  point_num_ = geo->size();
  type_ = ObMVTType::MVT_LINE;
  move_to_offset_ = encode_buffer_.size();
  if (OB_FAIL(encode_buffer_.push_back(encode_command(ObMVTCommand::CMD_MOVE_TO, 1)))) {
    LOG_WARN("failed to push back move to cmd", K(ret));
  } else if (OB_FAIL(encode_buffer_.push_back(UINT32_MAX))) {
    LOG_WARN("failed to push back value", K(ret));
  } else if (OB_FAIL(encode_buffer_.push_back(UINT32_MAX))) {
    LOG_WARN("failed to push back value", K(ret));
  } else if (OB_FAIL(encode_buffer_.push_back(encode_command(ObMVTCommand::CMD_LINE_TO, point_num_ - 1)))) {
    LOG_WARN("failed to push back line to cmd", K(ret));
  } else {
    line_to_offset_ = encode_buffer_.size() - 1;
    value_offset_ = move_to_offset_ + 1;
    point_idx_ = 0;
  }
  return ret;
}

int ObGeoMvtEncodeVisitor::visit(ObIWkbGeomLinearRing *geo)
{
  int ret = OB_SUCCESS;
  type_ = ObMVTType::MVT_RING;
  point_num_ = geo->size();
  move_to_offset_ = encode_buffer_.size();
  if (OB_FAIL(encode_buffer_.push_back(encode_command(ObMVTCommand::CMD_MOVE_TO, 1)))) {
    LOG_WARN("failed to push back move to cmd", K(ret));
  } else if (OB_FAIL(encode_buffer_.push_back(UINT32_MAX))) {
    LOG_WARN("failed to push back value", K(ret));
  } else if (OB_FAIL(encode_buffer_.push_back(UINT32_MAX))) {
    LOG_WARN("failed to push back value", K(ret));
  } else if (OB_FAIL(encode_buffer_.push_back(encode_command(ObMVTCommand::CMD_LINE_TO, point_num_ - 2)))) { // exclude start/end point
    LOG_WARN("failed to push back line to cmd", K(ret));
  } else {
    line_to_offset_ = encode_buffer_.size() - 1;
    value_offset_ = move_to_offset_ + 1;
    point_idx_ = 0;
  }
  return ret;
}

} // namespace common
} // namespace oceanbase
