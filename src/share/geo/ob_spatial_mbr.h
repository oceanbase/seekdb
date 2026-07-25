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

#ifndef OCEANBASE_LIB_GEO_OB_SPATIAL_MBR_H_
#define OCEANBASE_LIB_GEO_OB_SPATIAL_MBR_H_

#include <cmath>

#include "lib/container/ob_vector.h"
#include "lib/utility/ob_unify_serialize.h"
#include "share/geo/ob_geo_common.h"

class S2LatLngRect;

namespace oceanbase
{
namespace common
{

class ObCartesianBox;

typedef common::ObVector<uint64_t> ObS2Cellids;
static const int64_t OB_DEFAULT_MBR_SIZE = 32;

class ObSpatialMBR
{
  OB_UNIS_VERSION(4);
public:
  ObSpatialMBR()
      : x_min_(NAN),
        x_max_(NAN),
        y_min_(NAN),
        y_max_(NAN)
  {}

  ObSpatialMBR(ObDomainOpType rel_type)
      : x_min_(NAN),
        x_max_(NAN),
        y_min_(NAN),
        y_max_(NAN),
        mbr_type_(rel_type),
        is_point_(false),
        is_geog_(false)
  {}

  ObSpatialMBR(double x_min, double x_max, double y_min, double y_max, ObDomainOpType rel_type)
      : x_min_(x_min),
        x_max_(x_max),
        y_min_(y_min),
        y_max_(y_max),
        mbr_type_(rel_type),
        is_point_(false),
        is_geog_(false)
  {}

  ~ObSpatialMBR() {}

  int64_t to_string(char *buf, const int64_t buf_len) const
  {
    int64_t pos = 0;
    databuff_printf(buf,
                    buf_len,
                    pos,
                    "x_min_=%lf, x_max_=%lf, y_min_=%lf, y_max_=%lf, mbr_type_=%d",
                    x_min_,
                    x_max_,
                    y_min_,
                    y_max_,
                    static_cast<int>(mbr_type_));
    return pos;
  }

  int to_char(char *buf, int64_t &buf_len) const;
  static int from_string(ObString &mbr_str,
                         ObDomainOpType type,
                         ObSpatialMBR &spa_mbr,
                         bool is_point = false);
  int filter(const ObSpatialMBR &other, ObDomainOpType type, bool &pass_through) const;
  OB_INLINE bool is_point() const { return is_point_; }
  OB_INLINE bool is_geog() const { return is_geog_; }
  OB_INLINE ObDomainOpType get_type() const { return mbr_type_; }
  OB_INLINE double get_xmin() const { return x_min_; }
  OB_INLINE double get_xmax() const { return x_max_; }
  OB_INLINE double get_ymin() const { return y_min_; }
  OB_INLINE double get_ymax() const { return y_max_; }
  OB_INLINE bool is_empty() const
  {
    return std::isnan(x_min_) && std::isnan(x_max_) && std::isnan(y_min_) && std::isnan(y_max_);
  }

public:
  int generate_latlng_rect(S2LatLngRect &rect) const;
  int generate_box(ObCartesianBox &rect) const;
  double x_min_;
  double x_max_;
  double y_min_;
  double y_max_;
  ObDomainOpType mbr_type_;
  bool is_point_;
  bool is_geog_;
};

} // namespace common
} // namespace oceanbase

#endif // OCEANBASE_LIB_GEO_OB_SPATIAL_MBR_H_
