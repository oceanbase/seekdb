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

#ifndef OCEANBASE_LIB_GEO_OB_S2ADAPTER_
#define OCEANBASE_LIB_GEO_OB_S2ADAPTER_


// Core-only builds retain the MBR storage container without including S2 in
// storage/SQL translation units. Byte encoding stays here in the host; MBR
// predicates and covering dispatch to leased GIS plugin services. Storage and
// query admission gates remain closed until the complete index path is ready.
#include <cmath>
#include <cstdio>
#include <memory>
#include "lib/container/ob_vector.h"
#include "lib/ob_errno.h"
#include "share/geo/ob_geo_common.h"

namespace oceanbase {
namespace common {

struct ObSrsBoundsItem;
typedef common::ObVector<uint64_t> ObS2Cellids;
static const int64_t OB_DEFAULT_MBR_SIZE = 32;

class ObSpatialMBR
{
public:
  ObSpatialMBR()
      : x_min_(NAN), x_max_(NAN), y_min_(NAN), y_max_(NAN),
        mbr_type_(ObDomainOpType::T_INVALID), is_point_(false), is_geog_(false) {}
  explicit ObSpatialMBR(ObDomainOpType rel_type)
      : x_min_(NAN), x_max_(NAN), y_min_(NAN), y_max_(NAN),
        mbr_type_(rel_type), is_point_(false), is_geog_(false) {}
  ObSpatialMBR(double x_min, double x_max, double y_min, double y_max,
               ObDomainOpType rel_type)
      : x_min_(x_min), x_max_(x_max), y_min_(y_min), y_max_(y_max),
        mbr_type_(rel_type), is_point_(false), is_geog_(false) {}

  int64_t to_string(char *buf, const int64_t buf_len) const
  {
    return (buf == NULL || buf_len <= 0) ? 0 :
      snprintf(buf, static_cast<size_t>(buf_len),
               "x_min_=%lf, x_max_=%lf, y_min_=%lf, y_max_=%lf, mbr_type_=%d",
               x_min_, x_max_, y_min_, y_max_, static_cast<int>(mbr_type_));
  }
  int to_char(char *, int64_t &) const;
  static int from_string(ObString &, ObDomainOpType, ObSpatialMBR &, bool = false);
  int filter(const ObSpatialMBR &, ObDomainOpType, bool &pass_through) const;
  bool is_point() const { return is_point_; }
  bool is_geog() const { return is_geog_; }
  ObDomainOpType get_type() const { return mbr_type_; }
  double get_xmin() const { return x_min_; }
  double get_xmax() const { return x_max_; }
  double get_ymin() const { return y_min_; }
  double get_ymax() const { return y_max_; }
  bool is_empty() const { return std::isnan(x_min_) && std::isnan(x_max_)
                                && std::isnan(y_min_) && std::isnan(y_max_); }

  double x_min_;
  double x_max_;
  double y_min_;
  double y_max_;
  ObDomainOpType mbr_type_;
  bool is_point_;
  bool is_geog_;
};

struct SpatialIndexState;

class ObS2Adapter final
{
public:
  ObS2Adapter(ObIAllocator *, bool, bool = false);
  ObS2Adapter(ObIAllocator *, bool, double);
  ~ObS2Adapter();
  static int64_t get_child_of_cellid(uint64_t, uint64_t &start, uint64_t &end);
  int64_t get_ancestors(uint64_t, ObS2Cellids &);
  int64_t init(const ObString &, const ObSrsBoundsItem * = NULL);
  int64_t get_cellids(ObS2Cellids &, bool);
  int64_t get_cellids_and_unrepeated_ancestors(ObS2Cellids &, ObS2Cellids &);
  int64_t get_inner_cover_cellids(ObS2Cellids &);
  int64_t get_mbr(ObSpatialMBR &);
private:
  ObIAllocator *allocator_;
  bool is_geog_;
  bool query_window_;
  bool need_buffer_;
  double distance_;
  std::unique_ptr<SpatialIndexState> state_;
  DISALLOW_COPY_AND_ASSIGN(ObS2Adapter);
};

} // namespace common
} // namespace oceanbase


#endif // OCEANBASE_LIB_OB_S2ADAPTER_
