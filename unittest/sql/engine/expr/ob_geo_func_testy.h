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

#ifndef OCEANBASE_SQL_OB_GEO_FUNC_TESTY_H_
#define OCEANBASE_SQL_OB_GEO_FUNC_TESTY_H_

#include "lib/geo/ob_geo_func_common.h"

namespace oceanbase
{
namespace sql
{

extern int g_test_geo_unary_func_result[3][8];
extern int g_test_geo_binary_func_result[3][8][3][8];

class ObGeoFuncMockY
{
public:
  ObGeoFuncMockY();
  virtual ~ObGeoFuncMockY() = default;
  static int eval(const common::ObGeoEvalCtx &gis_context, int &result);
};

} // sql
} // oceanbase
#endif // OCEANBASE_SQL_OB_GEO_FUNC_TESTY_H_
