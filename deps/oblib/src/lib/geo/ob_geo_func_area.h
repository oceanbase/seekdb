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

#ifndef OCEANBASE_LIB_OB_GEO_FUNC_AREA_
#define OCEANBASE_LIB_OB_GEO_FUNC_AREA_

#include "lib/geo/ob_geo_func_common.h"

namespace oceanbase
{
namespace common
{

class ObGeoFuncArea
{
public:
  ObGeoFuncArea();
  ~ObGeoFuncArea();
  static int eval(const common::ObGeoEvalCtx &gis_context, double &result);
};

} // sql
} // oceanbase
#endif // OCEANBASE_LIB_OB_GEO_FUNC_AREA_
