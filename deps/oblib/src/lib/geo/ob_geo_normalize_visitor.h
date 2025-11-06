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
#ifndef OCEANBASE_LIB_GEO_OB_GEO_NORMALIZE_VISITOR_
#define OCEANBASE_LIB_GEO_OB_GEO_NORMALIZE_VISITOR_
#include "lib/geo/ob_geo_visitor.h"
#include "lib/geo/ob_srs_info.h"
namespace oceanbase
{
namespace common
{

class ObGeoNormalizeVisitor : public ObEmptyGeoVisitor
{
public:
  ObGeoNormalizeVisitor(const ObSrsItem *srs, bool no_srs = false)
    : srs_(srs), no_srs_(no_srs) {}
  virtual ~ObGeoNormalizeVisitor() {}
  bool prepare(ObGeometry *geo);
  int visit(ObIWkbGeogPoint *geo);
  int visit(ObIWkbGeomPoint *geo);
  int visit(ObIWkbGeometry *geo) { UNUSED(geo); return OB_SUCCESS; }

private:
  int normalize(ObIWkbPoint *geo);

private:
  const ObSrsItem *srs_;
  bool no_srs_; // for st_transform, only proj4text is given
  DISALLOW_COPY_AND_ASSIGN(ObGeoNormalizeVisitor);
};

} // namespace common
} // namespace oceanbase

#endif // OCEANBASE_LIB_GEO_OB_GEO_NORMALIZE_VISITOR_
