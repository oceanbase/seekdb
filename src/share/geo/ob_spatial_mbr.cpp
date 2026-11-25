/*
 * Copyright (c) 2026 OceanBase.
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
#include "ob_spatial_mbr.h"
#include "rust/spatial_covering.h"

namespace oceanbase {
namespace common {
namespace {

spatial::Mbr to_rust_mbr(const ObSpatialMBR &mbr)
{
  return spatial::Mbr {
      mbr.x_min_, mbr.x_max_, mbr.y_min_, mbr.y_max_, mbr.is_point_, mbr.is_geog_};
}

int relation_error(const spatial::MbrFilterResult &result)
{
  switch (result.code) {
    case spatial::ErrorCode::InvalidArgument:
      return OB_INVALID_ARGUMENT;
    case spatial::ErrorCode::Unsupported:
      return OB_NOT_SUPPORTED;
    default:
      return OB_ERR_UNEXPECTED;
  }
}

} // namespace

int ObSpatialMBR::filter(const ObSpatialMBR &other, ObDomainOpType type, bool &pass_through) const
{
  INIT_SUCC(ret);
  spatial::MbrRelation relation;
  switch (type) {
    case ObDomainOpType::T_GEO_COVERS:
      relation = spatial::MbrRelation::Covers;
      break;
    case ObDomainOpType::T_GEO_DWITHIN:
      relation = spatial::MbrRelation::DWithin;
      break;
    case ObDomainOpType::T_GEO_INTERSECTS:
      relation = spatial::MbrRelation::Intersects;
      break;
    case ObDomainOpType::T_GEO_COVEREDBY:
      relation = spatial::MbrRelation::CoveredBy;
      break;
    case ObDomainOpType::T_GEO_DFULLYWITHIN:
      relation = spatial::MbrRelation::DFullyWithin;
      break;
    default:
      ret = OB_INVALID_ARGUMENT;
      break;
  }
  if (OB_SUCC(ret)) {
    auto result = spatial::spatial_filter_mbr(to_rust_mbr(*this), to_rust_mbr(other), relation);
    if (result.code != spatial::ErrorCode::None) {
      ret = relation_error(result);
      LOG_WARN("failed to filter spatial MBR", K(ret), "reason", result.message.c_str());
    } else {
      pass_through = result.pass_through;
    }
  }
  return ret;
}

int ObSpatialMBR::to_char(char *buf, int64_t &buf_len) const
{
  INIT_SUCC(ret);
  int32_t pos = 0;
  if (OB_ISNULL(buf)) {
    ret = OB_INVALID_ARGUMENT;
  } else if (is_point_) {
    MEMCPY(buf + pos, reinterpret_cast<const char *>(&x_min_), sizeof(x_min_));
    pos += sizeof(double);
    MEMCPY(buf + pos, reinterpret_cast<const char *>(&y_min_), sizeof(y_min_));
    pos += sizeof(double);
  } else {
    MEMCPY(buf + pos, reinterpret_cast<const char *>(&y_min_), sizeof(y_min_));
    pos += sizeof(double);
    MEMCPY(buf + pos, reinterpret_cast<const char *>(&y_max_), sizeof(y_max_));
    pos += sizeof(double);
    MEMCPY(buf + pos, reinterpret_cast<const char *>(&x_min_), sizeof(x_min_));
    pos += sizeof(double);
    MEMCPY(buf + pos, reinterpret_cast<const char *>(&x_max_), sizeof(x_max_));
    pos += sizeof(double);
  }
  buf_len = pos;
  return ret;
}

int ObSpatialMBR::from_string(ObString &mbr_str,
                              ObDomainOpType type,
                              ObSpatialMBR &spa_mbr,
                              bool is_point)
{
  INIT_SUCC(ret);
  const int64_t expected_size = is_point ? 2 * sizeof(double) : 4 * sizeof(double);
  const char *data = mbr_str.ptr();
  if (OB_ISNULL(data) || mbr_str.length() != expected_size) {
    ret = OB_INVALID_ARGUMENT;
  } else if (is_point) {
    double x_min = *reinterpret_cast<const double *>(data);
    data += sizeof(double);
    double y_min = *reinterpret_cast<const double *>(data);
    new (&spa_mbr) ObSpatialMBR(x_min, x_min, y_min, y_min, type);
  } else {
    double y_min = *reinterpret_cast<const double *>(data);
    data += sizeof(double);
    double y_max = *reinterpret_cast<const double *>(data);
    data += sizeof(double);
    double x_min = *reinterpret_cast<const double *>(data);
    data += sizeof(double);
    double x_max = *reinterpret_cast<const double *>(data);
    new (&spa_mbr) ObSpatialMBR(x_min, x_max, y_min, y_max, type);
  }
  return ret;
}

OB_DEF_SERIALIZE(ObSpatialMBR)
{
  INIT_SUCC(ret);
  OB_UNIS_ENCODE(y_min_);
  OB_UNIS_ENCODE(y_max_);
  OB_UNIS_ENCODE(x_min_);
  OB_UNIS_ENCODE(x_max_);
  OB_UNIS_ENCODE(static_cast<int64_t>(mbr_type_));
  OB_UNIS_ENCODE(is_point_);
  OB_UNIS_ENCODE(is_geog_);
  return ret;
}

OB_DEF_SERIALIZE_SIZE(ObSpatialMBR)
{
  int64_t len = 0;
  OB_UNIS_ADD_LEN(y_min_);
  OB_UNIS_ADD_LEN(y_max_);
  OB_UNIS_ADD_LEN(x_min_);
  OB_UNIS_ADD_LEN(x_max_);
  OB_UNIS_ADD_LEN(static_cast<int64_t>(mbr_type_));
  OB_UNIS_ADD_LEN(is_point_);
  OB_UNIS_ADD_LEN(is_geog_);
  return len;
}

OB_DEF_DESERIALIZE(ObSpatialMBR)
{
  INIT_SUCC(ret);
  int64_t mbr_type = 0;
  OB_UNIS_DECODE(y_min_);
  OB_UNIS_DECODE(y_max_);
  OB_UNIS_DECODE(x_min_);
  OB_UNIS_DECODE(x_max_);
  OB_UNIS_DECODE(mbr_type);
  if (OB_SUCC(ret)) {
    mbr_type_ = static_cast<ObDomainOpType>(mbr_type);
  }
  OB_UNIS_DECODE(is_point_);
  OB_UNIS_DECODE(is_geog_);
  return ret;
}

} // namespace common
} // namespace oceanbase
