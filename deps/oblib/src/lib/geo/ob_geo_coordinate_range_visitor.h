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

#ifndef OCEANBASE_LIB_GEO_OB_GEO_COORDINATE_RANGE_VISITOR_
#define OCEANBASE_LIB_GEO_OB_GEO_COORDINATE_RANGE_VISITOR_
#include "lib/geo/ob_geo_visitor.h"
#include "lib/geo/ob_srs_info.h"

namespace oceanbase
{
namespace common
{
struct ObGeoCoordRangeResult
{
  bool is_lati_out_range_ = false;
  bool is_long_out_range_ = false;
  double value_out_range_ = NAN;
};

class ObGeoCoordinateRangeVisitor : public ObEmptyGeoVisitor
{
public:
  ObGeoCoordinateRangeVisitor(const ObSrsItem *srs, bool is_normalized = true)
    : srs_(srs), is_lati_out_range_(false), is_long_out_range_(false),
      value_out_range_(NAN), is_normalized_(is_normalized) {}
  virtual ~ObGeoCoordinateRangeVisitor() {}
  bool is_latitude_out_of_range() { return is_lati_out_range_; }
  bool is_longtitude_out_of_range() { return is_long_out_range_; }
  double value_out_of_range() { return value_out_range_; }
  bool prepare(ObGeometry *geo) override;
  int visit(ObIWkbGeogPoint *geo) override;
  int visit(ObGeometry *geo) override { UNUSED(geo); return OB_SUCCESS; } // all cartesian geometry is in range
  bool is_end(ObGeometry *geo) override { UNUSED(geo); return is_lati_out_range_ || is_long_out_range_; }
  void reset();

  int visit(ObGeographPoint *geo) override;
  void get_coord_range_result(ObGeoCoordRangeResult& result);
  static int calculate_point_range(const ObSrsItem *srs,
                                   double longti, 
                                   double lati, 
                                   bool is_normalized, 
                                   ObGeoCoordRangeResult &result);
  bool set_after_visitor() { return false; }

private:
  const ObSrsItem *srs_;
  bool is_lati_out_range_;
  bool is_long_out_range_;
  double value_out_range_;
  bool is_normalized_;
  DISALLOW_COPY_AND_ASSIGN(ObGeoCoordinateRangeVisitor);
};

} // namespace common
} // namespace oceanbase

#endif
