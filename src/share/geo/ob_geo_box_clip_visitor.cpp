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
#include "ob_geo_box_clip_visitor.h"
#include "share/geo/ob_geo_func_register.h"
#include "seekdb/geo/box_clip.hpp"

namespace oceanbase
{
namespace common
{
namespace {
// Only ownership and existing predicate dispatch live here. The algorithm is
// also instantiated by the plugin without any of these server types.
struct CoreClipFactory {
  using Point = ObWkbGeomInnerPoint;
  using Line = ObCartesianLineString;
  using Ring = ObCartesianLinearring;
  using Polygon = ObCartesianPolygon;
  using Lines = ObCartesianMultilinestring;
  using Polygons = ObCartesianMultipolygon;
  static constexpr int invalid_argument = OB_INVALID_ARGUMENT;
  static constexpr int invalid_geometry = OB_ERR_GIS_INVALID_DATA;
  uint32_t srid;
  ObIAllocator &allocator;
  lib::MemoryContext &context;

  Line line() const { return Line(srid, allocator); }
  Ring ring() const { return Ring(srid, allocator); }
  Polygon polygon() const { return Polygon(srid, allocator); }
  Lines lines() const { return Lines(srid, allocator); }
  Polygons polygons() const { return Polygons(srid, allocator); }
  int erase(Lines &value, size_t index) const { return value.remove(index); }
  int within(const Point &point, const Polygon &polygon, bool &result) const
  {
    int ret = OB_SUCCESS;
    ObCartesianPoint value(point.get<0>(), point.get<1>(), srid);
    ObGeoEvalCtx ctx(context);
    if (OB_FAIL(ctx.append_geo_arg(&value))) {
    } else if (OB_FAIL(ctx.append_geo_arg(&polygon))) {
    } else if (OB_FAIL(ObGeoFunc<ObGeoFuncType::Within>::gis_func::eval(ctx, result))) {
    }
    return ret;
  }
  int covered_by(const Ring &ring, const Polygon &polygon, bool &result) const
  {
    int ret = OB_SUCCESS;
    const Line *line = &ring;
    ObGeoEvalCtx ctx(context);
    if (OB_FAIL(ctx.append_geo_arg(line))) {
    } else if (OB_FAIL(ctx.append_geo_arg(&polygon))) {
    } else if (OB_FAIL(ObGeoFunc<ObGeoFuncType::CoveredBy>::geo_func::eval(ctx, result))) {
    }
    return ret;
  }
};
}

ObBoxPosition ObGeoBoxClipVisitor::get_position(double x, double y)
{
  const seekdb::geo::cartesian::ClipBox box{xmin_, ymin_, xmax_, ymax_};
  return static_cast<ObBoxPosition>(box.position(x, y));
}

ObGeoType ObGeoBoxClipVisitor::get_result_basic_type()
{
  ObGeoType type = ObGeoType::GEOMETRYCOLLECTION;
  if (!res_geo_->empty()) {
    type = res_geo_->front().type();
    for (uint32_t i = 1; i < res_geo_->size() && type != ObGeoType::GEOMETRYCOLLECTION; ++i) {
      if (type != (*res_geo_)[i].type()) type = ObGeoType::GEOMETRYCOLLECTION;
    }
  }
  return type;
}

int ObGeoBoxClipVisitor::get_geometry(ObGeometry *&geo)
{
  int ret = OB_SUCCESS;
  const ObGeoType type = get_result_basic_type();
  geo = nullptr;
  if (res_geo_->size() == 1) geo = &res_geo_->front();
  else if (type == ObGeoType::GEOMETRYCOLLECTION) geo = res_geo_;
  else if (type == ObGeoType::POINT) {
    auto *points = OB_NEWx(ObCartesianMultipoint, allocator_, res_geo_->get_srid(), *allocator_);
    if (OB_ISNULL(points)) ret = OB_ALLOCATE_MEMORY_FAILED;
    for (uint32_t i = 0; OB_SUCC(ret) && i < res_geo_->size(); ++i) {
      ret = points->push_back(*reinterpret_cast<const ObWkbGeomInnerPoint *>((*res_geo_)[i].val()));
    }
    if (OB_SUCC(ret)) geo = points;
  } else if (type == ObGeoType::POLYGON) {
    auto *polygons = OB_NEWx(ObCartesianMultipolygon, allocator_, res_geo_->get_srid(), *allocator_);
    if (OB_ISNULL(polygons)) ret = OB_ALLOCATE_MEMORY_FAILED;
    for (uint32_t i = 0; OB_SUCC(ret) && i < res_geo_->size(); ++i) {
      ret = polygons->push_back(static_cast<const ObCartesianPolygon &>((*res_geo_)[i]));
    }
    if (OB_SUCC(ret)) geo = polygons;
  } else if (type == ObGeoType::LINESTRING) {
    auto *lines = OB_NEWx(ObCartesianMultilinestring, allocator_, res_geo_->get_srid(), *allocator_);
    if (OB_ISNULL(lines)) ret = OB_ALLOCATE_MEMORY_FAILED;
    for (uint32_t i = 0; OB_SUCC(ret) && i < res_geo_->size(); ++i) {
      ret = lines->push_back(static_cast<const ObCartesianLineString &>((*res_geo_)[i]));
    }
    if (OB_SUCC(ret)) geo = lines;
  }
  return ret;
}

bool ObGeoBoxClipVisitor::prepare(ObGeometry *geo)
{
  if (OB_ISNULL(geo)) return false;
  if (OB_ISNULL(res_geo_)) res_geo_ = OB_NEWx(ObCartesianGeometrycollection, allocator_, geo->get_srid(), *allocator_);
  return OB_NOT_NULL(res_geo_);
}

int ObGeoBoxClipVisitor::visit(ObCartesianPoint *geo)
{
  return !geo->is_empty() && get_position(geo->x(), geo->y()) == ObBoxPosition::INSIDE
      ? res_geo_->push_back(*geo) : OB_SUCCESS;
}

int ObGeoBoxClipVisitor::visit(ObCartesianMultipoint *geo)
{
  int ret = OB_SUCCESS;
  for (uint32_t i = 0; OB_SUCC(ret) && i < geo->size(); ++i) {
    const auto &point = (*geo)[i];
    if (get_position(point.get<0>(), point.get<1>()) == ObBoxPosition::INSIDE) {
      auto *value = OB_NEWx(ObCartesianPoint, allocator_, point.get<0>(), point.get<1>(), geo->get_srid());
      if (OB_ISNULL(value)) ret = OB_ALLOCATE_MEMORY_FAILED;
      else ret = res_geo_->push_back(*value);
    }
  }
  return ret;
}

int ObGeoBoxClipVisitor::line_visit(const ObCartesianLineString &line,
    ObCartesianMultilinestring *&lines, bool &inside)
{
  if (OB_ISNULL(lines)) lines = OB_NEWx(ObCartesianMultilinestring, allocator_, line.get_srid(), *allocator_);
  if (OB_ISNULL(lines)) return OB_ALLOCATE_MEMORY_FAILED;
  CoreClipFactory factory{line.get_srid(), *allocator_, *mem_ctx_};
  seekdb::geo::cartesian::BoxClipper<CoreClipFactory> clipper({xmin_, ymin_, xmax_, ymax_}, factory);
  return clipper.clip_line(line, *lines, inside);
}

int ObGeoBoxClipVisitor::visit(ObCartesianLineString *geo)
{
  int ret = OB_SUCCESS;
  ObCartesianMultilinestring *lines = nullptr;
  bool inside = false;
  if (!geo->is_empty()) {
    if (OB_FAIL(line_visit(*geo, lines, inside))) {
    } else for (uint32_t i = 0; OB_SUCC(ret) && i < lines->size(); ++i) ret = res_geo_->push_back((*lines)[i]);
  }
  return ret;
}

int ObGeoBoxClipVisitor::visit(ObCartesianMultilinestring *geo)
{
  int ret = OB_SUCCESS;
  ObCartesianMultilinestring *lines = nullptr;
  bool inside = false;
  for (uint32_t i = 0; OB_SUCC(ret) && i < geo->size(); ++i) ret = line_visit((*geo)[i], lines, inside);
  if (OB_NOT_NULL(lines)) {
    for (uint32_t i = 0; OB_SUCC(ret) && i < lines->size(); ++i) ret = res_geo_->push_back((*lines)[i]);
  }
  return ret;
}

int ObGeoBoxClipVisitor::visit_polygon(ObCartesianPolygon &polygon, ObCartesianMultipolygon *&polygons)
{
  if (OB_ISNULL(polygons)) polygons = OB_NEWx(ObCartesianMultipolygon, allocator_, polygon.get_srid(), *allocator_);
  if (OB_ISNULL(polygons)) return OB_ALLOCATE_MEMORY_FAILED;
  CoreClipFactory factory{polygon.get_srid(), *allocator_, *mem_ctx_};
  seekdb::geo::cartesian::BoxClipper<CoreClipFactory> clipper({xmin_, ymin_, xmax_, ymax_}, factory);
  return clipper.clip_polygon(polygon, *polygons);
}

int ObGeoBoxClipVisitor::visit(ObCartesianPolygon *geo)
{
  int ret = OB_SUCCESS;
  ObCartesianMultipolygon *polygons = nullptr;
  if (!geo->is_empty()) {
    if (OB_FAIL(visit_polygon(*geo, polygons))) {
    } else for (uint32_t i = 0; OB_SUCC(ret) && i < polygons->size(); ++i) ret = res_geo_->push_back((*polygons)[i]);
  }
  return ret;
}

int ObGeoBoxClipVisitor::visit(ObCartesianMultipolygon *geo)
{
  int ret = OB_SUCCESS;
  ObCartesianMultipolygon *polygons = nullptr;
  for (uint32_t i = 0; OB_SUCC(ret) && i < geo->size(); ++i) ret = visit_polygon((*geo)[i], polygons);
  if (OB_NOT_NULL(polygons)) {
    for (uint32_t i = 0; OB_SUCC(ret) && i < polygons->size(); ++i) ret = res_geo_->push_back((*polygons)[i]);
  }
  return ret;
}
} // namespace common
} // namespace oceanbase
