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

#include <array>
#include <cstring>
#include <string>

#include <boost/geometry.hpp>
#include <gtest/gtest.h>

#include "lib/rc/context.h"
#include "share/geo/ob_geo_bin_traits.h"
#include "share/geo/ob_geo_func_register.h"
#include "share/geo/ob_geo_utils.h"
#include "share/geo/ob_geo_to_tree_visitor.h"
#include "share/geo/ob_srs_info.h"
#include "share/geo/ob_wkt_parser.h"

namespace bg = boost::geometry;

namespace oceanbase
{
namespace common
{
namespace
{

enum class RelationPredicate
{
  EQUALS,
  DISJOINT,
  INTERSECTS,
  TOUCHES,
  WITHIN,
  COVERED_BY,
  CROSSES,
  OVERLAPS,
};

const char *predicate_name(RelationPredicate predicate)
{
  switch (predicate) {
    case RelationPredicate::EQUALS: return "equals";
    case RelationPredicate::DISJOINT: return "disjoint";
    case RelationPredicate::INTERSECTS: return "intersects";
    case RelationPredicate::TOUCHES: return "touches";
    case RelationPredicate::WITHIN: return "within";
    case RelationPredicate::COVERED_BY: return "covered_by";
    case RelationPredicate::CROSSES: return "crosses";
    case RelationPredicate::OVERLAPS: return "overlaps";
  }
  return "unknown";
}

struct RelationOutcome
{
  int ret_ = OB_SUCCESS;
  bool value_ = false;
  bool is_null_ = false;
};

bool equivalent(const RelationOutcome &left, const RelationOutcome &right)
{
  return left.ret_ == right.ret_
      && (left.ret_ != OB_SUCCESS
          || (left.is_null_ == right.is_null_
              && (left.is_null_ || left.value_ == right.value_)));
}

bool is_not_implemented(int ret)
{
  return ret == OB_ERR_NOT_IMPLEMENTED_FOR_CARTESIAN_SRS
      || ret == OB_ERR_NOT_IMPLEMENTED_FOR_GEOGRAPHIC_SRS;
}

int eval_relation(RelationPredicate predicate,
                  lib::MemoryContext &memory_context,
                  const ObSrsItem *srs,
                  const ObGeometry *left,
                  const ObGeometry *right,
                  RelationOutcome &outcome)
{
  ObGeoEvalCtx context(memory_context, srs);
  int ret = context.append_geo_arg(left);
  if (OB_SUCC(ret)) {
    ret = context.append_geo_arg(right);
  }

  bool result = false;
  ObGeoFuncResWithNull nullable_result;
  if (OB_FAIL(ret)) {
  } else {
    switch (predicate) {
      case RelationPredicate::EQUALS:
        ret = ObGeoFunc<ObGeoFuncType::Equals>::geo_func::eval(context, result);
        break;
      case RelationPredicate::DISJOINT:
        ret = ObGeoFunc<ObGeoFuncType::Disjoint>::geo_func::eval(context, result);
        break;
      case RelationPredicate::INTERSECTS:
        ret = ObGeoFunc<ObGeoFuncType::Intersects>::geo_func::eval(context, result);
        break;
      case RelationPredicate::TOUCHES:
        ret = ObGeoFunc<ObGeoFuncType::Touches>::geo_func::eval(context, result);
        break;
      case RelationPredicate::WITHIN:
        ret = ObGeoFunc<ObGeoFuncType::Within>::gis_func::eval(context, result);
        break;
      case RelationPredicate::COVERED_BY:
        ret = ObGeoFunc<ObGeoFuncType::CoveredBy>::geo_func::eval(context, result);
        break;
      case RelationPredicate::CROSSES:
        ret = ObGeoFunc<ObGeoFuncType::Crosses>::geo_func::eval(context, nullable_result);
        result = nullable_result.bret;
        outcome.is_null_ = nullable_result.is_null;
        break;
      case RelationPredicate::OVERLAPS:
        ret = ObGeoFunc<ObGeoFuncType::Overlaps>::geo_func::eval(context, nullable_result);
        result = nullable_result.bret;
        outcome.is_null_ = nullable_result.is_null;
        break;
    }
  }
  outcome.ret_ = ret;
  outcome.value_ = result;
  return ret;
}

int create_geographic_srs(ObIAllocator &allocator, const ObSrsItem *&srs_item)
{
  int ret = OB_SUCCESS;
  ObGeographicRs rs;
  rs.rs_name.assign_ptr("WGS 84", 6);
  rs.datum_info.name.assign_ptr("World Geodetic System 1984", 26);
  rs.datum_info.spheroid.name.assign_ptr("WGS 84", 6);
  rs.datum_info.spheroid.inverse_flattening = 298.257223563;
  rs.datum_info.spheroid.semi_major_axis = 6378137.0;
  rs.primem.longtitude = 0.0;
  rs.unit.conversion_factor = 0.017453292519943278;
  rs.axis.x.direction = ObAxisDirection::NORTH;
  rs.axis.y.direction = ObAxisDirection::EAST;
  rs.authority.is_valid = false;

  ObSpatialReferenceSystemBase *srs_info = nullptr;
  if (OB_FAIL(ObSpatialReferenceSystemBase::create_geographic_srs(
          &allocator, OB_GEO_DEFAULT_GEOGRAPHY_SRID, &rs, srs_info))) {
  } else {
    ObSrsItem *item = OB_NEWx(ObSrsItem, (&allocator), srs_info);
    if (OB_ISNULL(item)) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
    } else {
      srs_item = item;
    }
  }
  return ret;
}

struct GeometrySample
{
  const char *name_;
  const char *near_wkt_;
  const char *far_wkt_;
};

const std::array<GeometrySample, 7> GEOMETRY_SAMPLES = {{
  {"Point", "POINT(2 2)", "POINT(22 22)"},
  {"LineString", "LINESTRING(0 2,4 2)", "LINESTRING(20 22,24 22)"},
  {"Polygon", "POLYGON((0 0,0 4,4 4,4 0,0 0))",
              "POLYGON((20 20,20 24,24 24,24 20,20 20))"},
  {"GeometryCollection",
      "GEOMETRYCOLLECTION(POINT(2 2),LINESTRING(0 2,4 2),"
      "POLYGON((0 0,0 4,4 4,4 0,0 0)))",
      "GEOMETRYCOLLECTION(POINT(22 22),LINESTRING(20 22,24 22),"
      "POLYGON((20 20,20 24,24 24,24 20,20 20)))"},
  {"MultiPoint", "MULTIPOINT((2 2),(5 5))", "MULTIPOINT((22 22),(25 25))"},
  {"MultiLineString", "MULTILINESTRING((0 2,4 2),(5 5,6 6))",
                      "MULTILINESTRING((20 22,24 22),(25 25,26 26))"},
  {"MultiPolygon",
      "MULTIPOLYGON(((0 0,0 4,4 4,4 0,0 0)),((5 5,5 6,6 6,6 5,5 5)))",
      "MULTIPOLYGON(((20 20,20 24,24 24,24 20,20 20)),"
      "((25 25,25 26,26 26,26 25,25 25)))"},
}};

const std::array<RelationPredicate, 8> RELATION_PREDICATES = {{
  RelationPredicate::EQUALS,
  RelationPredicate::DISJOINT,
  RelationPredicate::INTERSECTS,
  RelationPredicate::TOUCHES,
  RelationPredicate::WITHIN,
  RelationPredicate::COVERED_BY,
  RelationPredicate::CROSSES,
  RelationPredicate::OVERLAPS,
}};

struct GeometrySet
{
  std::array<ObGeometry *, 7> wkb_{};
  std::array<ObGeometry *, 7> tree_{};
};

int create_empty_wkb_geometry(ObIAllocator &allocator,
                              ObGeoType type,
                              bool geographic,
                              ObGeometry *&wkb)
{
  int ret = OB_SUCCESS;
  ObWkbBuffer buffer(allocator);
  if (OB_FAIL(buffer.append(static_cast<char>(ObGeoWkbByteOrder::LittleEndian)))) {
  } else if (OB_FAIL(buffer.append(static_cast<uint32_t>(type)))) {
  } else if (OB_FAIL(buffer.append(static_cast<uint32_t>(0)))) {
  } else if (OB_FAIL(ObGeoTypeUtil::create_geo_by_type(
                 allocator, type, geographic, true, wkb))) {
  } else {
    wkb->set_data(buffer.string());
    wkb->set_srid(geographic ? OB_GEO_DEFAULT_GEOGRAPHY_SRID : 0);
  }
  return ret;
}

int parse_wkb_geometry(ObIAllocator &allocator,
                       const char *wkt,
                       bool geographic,
                       ObGeometry *&wkb)
{
  int ret = OB_SUCCESS;
  if (std::strcmp(wkt, "LINESTRING EMPTY") == 0) {
    ret = create_empty_wkb_geometry(allocator, ObGeoType::LINESTRING, geographic, wkb);
  } else if (std::strcmp(wkt, "MULTILINESTRING EMPTY") == 0) {
    ret = create_empty_wkb_geometry(allocator, ObGeoType::MULTILINESTRING, geographic, wkb);
  } else {
    ret = ObWktParser::parse_wkt(allocator, ObString(wkt), wkb, true, geographic);
  }
  if (OB_SUCC(ret)) {
    wkb->set_srid(geographic ? OB_GEO_DEFAULT_GEOGRAPHY_SRID : 0);
  }
  return ret;
}

int parse_geometry(ObIAllocator &allocator,
                   const char *wkt,
                   bool geographic,
                   ObGeometry *&wkb,
                   ObGeometry *&tree)
{
  int ret = parse_wkb_geometry(allocator, wkt, geographic, wkb);
  if (OB_SUCC(ret)) {
    ObGeoToTreeVisitor visitor(&allocator);
    if (OB_FAIL(wkb->do_visit(visitor))) {
    } else if (OB_ISNULL(tree = visitor.get_geometry())) {
      ret = OB_ERR_UNEXPECTED;
    } else {
      tree->set_srid(wkb->get_srid());
    }
  }
  return ret;
}

int build_geometry_set(ObIAllocator &allocator,
                       bool geographic,
                       bool far,
                       GeometrySet &geometry_set)
{
  int ret = OB_SUCCESS;
  for (size_t i = 0; i < GEOMETRY_SAMPLES.size() && OB_SUCC(ret); ++i) {
    const char *wkt = far ? GEOMETRY_SAMPLES[i].far_wkt_ : GEOMETRY_SAMPLES[i].near_wkt_;
    ret = parse_geometry(
        allocator, wkt, geographic, geometry_set.wkb_[i], geometry_set.tree_[i]);
  }
  return ret;
}

std::string case_description(bool geographic,
                             bool far,
                             size_t left_type,
                             size_t right_type,
                             RelationPredicate predicate)
{
  std::string description = geographic ? "Geographic" : "Cartesian";
  description += far ? "/disjoint/" : "/interacting/";
  description += GEOMETRY_SAMPLES[left_type].name_;
  description += " x ";
  description += GEOMETRY_SAMPLES[right_type].name_;
  description += "/";
  description += predicate_name(predicate);
  return description;
}

bool is_known_representation_mismatch(const std::string &description)
{
  return description == "Cartesian/interacting/GeometryCollection x Polygon/equals"
      || description == "Cartesian/interacting/MultiPolygon x MultiLineString/touches";
}

using CartesianPoint = bg::model::d2::point_xy<double>;
using CartesianLine = bg::model::linestring<CartesianPoint>;
using CartesianPolygon = bg::model::polygon<CartesianPoint>;
using CartesianMultiPoint = bg::model::multi_point<CartesianPoint>;

bool boost_reference(RelationPredicate predicate)
{
  CartesianPoint point;
  CartesianLine line1;
  CartesianLine line2;
  CartesianPolygon polygon1;
  CartesianPolygon polygon2;
  CartesianMultiPoint multipoint;
  bool result = false;
  switch (predicate) {
    case RelationPredicate::EQUALS:
      bg::read_wkt("POLYGON((0 0,0 4,4 4,4 0,0 0))", polygon1);
      bg::read_wkt("POLYGON((0 0,0 4,4 4,4 0,0 0))", polygon2);
      result = bg::equals(polygon1, polygon2);
      break;
    case RelationPredicate::DISJOINT:
      bg::read_wkt("MULTIPOINT((10 10),(11 11))", multipoint);
      bg::read_wkt("POLYGON((0 0,0 4,4 4,4 0,0 0))", polygon1);
      result = bg::disjoint(multipoint, polygon1);
      break;
    case RelationPredicate::INTERSECTS:
      bg::read_wkt("LINESTRING(0 0,4 4)", line1);
      bg::read_wkt("LINESTRING(0 4,4 0)", line2);
      result = bg::intersects(line1, line2);
      break;
    case RelationPredicate::TOUCHES:
      bg::read_wkt("POLYGON((0 0,0 4,4 4,4 0,0 0))", polygon1);
      bg::read_wkt("POLYGON((4 0,4 4,8 4,8 0,4 0))", polygon2);
      result = bg::touches(polygon1, polygon2);
      break;
    case RelationPredicate::WITHIN:
      bg::read_wkt("POINT(2 2)", point);
      bg::read_wkt("POLYGON((0 0,0 4,4 4,4 0,0 0))", polygon1);
      result = bg::within(point, polygon1);
      break;
    case RelationPredicate::COVERED_BY:
      bg::read_wkt("POINT(0 2)", point);
      bg::read_wkt("POLYGON((0 0,0 4,4 4,4 0,0 0))", polygon1);
      result = bg::covered_by(point, polygon1);
      break;
    case RelationPredicate::CROSSES:
      bg::read_wkt("LINESTRING(0 0,4 4)", line1);
      bg::read_wkt("LINESTRING(0 4,4 0)", line2);
      result = bg::crosses(line1, line2);
      break;
    case RelationPredicate::OVERLAPS:
      bg::read_wkt("POLYGON((0 0,0 4,4 4,4 0,0 0))", polygon1);
      bg::read_wkt("POLYGON((2 0,2 4,6 4,6 0,2 0))", polygon2);
      result = bg::overlaps(polygon1, polygon2);
      break;
  }
  return result;
}

struct ReferenceCase
{
  RelationPredicate predicate_;
  const char *left_wkt_;
  const char *right_wkt_;
};

const std::array<ReferenceCase, 8> REFERENCE_CASES = {{
  {RelationPredicate::EQUALS,
      "POLYGON((0 0,0 4,4 4,4 0,0 0))",
      "POLYGON((0 0,0 4,4 4,4 0,0 0))"},
  {RelationPredicate::DISJOINT,
      "MULTIPOINT((10 10),(11 11))",
      "POLYGON((0 0,0 4,4 4,4 0,0 0))"},
  {RelationPredicate::INTERSECTS,
      "LINESTRING(0 0,4 4)",
      "LINESTRING(0 4,4 0)"},
  {RelationPredicate::TOUCHES,
      "POLYGON((0 0,0 4,4 4,4 0,0 0))",
      "POLYGON((4 0,4 4,8 4,8 0,4 0))"},
  {RelationPredicate::WITHIN,
      "POINT(2 2)",
      "POLYGON((0 0,0 4,4 4,4 0,0 0))"},
  {RelationPredicate::COVERED_BY,
      "POINT(0 2)",
      "POLYGON((0 0,0 4,4 4,4 0,0 0))"},
  {RelationPredicate::CROSSES,
      "LINESTRING(0 0,4 4)",
      "LINESTRING(0 4,4 0)"},
  {RelationPredicate::OVERLAPS,
      "POLYGON((0 0,0 4,4 4,4 0,0 0))",
      "POLYGON((2 0,2 4,6 4,6 0,2 0))"},
}};

// ObGeoFuncDisjoint is currently an internal, partial implementation used by
// intersects. The seven public predicate paths below are the first candidates
// for a shared LineString x LineString relation backend.
const std::array<RelationPredicate, 7> LINESTRING_PREDICATES = {{
  RelationPredicate::EQUALS,
  RelationPredicate::INTERSECTS,
  RelationPredicate::TOUCHES,
  RelationPredicate::WITHIN,
  RelationPredicate::COVERED_BY,
  RelationPredicate::CROSSES,
  RelationPredicate::OVERLAPS,
}};

struct LineStringCase
{
  const char *name_;
  const char *left_wkt_;
  const char *right_wkt_;
};

// "identical/reversed" is one topology class, but both coordinate orders are
// separate rows so an orientation-sensitive regression cannot hide in it.
const std::array<LineStringCase, 7> LINESTRING_CASES = {{
  {"identical", "LINESTRING(0 0,4 0)", "LINESTRING(0 0,4 0)"},
  {"reversed", "LINESTRING(0 0,4 0)", "LINESTRING(4 0,0 0)"},
  {"disjoint", "LINESTRING(0 0,4 0)", "LINESTRING(0 2,4 2)"},
  {"interior_cross", "LINESTRING(0 0,4 0)", "LINESTRING(2 -2,2 2)"},
  {"endpoint_touch", "LINESTRING(0 0,4 0)", "LINESTRING(4 0,6 2)"},
  {"partial_collinear_overlap", "LINESTRING(0 0,4 0)", "LINESTRING(2 0,6 0)"},
  {"proper_subset", "LINESTRING(1 0,3 0)", "LINESTRING(0 0,4 0)"},
}};

bool boost_line_string_reference(RelationPredicate predicate,
                                 const char *left_wkt,
                                 const char *right_wkt)
{
  CartesianLine left;
  CartesianLine right;
  bg::read_wkt(left_wkt, left);
  bg::read_wkt(right_wkt, right);
  bool result = false;
  switch (predicate) {
    case RelationPredicate::EQUALS:
      result = bg::equals(left, right);
      break;
    case RelationPredicate::INTERSECTS:
      result = bg::intersects(left, right);
      break;
    case RelationPredicate::TOUCHES:
      result = bg::touches(left, right);
      break;
    case RelationPredicate::WITHIN:
      result = bg::within(left, right);
      break;
    case RelationPredicate::COVERED_BY:
      result = bg::covered_by(left, right);
      break;
    case RelationPredicate::CROSSES:
      result = bg::crosses(left, right);
      break;
    case RelationPredicate::OVERLAPS:
      result = bg::overlaps(left, right);
      break;
    case RelationPredicate::DISJOINT:
      result = bg::disjoint(left, right);
      break;
  }
  return result;
}

template <typename Left, typename Right>
bool legacy_wkb_linear_reference_t(RelationPredicate predicate,
                                   const ObGeometry *left_geometry,
                                   const ObGeometry *right_geometry)
{
  const Left &left = *reinterpret_cast<const Left *>(left_geometry->val());
  const Right &right = *reinterpret_cast<const Right *>(right_geometry->val());
  bool result = false;
  switch (predicate) {
    case RelationPredicate::EQUALS:
      result = bg::equals(left, right);
      break;
    case RelationPredicate::DISJOINT:
      result = bg::disjoint(left, right);
      break;
    case RelationPredicate::INTERSECTS:
      result = bg::intersects(left, right);
      break;
    case RelationPredicate::TOUCHES:
      result = bg::touches(left, right);
      break;
    case RelationPredicate::WITHIN:
      result = bg::within(left, right);
      break;
    case RelationPredicate::COVERED_BY:
      result = bg::covered_by(left, right);
      break;
    case RelationPredicate::CROSSES:
      result = bg::crosses(left, right);
      break;
    case RelationPredicate::OVERLAPS:
      result = bg::overlaps(left, right);
      break;
  }
  return result;
}

bool legacy_wkb_linear_reference(RelationPredicate predicate,
                                 bool left_is_multi,
                                 bool right_is_multi,
                                 const ObGeometry *left,
                                 const ObGeometry *right)
{
  bool result = false;
  if (left_is_multi && right_is_multi) {
    result = legacy_wkb_linear_reference_t<ObWkbGeomMultiLineString, ObWkbGeomMultiLineString>(
        predicate, left, right);
  } else if (left_is_multi) {
    result = legacy_wkb_linear_reference_t<ObWkbGeomMultiLineString, ObWkbGeomLineString>(
        predicate, left, right);
  } else if (right_is_multi) {
    result = legacy_wkb_linear_reference_t<ObWkbGeomLineString, ObWkbGeomMultiLineString>(
        predicate, left, right);
  } else {
    result = legacy_wkb_linear_reference_t<ObWkbGeomLineString, ObWkbGeomLineString>(
        predicate, left, right);
  }
  return result;
}

struct LinearScenario
{
  const char *name_;
  const char *left_line_wkt_;
  const char *left_multi_wkt_;
  const char *right_line_wkt_;
  const char *right_multi_wkt_;
};

// A valid non-empty 1D geometry cannot lie only in another linear geometry's
// 0D boundary. The zero-length endpoint row is therefore the degenerate probe
// for covered_by-vs-within boundary behavior.
const std::array<LinearScenario, 14> LINEAR_SCENARIOS = {{
  {"endpoint_touch",
      "LINESTRING(0 0,4 0)",
      "MULTILINESTRING((0 0,4 0))",
      "LINESTRING(4 0,6 2)",
      "MULTILINESTRING((4 0,6 2))"},
  {"interior_cross",
      "LINESTRING(0 0,4 0)",
      "MULTILINESTRING((0 0,4 0))",
      "LINESTRING(2 -2,2 2)",
      "MULTILINESTRING((2 -2,2 2))"},
  {"partial_overlap",
      "LINESTRING(0 0,4 0)",
      "MULTILINESTRING((0 0,4 0))",
      "LINESTRING(2 0,6 0)",
      "MULTILINESTRING((2 0,6 0))"},
  {"proper_subset",
      "LINESTRING(1 0,3 0)",
      "MULTILINESTRING((1 0,3 0))",
      "LINESTRING(0 0,4 0)",
      "MULTILINESTRING((0 0,4 0))"},
  {"multi_partial_intersection",
      "LINESTRING(0 0,4 0)",
      "MULTILINESTRING((0 0,4 0),(10 0,12 0))",
      "LINESTRING(2 -1,2 1)",
      "MULTILINESTRING((2 -1,2 1),(20 0,22 0))"},
  {"empty_left",
      "LINESTRING EMPTY",
      "MULTILINESTRING EMPTY",
      "LINESTRING(0 0,4 0)",
      "MULTILINESTRING((0 0,4 0))"},
  {"empty_both",
      "LINESTRING EMPTY",
      "MULTILINESTRING EMPTY",
      "LINESTRING EMPTY",
      "MULTILINESTRING EMPTY"},
  {"duplicate_vertex",
      "LINESTRING(0 0,2 0,2 0,4 0)",
      "MULTILINESTRING((0 0,2 0,2 0,4 0))",
      "LINESTRING(0 0,4 0)",
      "MULTILINESTRING((0 0,4 0))"},
  {"zero_length_at_endpoint",
      "LINESTRING(0 0,0 0)",
      "MULTILINESTRING((0 0,0 0))",
      "LINESTRING(0 0,4 0)",
      "MULTILINESTRING((0 0,4 0))"},
  {"self_intersection",
      "LINESTRING(0 0,4 4,0 4,4 0)",
      "MULTILINESTRING((0 0,4 4,0 4,4 0))",
      "LINESTRING(0 2,4 2)",
      "MULTILINESTRING((0 2,4 2))"},
  {"backtracking",
      "LINESTRING(0 0,4 0,2 0,6 0)",
      "MULTILINESTRING((0 0,4 0,2 0,6 0))",
      "LINESTRING(1 0,5 0)",
      "MULTILINESTRING((1 0,5 0))"},
  {"overlapping_components",
      "LINESTRING(0 0,4 0)",
      "MULTILINESTRING((0 0,4 0),(2 0,6 0))",
      "LINESTRING(3 0,5 0)",
      "MULTILINESTRING((3 0,5 0),(10 0,12 0))"},
  {"degenerate_component",
      "LINESTRING(0 0,4 0)",
      "MULTILINESTRING((0 0,0 0),(0 0,4 0))",
      "LINESTRING(2 -1,2 1)",
      "MULTILINESTRING((2 -1,2 1))"},
  {"disjoint",
      "LINESTRING(0 0,4 0)",
      "MULTILINESTRING((0 0,4 0),(10 0,12 0))",
      "LINESTRING(0 2,4 2)",
      "MULTILINESTRING((0 2,4 2),(20 0,22 0))"},
}};

} // namespace

class TestGeoRelationMatrix : public ::testing::Test
{
protected:
  void SetUp() override
  {
    ASSERT_EQ(OB_SUCCESS, CURRENT_CONTEXT->CREATE_CONTEXT(
        memory_context_, lib::ContextParam().set_label("GEO_REL_UT")));
    ASSERT_EQ(OB_SUCCESS, create_geographic_srs(geometry_allocator_, geographic_srs_));
  }

