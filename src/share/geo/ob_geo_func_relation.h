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

#ifndef OCEANBASE_LIB_OB_GEO_FUNC_RELATION_H_
#define OCEANBASE_LIB_OB_GEO_FUNC_RELATION_H_

#include <cstdint>

namespace oceanbase
{
namespace common
{

class ObGeometry;

enum class ObGeoRelationPredicate : uint8_t
{
  EQUALS,
  INTERSECTS,
  WITHIN,
  OVERLAPS,
};

// Share one Boost.Geometry relate instantiation for each ordered pair of
// Cartesian WKB linear types.  Callers retain their existing SQL-level
// dispatch and nullability semantics.
int eval_cartesian_wkb_linear_relation(const ObGeometry *g1,
                                       const ObGeometry *g2,
                                       ObGeoRelationPredicate predicate,
                                       bool &result);

} // namespace common
} // namespace oceanbase

#endif // OCEANBASE_LIB_OB_GEO_FUNC_RELATION_H_
