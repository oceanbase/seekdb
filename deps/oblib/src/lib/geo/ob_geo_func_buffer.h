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

#ifndef OCEANBASE_LIB_OB_GEO_FUNC_BUFFER_
#define OCEANBASE_LIB_OB_GEO_FUNC_BUFFER_

#include "lib/geo/ob_geo_func_common.h"

namespace oceanbase
{
namespace common
{

enum class ObGeoBufferStrategyStateType
{
  JR_ER_PC = 0, // join_round, end_round, point_circle
  JR_ER_PS = 1, // join_round, end_round, point_square
  JR_EF_PC = 2, // join_round, end_flat, point_circle
  JR_EF_PS = 3, // join_round, end_flat, point_square
  JM_ER_PC = 4, // join_miter, end_round, point_circle
  JM_ER_PS = 5, // join_miter, end_round, point_square
  JM_EF_PC = 6, // join_miter, end_flat, point_circle
  JM_EF_PS = 7, // join_miter, end_flat, point_square
};

class ObGeoFuncBuffer
{
public:
  ObGeoFuncBuffer();
  ~ObGeoFuncBuffer();
  static int eval(const common::ObGeoEvalCtx &gis_context, common::ObGeometry *&result);
};

} // sql
} // oceanbase
#endif // OCEANBASE_LIB_OB_GEO_FUNC_BUFFER