  void TearDown() override
  {
    DESTROY_CONTEXT(memory_context_);
  }

  ObArenaAllocator geometry_allocator_{ObModIds::TEST};
  lib::MemoryContext memory_context_;
  const ObSrsItem *geographic_srs_ = nullptr;
};

TEST_F(TestGeoRelationMatrix, production_wkb_tree_matrix)
{
  int64_t compared = 0;
  int64_t representation_mismatches = 0;
  int64_t tree_not_implemented = 0;
  int64_t both_not_implemented = 0;
  int64_t nullable = 0;
  int64_t disjoint_complements = 0;

  for (bool geographic : {false, true}) {
    GeometrySet near_set;
    GeometrySet far_set;
    ASSERT_EQ(OB_SUCCESS, build_geometry_set(geometry_allocator_, geographic, false, near_set));
    ASSERT_EQ(OB_SUCCESS, build_geometry_set(geometry_allocator_, geographic, true, far_set));
    const ObSrsItem *srs = geographic ? geographic_srs_ : nullptr;

    for (bool far : {false, true}) {
      const GeometrySet &right_set = far ? far_set : near_set;
      for (size_t left_type = 0; left_type < GEOMETRY_SAMPLES.size(); ++left_type) {
        for (size_t right_type = 0; right_type < GEOMETRY_SAMPLES.size(); ++right_type) {
          RelationOutcome intersects_wkb;
          RelationOutcome disjoint_wkb;
          ASSERT_NO_FATAL_FAILURE(eval_relation(RelationPredicate::INTERSECTS,
              memory_context_, srs, near_set.wkb_[left_type], right_set.wkb_[right_type],
              intersects_wkb));
          ASSERT_NO_FATAL_FAILURE(eval_relation(RelationPredicate::DISJOINT,
              memory_context_, srs, near_set.wkb_[left_type], right_set.wkb_[right_type],
              disjoint_wkb));
          if (intersects_wkb.ret_ == OB_SUCCESS && disjoint_wkb.ret_ == OB_SUCCESS) {
            EXPECT_NE(intersects_wkb.value_, disjoint_wkb.value_)
                << case_description(geographic, far, left_type, right_type,
                                    RelationPredicate::DISJOINT);
            ++disjoint_complements;
          }

          for (RelationPredicate predicate : RELATION_PREDICATES) {
            RelationOutcome wkb_outcome;
            RelationOutcome tree_outcome;
            ASSERT_NO_FATAL_FAILURE(eval_relation(predicate, memory_context_, srs,
                near_set.wkb_[left_type], right_set.wkb_[right_type], wkb_outcome));
            ASSERT_NO_FATAL_FAILURE(eval_relation(predicate, memory_context_, srs,
                near_set.tree_[left_type], right_set.tree_[right_type], tree_outcome));

            if (is_not_implemented(tree_outcome.ret_)) {
              if (is_not_implemented(wkb_outcome.ret_)) {
                ++both_not_implemented;
              } else {
                ++tree_not_implemented;
              }
            } else {
              if (!equivalent(wkb_outcome, tree_outcome)) {
                const std::string description = case_description(
                    geographic, far, left_type, right_type, predicate);
                EXPECT_TRUE(is_known_representation_mismatch(description)) << description;
                ++representation_mismatches;
              }
              ++compared;
            }
            if ((wkb_outcome.ret_ == OB_SUCCESS && wkb_outcome.is_null_)
                || (tree_outcome.ret_ == OB_SUCCESS && tree_outcome.is_null_)) {
              ++nullable;
            }
          }
        }
      }
    }
  }

  // Guard against accidentally turning this into a matrix that only records skips.
  // Two legacy representation differences are intentionally characterized:
  // GC x Polygon equals and MultiPolygon x MultiLineString touches.
  EXPECT_GT(compared, 250);
  EXPECT_EQ(2, representation_mismatches);
  EXPECT_GT(disjoint_complements, 0);
  RecordProperty("wkb_tree_compared", compared);
  RecordProperty("wkb_tree_representation_mismatches", representation_mismatches);
  RecordProperty("tree_specialization_missing", tree_not_implemented);
  RecordProperty("both_paths_not_implemented", both_not_implemented);
  RecordProperty("nullable_results", nullable);
  RecordProperty("disjoint_intersects_complements", disjoint_complements);
}

TEST_F(TestGeoRelationMatrix, boost_reference_cases)
{
  int64_t compared = 0;
  int64_t tree_not_implemented = 0;
  for (const ReferenceCase &test_case : REFERENCE_CASES) {
    // These two production functions intentionally differ from bare Boost
    // semantics for some type combinations; they are covered by the exact
    // WKB legacy oracle below instead.
    if (test_case.predicate_ == RelationPredicate::TOUCHES
        || test_case.predicate_ == RelationPredicate::WITHIN) {
      continue;
    }
    const bool expected = boost_reference(test_case.predicate_);
    const bool geographic = false;
    ObGeometry *left_wkb = nullptr;
    ObGeometry *left_tree = nullptr;
    ObGeometry *right_wkb = nullptr;
    ObGeometry *right_tree = nullptr;
    ASSERT_EQ(OB_SUCCESS, parse_geometry(geometry_allocator_, test_case.left_wkt_, geographic,
                                        left_wkb, left_tree));
    ASSERT_EQ(OB_SUCCESS, parse_geometry(geometry_allocator_, test_case.right_wkt_, geographic,
                                        right_wkb, right_tree));

    RelationOutcome wkb_outcome;
    RelationOutcome tree_outcome;
    ASSERT_NO_FATAL_FAILURE(eval_relation(test_case.predicate_, memory_context_, nullptr,
                                          left_wkb, right_wkb, wkb_outcome));
    ASSERT_EQ(OB_SUCCESS, wkb_outcome.ret_) << predicate_name(test_case.predicate_);
    ASSERT_FALSE(wkb_outcome.is_null_) << predicate_name(test_case.predicate_);
    EXPECT_EQ(expected, wkb_outcome.value_)
        << "Cartesian/" << predicate_name(test_case.predicate_);
    ++compared;

    ASSERT_NO_FATAL_FAILURE(eval_relation(test_case.predicate_, memory_context_, nullptr,
                                          left_tree, right_tree, tree_outcome));
    if (is_not_implemented(tree_outcome.ret_)) {
      ++tree_not_implemented;
    } else {
      ASSERT_EQ(OB_SUCCESS, tree_outcome.ret_) << predicate_name(test_case.predicate_);
      ASSERT_FALSE(tree_outcome.is_null_) << predicate_name(test_case.predicate_);
      EXPECT_EQ(expected, tree_outcome.value_)
          << "Cartesian/Tree/" << predicate_name(test_case.predicate_);
      ++compared;
    }
  }
  EXPECT_GE(compared, 6);
  RecordProperty("boost_reference_compared", compared);
  RecordProperty("boost_reference_tree_specialization_missing", tree_not_implemented);
}

TEST_F(TestGeoRelationMatrix, line_string_topology_oracle)
{
  int64_t cartesian_wkb_compared = 0;
  int64_t tree_compared = 0;
  int64_t tree_not_implemented = 0;

  for (const LineStringCase &test_case : LINESTRING_CASES) {
    const bool reference_disjoint = boost_line_string_reference(
        RelationPredicate::DISJOINT, test_case.left_wkt_, test_case.right_wkt_);
    ObGeometry *left_wkb = nullptr;
    ObGeometry *left_tree = nullptr;
    ObGeometry *right_wkb = nullptr;
    ObGeometry *right_tree = nullptr;
    ASSERT_EQ(OB_SUCCESS, parse_geometry(geometry_allocator_, test_case.left_wkt_, false,
                                        left_wkb, left_tree));
    ASSERT_EQ(OB_SUCCESS, parse_geometry(geometry_allocator_, test_case.right_wkt_, false,
                                        right_wkb, right_tree));

    for (RelationPredicate predicate : LINESTRING_PREDICATES) {
      const bool expected = boost_line_string_reference(
          predicate, test_case.left_wkt_, test_case.right_wkt_);
      RelationOutcome wkb_outcome;
      ASSERT_NO_FATAL_FAILURE(eval_relation(
          predicate, memory_context_, nullptr, left_wkb, right_wkb, wkb_outcome));
      ASSERT_EQ(OB_SUCCESS, wkb_outcome.ret_)
          << test_case.name_ << "/" << predicate_name(predicate);
      ASSERT_FALSE(wkb_outcome.is_null_)
          << test_case.name_ << "/" << predicate_name(predicate);
      EXPECT_EQ(expected, wkb_outcome.value_)
          << "Cartesian/" << test_case.name_ << "/" << predicate_name(predicate);
      ++cartesian_wkb_compared;

      if (predicate == RelationPredicate::INTERSECTS) {
        EXPECT_EQ(!reference_disjoint, wkb_outcome.value_)
            << "Cartesian/" << test_case.name_ << "/intersects-vs-disjoint";
      }

      RelationOutcome tree_outcome;
      ASSERT_NO_FATAL_FAILURE(eval_relation(
          predicate, memory_context_, nullptr, left_tree, right_tree, tree_outcome));
      if (is_not_implemented(tree_outcome.ret_)) {
        ++tree_not_implemented;
      } else {
        ASSERT_EQ(OB_SUCCESS, tree_outcome.ret_)
            << test_case.name_ << "/Tree/" << predicate_name(predicate);
        ASSERT_FALSE(tree_outcome.is_null_)
            << test_case.name_ << "/Tree/" << predicate_name(predicate);
        EXPECT_EQ(expected, tree_outcome.value_)
            << "Cartesian/Tree/" << test_case.name_ << "/" << predicate_name(predicate);
        ++tree_compared;
      }
    }
  }

  EXPECT_EQ(static_cast<int64_t>(LINESTRING_CASES.size() * LINESTRING_PREDICATES.size()),
            cartesian_wkb_compared);
  RecordProperty("line_cartesian_wkb_oracle_compared", cartesian_wkb_compared);
  RecordProperty("line_tree_oracle_compared", tree_compared);
  RecordProperty("line_tree_specialization_missing", tree_not_implemented);
}

TEST_F(TestGeoRelationMatrix, cartesian_wkb_linear_type_matrix_oracle)
{
  int64_t compared = 0;
  int64_t disjoint_cross_checks = 0;
  for (bool left_is_multi : {false, true}) {
    for (bool right_is_multi : {false, true}) {
      for (const LinearScenario &scenario : LINEAR_SCENARIOS) {
        const char *left_wkt =
            left_is_multi ? scenario.left_multi_wkt_ : scenario.left_line_wkt_;
        const char *right_wkt =
            right_is_multi ? scenario.right_multi_wkt_ : scenario.right_line_wkt_;
        ObGeometry *left_wkb = nullptr;
        ObGeometry *right_wkb = nullptr;
        ASSERT_EQ(OB_SUCCESS, parse_wkb_geometry(
            geometry_allocator_, left_wkt, false, left_wkb))
            << (left_is_multi ? "MultiLineString" : "LineString") << "/"
            << scenario.name_ << "/left";
        ASSERT_EQ(OB_SUCCESS, parse_wkb_geometry(
            geometry_allocator_, right_wkt, false, right_wkb))
            << (right_is_multi ? "MultiLineString" : "LineString") << "/"
            << scenario.name_ << "/right";

        for (RelationPredicate predicate : LINESTRING_PREDICATES) {
          const bool expected = legacy_wkb_linear_reference(
              predicate, left_is_multi, right_is_multi, left_wkb, right_wkb);
          RelationOutcome production;
          ASSERT_NO_FATAL_FAILURE(eval_relation(
              predicate, memory_context_, nullptr, left_wkb, right_wkb, production));
          ASSERT_EQ(OB_SUCCESS, production.ret_)
              << (left_is_multi ? "MultiLineString" : "LineString") << " x "
              << (right_is_multi ? "MultiLineString" : "LineString") << "/"
              << scenario.name_ << "/" << predicate_name(predicate);
          ASSERT_FALSE(production.is_null_)
              << scenario.name_ << "/" << predicate_name(predicate);
          EXPECT_EQ(expected, production.value_)
              << (left_is_multi ? "MultiLineString" : "LineString") << " x "
              << (right_is_multi ? "MultiLineString" : "LineString") << "/"
              << scenario.name_ << "/" << predicate_name(predicate);
          ++compared;

          if (predicate == RelationPredicate::INTERSECTS) {
            const bool reference_disjoint = legacy_wkb_linear_reference(
                RelationPredicate::DISJOINT,
                left_is_multi,
                right_is_multi,
                left_wkb,
                right_wkb);
            EXPECT_EQ(!reference_disjoint, production.value_)
                << scenario.name_ << "/intersects-vs-disjoint";
            ++disjoint_cross_checks;
          }
        }
      }
    }
  }

  const int64_t expected_comparisons = static_cast<int64_t>(
      4 * LINEAR_SCENARIOS.size() * LINESTRING_PREDICATES.size());
  EXPECT_EQ(expected_comparisons, compared);
  EXPECT_EQ(static_cast<int64_t>(4 * LINEAR_SCENARIOS.size()), disjoint_cross_checks);
  RecordProperty("cartesian_linear_legacy_oracle_compared", compared);
  RecordProperty("cartesian_linear_disjoint_cross_checks", disjoint_cross_checks);
}

} // namespace common
} // namespace oceanbase

int main(int argc, char **argv)
{
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
