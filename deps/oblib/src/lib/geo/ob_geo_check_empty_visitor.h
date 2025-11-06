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
#ifndef OCEANBASE_LIB_GEO_OB_GEO_CHECK_EMPTY_VISITOR_
#define OCEANBASE_LIB_GEO_OB_GEO_CHECK_EMPTY_VISITOR_
#include "lib/geo/ob_geo_visitor.h"
#include "lib/geo/ob_srs_info.h"
namespace oceanbase
{
namespace common
{

// ST_GEOMFROMTEXT('GEOMETRYCOLLECTION(GEOMETRYCOLLECTION())')) is empty
class ObGeoCheckEmptyVisitor : public ObEmptyGeoVisitor
{
public:
  ObGeoCheckEmptyVisitor() : is_empty_(true) {}
  virtual ~ObGeoCheckEmptyVisitor() {}
  bool prepare(ObGeometry *geo) { return (OB_NOT_NULL(geo)); }
  int visit(ObIWkbPoint *geo) { return check_empty(geo); }
  int visit(ObPoint *geo) { return check_empty(geo); }
  int visit(ObGeometry *geo) { UNUSED(geo); return OB_SUCCESS; } 
  // stop when found first non-empty point
  bool is_end(ObGeometry *geo) { UNUSED(geo); return (is_empty_ == false); }
  bool get_result() { return is_empty_; };
  bool set_after_visitor() { return false; }

private:
  int check_empty(ObIWkbPoint *geo) { is_empty_ = geo->is_empty(); return OB_SUCCESS; }
  int check_empty(ObPoint *geo) { is_empty_ = geo->is_empty(); return OB_SUCCESS; }

private:
  bool is_empty_;
  
  DISALLOW_COPY_AND_ASSIGN(ObGeoCheckEmptyVisitor);
};

} // namespace common
} // namespace oceanbase

#endif // OCEANBASE_LIB_GEO_OB_GEO_CHECK_EMPTY_VISITOR_
