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

#define USING_LOG_PREFIX LIB

#include "share/geo/ob_geo_func_relation.h"

#include "share/geo/ob_geo_func_common.h"

namespace oceanbase
{
namespace common
{
namespace
{

namespace bg = boost::geometry;

using Mask = bg::de9im::mask;

const Mask EQUALS_MASK("T*F**FFF*");
const Mask DISJOINT_MASK("FF*FF****");
const Mask WITHIN_MASK("T*F**F***");
const Mask OVERLAPS_LINEAR_MASK("1*T***T**");

int get_relation_mask(ObGeoRelationPredicate predicate, const Mask *&mask, bool &negate)
{
  int ret = OB_SUCCESS;
  negate = false;
  switch (predicate) {
    case ObGeoRelationPredicate::EQUALS:
      mask = &EQUALS_MASK;
      break;
    case ObGeoRelationPredicate::INTERSECTS:
      mask = &DISJOINT_MASK;
      negate = true;
      break;
    case ObGeoRelationPredicate::WITHIN:
      mask = &WITHIN_MASK;
      break;
    case ObGeoRelationPredicate::OVERLAPS:
      mask = &OVERLAPS_LINEAR_MASK;
      break;
    default:
      ret = OB_INVALID_ARGUMENT;
      break;
  }
  return ret;
}

template <typename Geometry1, typename Geometry2>
int eval_linear_relation(const ObGeometry *g1,
                         const ObGeometry *g2,
                         const Mask &mask,
                         bool negate,
                         bool &result)
{
  int ret = OB_SUCCESS;
  const Geometry1 *geo1 = reinterpret_cast<const Geometry1 *>(g1->val());
  const Geometry2 *geo2 = reinterpret_cast<const Geometry2 *>(g2->val());
  if (OB_ISNULL(geo1) || OB_ISNULL(geo2)) {
    ret = OB_ERR_NULL_VALUE;
  } else {
    result = bg::relate(*geo1, *geo2, mask);
    if (negate) {
      result = !result;
    }
  }
  return ret;
}

} // namespace

int eval_cartesian_wkb_linear_relation(const ObGeometry *g1,
                                       const ObGeometry *g2,
                                       ObGeoRelationPredicate predicate,
                                       bool &result)
{
  int ret = OB_SUCCESS;
  const Mask *mask = nullptr;
  bool negate = false;
  if (OB_ISNULL(g1) || OB_ISNULL(g2) || g1->is_tree() || g2->is_tree()
      || g1->crs() != ObGeoCRS::Cartesian || g2->crs() != ObGeoCRS::Cartesian) {
    ret = OB_INVALID_ARGUMENT;
  } else if (OB_FAIL(get_relation_mask(predicate, mask, negate))) {
  } else if (g1->type() == ObGeoType::LINESTRING && g2->type() == ObGeoType::LINESTRING) {
    ret = eval_linear_relation<ObWkbGeomLineString, ObWkbGeomLineString>(
        g1, g2, *mask, negate, result);
  } else if (g1->type() == ObGeoType::LINESTRING && g2->type() == ObGeoType::MULTILINESTRING) {
    ret = eval_linear_relation<ObWkbGeomLineString, ObWkbGeomMultiLineString>(
        g1, g2, *mask, negate, result);
  } else if (g1->type() == ObGeoType::MULTILINESTRING && g2->type() == ObGeoType::LINESTRING) {
    ret = eval_linear_relation<ObWkbGeomMultiLineString, ObWkbGeomLineString>(
        g1, g2, *mask, negate, result);
  } else if (g1->type() == ObGeoType::MULTILINESTRING
             && g2->type() == ObGeoType::MULTILINESTRING) {
    ret = eval_linear_relation<ObWkbGeomMultiLineString, ObWkbGeomMultiLineString>(
        g1, g2, *mask, negate, result);
  } else {
    ret = OB_INVALID_ARGUMENT;
  }
  return ret;
}

} // namespace common
} // namespace oceanbase
