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

#ifndef OCEANBASE_LIB_GEO_OB_GEO_WKB_VISITOR_
#define OCEANBASE_LIB_GEO_OB_GEO_WKB_VISITOR_
#include "lib/geo/ob_geo_visitor.h"
#include "lib/geo/ob_srs_info.h"

namespace oceanbase
{
namespace common
{

class ObGeoWkbVisitor : public ObEmptyGeoVisitor
{
public:
  // need_convert: Configure whether conversion is required when srs type is GEOGRAPHIC_SRS.
  ObGeoWkbVisitor(const ObSrsItem *srs, ObString *buf, bool need_convert = true)
      : srs_(srs),
        buffer_(buf),
        need_convert_(need_convert)
  {}
  virtual ~ObGeoWkbVisitor() {}
  bool prepare(ObGeometry *geo) override { UNUSED(geo); return true; }

  // wkb
  int visit(ObIWkbGeometry *geo) override;
  bool is_end(ObIWkbGeometry *geo) override { UNUSED(geo); return true; }

  // tree
  int visit(ObPoint *geo) override;
  int visit(ObCartesianLineString *geo) override;
  int visit(ObGeographLineString *geo) override;
  int visit(ObGeographPolygon *geo) override;
  int visit(ObCartesianPolygon *geo) override;

  int visit(ObGeometrycollection *geo) override;  

  bool is_end(ObCartesianLineString *geo) override { UNUSED(geo); return true; }
  bool is_end(ObGeographLineString *geo) override { UNUSED(geo); return true; }
  bool is_end(ObGeographPolygon *geo) override { UNUSED(geo); return true; }
  bool is_end(ObCartesianPolygon *geo) override { UNUSED(geo); return true; }

  template<typename T>
  int write_to_buffer(T data, uint64_t len)
  {
    int ret = OB_SUCCESS;
    if (buffer_->write(reinterpret_cast<char *>(&data), len) != len) {
      ret = OB_BUF_NOT_ENOUGH;
      OB_LOG(WARN, "failed to write buffer", K(ret));
    }
    return ret;
  }

  template<typename T>
  int write_head_info(T *geo);

  int write_cartesian_point(double x, double y);
  int write_geograph_point(double x, double y);

  void set_wkb_buffer(ObString *res) { buffer_ = res; }
  void set_srs(const ObSrsItem *srs) { srs_ = srs; }
private:
  const ObSrsItem *srs_;
  ObString *buffer_;
  bool need_convert_;
  uint32_t curr_pos_;
  DISALLOW_COPY_AND_ASSIGN(ObGeoWkbVisitor);

};

} // namespace common
} // namespace oceanbase

#endif
