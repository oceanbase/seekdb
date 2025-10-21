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
#ifndef OCEANBASE_LIB_GEO_OB_GEO_AFFINE_VISITOR_
#define OCEANBASE_LIB_GEO_OB_GEO_AFFINE_VISITOR_
#include "lib/geo/ob_geo_visitor.h"
#include "lib/geo/ob_geo_utils.h"
namespace oceanbase
{
namespace common
{

class ObGeoAffineVisitor : public ObEmptyGeoVisitor
{
public:
  explicit ObGeoAffineVisitor(const ObAffineMatrix *affine) : affine_(affine)
  {}
  virtual ~ObGeoAffineVisitor()
  {}
  bool prepare(ObGeometry *geo);
  int visit(ObCartesianPoint *geo);
  int visit(ObGeographPoint *geo);
  int visit(ObGeometry *geo)
  {
    UNUSED(geo);
    return OB_SUCCESS;
  }
  template<typename PtType>
  void affine(PtType *point);

private:
  const ObAffineMatrix *affine_;
  DISALLOW_COPY_AND_ASSIGN(ObGeoAffineVisitor);
};

}  // namespace common
}  // namespace oceanbase

#endif  // OCEANBASE_LIB_GEO_OB_GEO_AFFINE_VISITOR_
