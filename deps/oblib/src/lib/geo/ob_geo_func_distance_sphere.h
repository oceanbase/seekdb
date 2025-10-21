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

#ifndef OCEANBASE_LIB_OB_GEO_FUNC_DISTANCE_SPHERE_
#define OCEANBASE_LIB_OB_GEO_FUNC_DISTANCE_SPHERE_

#include "lib/geo/ob_geo_func_common.h"

namespace oceanbase
{
namespace common
{

class ObGeoFuncDistanceSphereUtil
{
public:
  template <typename GeoType1, typename GeoType2>
  static int eval(const GeoType1 *g1,
                  const GeoType2 *g2,
                  const common::ObGeoEvalCtx &context,
                  double &result);
  template <typename GeoType1, typename GeoType2>
  static int eval(const common::ObGeometry *g1,
                  const common::ObGeometry *g2,
                  const common::ObGeoEvalCtx &context,
                  double &result);
  static int reinterpret_as_degrees(const common::ObWkbGeomPoint *cart_pt,
                                    common::ObWkbGeogPoint &geog_pt,
                                    double &result);
  static int reinterpret_as_degrees(common::ObIAllocator *allocator,
                                    const common::ObGeometry *g,
                                    const common::ObWkbGeogMultiPoint *&geog_mpt,
                                    double &result);
private:
  static int reinterpret_as_degrees(double lon_deg,
                                    double lat_deg,
                                    double &x,
                                    double &y,
                                    double &result);
};

class ObGeoFuncDistanceSphere
{
public:
  ObGeoFuncDistanceSphere();
  virtual ~ObGeoFuncDistanceSphere() = default;
  static int eval(const common::ObGeoEvalCtx &gis_context, double &result);
};

} // sql
} // oceanbase
#endif // OCEANBASE_LIB_OB_GEO_FUNC_DISTANCE_SPHERE_
