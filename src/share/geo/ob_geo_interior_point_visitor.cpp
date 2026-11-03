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
#include "ob_geo_interior_point_visitor.h"
#include "share/geo/ob_geo_func_register.h"
#include "seekdb/geo/interior_point.hpp"

namespace oceanbase
{
namespace common
{
int ObGeoInteriorPointVisitor::init(ObGeometry *geo)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(ObGeoTypeUtil::check_empty(geo, is_geo_empty_))) {
  } else {
    ObGeoEvalCtx centroid_context(mem_ctx_);
    ObGeometry *res_geo = nullptr;
    if (OB_FAIL(centroid_context.append_geo_arg(geo))) {
    } else if (OB_FAIL(ObGeoFunc<ObGeoFuncType::Centroid>::geo_func::eval(centroid_context, res_geo))) {
      if (ret == OB_ERR_BOOST_GEOMETRY_CENTROID_EXCEPTION) {
        exist_centroid_ = false;
        ret = OB_SUCCESS;
      } else {
      }
    } else {
      centroid_pt_ = reinterpret_cast<ObCartesianPoint *>(res_geo);
    }
  }

  if (OB_SUCC(ret)) {
    is_inited_ = true;
  }
  return ret;
}

// support ObWkbGeomPoint/ObWkbGeomInnerPoint
template<typename PointType>
double ObGeoInteriorPointVisitor::calculate_euclidean_distance(ObCartesianPoint &p1, PointType &p2)
{
  return seekdb::geo::cartesian::surface_distance(p2.template get<0>(), p2.template get<1>(), p1.x(), p1.y());
}

int ObGeoInteriorPointVisitor::assign_interior_point(double x, double y)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(interior_point_)) {
    if (OB_ISNULL(interior_point_ = OB_NEWx(ObCartesianPoint, allocator_, x, y, srid_))) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
    }
  } else {
    interior_point_->x(x);
    interior_point_->y(y);
  }

  return ret;
}

int ObGeoInteriorPointVisitor::assign_interior_endpoint(double x, double y)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(interior_endpoint_)) {
    if (OB_ISNULL(interior_endpoint_ = OB_NEWx(ObCartesianPoint, allocator_, x, y, srid_))) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
    }
  } else {
    interior_endpoint_->x(x);
    interior_endpoint_->y(y);
  }

  return ret;
}

int ObGeoInteriorPointVisitor::visit(ObIWkbGeomPoint *geo)
{
  int ret = OB_SUCCESS;
  if (!is_inited_) {
    if (OB_FAIL(init(geo))) {
    }
  }

  if (OB_SUCC(ret) && !is_geo_empty_ && exist_centroid_ && (dimension_ == -1 || dimension_ == 0)) {
    ObWkbGeomPoint *point = reinterpret_cast<ObWkbGeomPoint *>(geo->val());
    double dist = calculate_euclidean_distance(*centroid_pt_, *point);
    if (dist < min_dist_) {
      min_dist_ = dist;
      if (OB_FAIL(assign_interior_point(geo->x(), geo->y()))) {
      }
    }
  }
  return ret;
}

int ObGeoInteriorPointVisitor::visit(ObIWkbGeomMultiPoint *geo)
{
  int ret = OB_SUCCESS;
  if (!is_inited_) {
    if (OB_FAIL(init(geo))) {
    }
  }

  if (OB_SUCC(ret) && !is_geo_empty_ && exist_centroid_) {
    const ObWkbGeomMultiPoint *line = reinterpret_cast<const ObWkbGeomMultiPoint *>(geo->val());
    ObWkbGeomMultiPoint::iterator iter = line->begin();
    for (; iter != line->end() && OB_SUCC(ret); iter++) {
      double dist = calculate_euclidean_distance(*centroid_pt_, *iter);
      if (dist < min_dist_) {
        min_dist_ = dist;
        if (OB_FAIL(assign_interior_point(iter->get<0>(), iter->get<1>()))) {
        }
      }
    }
  }
  return ret;
}

int ObGeoInteriorPointVisitor::visit(ObIWkbGeomLineString *geo)
{
  int ret = OB_SUCCESS;
  if (!is_inited_) {
    if (OB_FAIL(init(geo))) {
    }
  }
  if (OB_SUCC(ret) && !is_geo_empty_) {
    const ObWkbGeomLineString *line = reinterpret_cast<const ObWkbGeomLineString *>(geo->val());
    // No centroid: the visitor's documented fallback origin must not
    // dereference a null centroid pointer.
    const double cx = centroid_pt_ == nullptr ? 0 : centroid_pt_->x();
    const double cy = centroid_pt_ == nullptr ? 0 : centroid_pt_->y();
    const auto vertex = [&](double x, double y) {
      const double dist = seekdb::geo::cartesian::surface_distance(x, y, cx, cy);
      if (dist < min_dist_) {
        min_dist_ = dist;
        ret = assign_interior_point(x, y);
      }
      return OB_SUCC(ret);
    };
    const auto endpoint = [&](double x, double y) {
      if (OB_ISNULL(interior_point_)) {
        const double dist = seekdb::geo::cartesian::surface_distance(x, y, cx, cy);
        if (dist < min_endpoint_dist_) {
          min_endpoint_dist_ = dist;
          ret = assign_interior_endpoint(x, y);
        }
      }
      return OB_SUCC(ret);
    };
    seekdb::geo::cartesian::surface_line_candidates(*line, exist_centroid_, vertex, endpoint);
  }
  return ret;
}

int ObGeoInteriorPointVisitor::visit(ObIWkbGeomMultiLineString *geo)
{
  int ret = OB_SUCCESS;
  // unused
  if (!is_inited_) {
    if (OB_FAIL(init(geo))) {
    }
  }

  return ret;
}
int ObGeoInteriorPointVisitor::calculate_interior_y(ObIWkbGeomPolygon *geo, double &interior_y)
{
  const ObWkbGeomPolygon *polygon = reinterpret_cast<const ObWkbGeomPolygon *>(geo->val());
  return seekdb::geo::cartesian::surface_scanline_y(
      polygon->exterior_ring(), polygon->inner_rings(), interior_y) ? OB_SUCCESS : OB_ERR_GIS_INVALID_DATA;
}
int ObGeoInteriorPointVisitor::inner_calculate_crossing_points(
    const ObWkbGeomLinearRing &ring, double interior_y, ObArray<double> &crossing_points_x)
{
  int ret = OB_SUCCESS;
  const auto append = [&](double x) {
    ret = crossing_points_x.push_back(x);
    return OB_SUCC(ret);
  };
  if (!seekdb::geo::cartesian::surface_ring_crossings(ring, interior_y, append) && OB_SUCC(ret)) {
    ret = OB_ERR_GIS_INVALID_DATA;
  }
  return ret;
}

int ObGeoInteriorPointVisitor::calculate_crossing_points(
    ObIWkbGeomPolygon *geo, double interior_y, ObArray<double> &crossing_points_x)
{
  int ret = OB_SUCCESS;
  const ObWkbGeomPolygon *polygon = reinterpret_cast<const ObWkbGeomPolygon *>(geo->val());
  if (OB_FAIL(inner_calculate_crossing_points(polygon->exterior_ring(), interior_y, crossing_points_x))) {
  } else {
    const ObWkbGeomPolygonInnerRings &rings = polygon->inner_rings();
    ObWkbGeomPolygonInnerRings::const_iterator iter = rings.begin();
    for (; iter != rings.end() && OB_SUCC(ret); iter++) {
      if (OB_FAIL(inner_calculate_crossing_points(*iter, interior_y, crossing_points_x))) {
      }
    }
  }
  return ret;
}

int ObGeoInteriorPointVisitor::visit(ObIWkbGeomPolygon *geo)
{
  int ret = OB_SUCCESS;
  if (!is_inited_) {
    if (OB_FAIL(ObGeoTypeUtil::check_empty(geo, is_geo_empty_))) {
    } else {
      is_inited_ = true;
    }
  }

  if (OB_SUCC(ret) && !is_geo_empty_) {
    double interior_y = 0;
    ObArray<double> crossing_points;
    if (OB_FAIL(calculate_interior_y(geo, interior_y))) {
    } else if (OB_FAIL(calculate_crossing_points(geo, interior_y, crossing_points))) {
    } else if (crossing_points.size() % 2) {
      ret = OB_ERR_GIS_INVALID_DATA;
    } else {
      double interior_x = 0;
      lib::ob_sort(crossing_points.begin(), crossing_points.end());
      if (seekdb::geo::cartesian::surface_widest_interval(
          crossing_points.begin(), crossing_points.end(), max_width_, interior_x)) {
        ret = assign_interior_point(interior_x, interior_y);
      }

      if (OB_SUCC(ret) && (max_width_ == -1)) {
        // set default interior point, in case polygon has zero area
        const ObWkbGeomPolygon *polygon = reinterpret_cast<const ObWkbGeomPolygon *>(geo->val());
        const ObWkbGeomInnerPoint &first_point = *polygon->exterior_ring().begin();
        if (OB_FAIL(assign_interior_point(first_point.get<0>(), first_point.get<1>()))) {
        } else {
          max_width_ = 0.0;
        }
      }
    }
  }
  return ret;
}

int ObGeoInteriorPointVisitor::visit(ObIWkbGeomMultiPolygon *geo)
{
  int ret = OB_SUCCESS;
  if (!is_inited_) {
    if (OB_FAIL(ObGeoTypeUtil::check_empty(geo, is_geo_empty_))) {
    } else {
      is_inited_ = true;
    }
  }
  return ret;
}

int ObGeoInteriorPointVisitor::visit(ObIWkbGeomCollection *geo)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(ObGeoTypeUtil::check_empty(geo, is_geo_empty_))) {
  } else if (!is_geo_empty_) {
    if (OB_FAIL(ObGeoTypeUtil::get_coll_dimension(geo, dimension_))) {
    } else if (dimension_ == 0 || dimension_ == 1) {
      ObGeoEvalCtx centroid_context(mem_ctx_);
      ObGeometry *res_geo = nullptr;
      if (OB_FAIL(centroid_context.append_geo_arg(geo))) {
      } else if (OB_FAIL(ObGeoFunc<ObGeoFuncType::Centroid>::geo_func::eval(centroid_context, res_geo))) {
        if (ret == OB_ERR_BOOST_GEOMETRY_CENTROID_EXCEPTION) {
          exist_centroid_ = false;
          ret = OB_SUCCESS;
        } else {
        }
      } else {
        centroid_pt_ = reinterpret_cast<ObCartesianPoint *>(res_geo);
      }
      if (OB_SUCC(ret)) {
        is_inited_ = true;
      }
    }
  }
  return ret;
}

int ObGeoInteriorPointVisitor::get_interior_point(ObGeometry *&interior_point)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(interior_point_)) {
    if (OB_ISNULL(interior_endpoint_)) {
      // return ObCartesianGeometrycollection EMPTY
      if (OB_ISNULL(interior_point = OB_NEWx(ObCartesianGeometrycollection, allocator_, srid_, *allocator_))) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
      }
    } else {
      interior_point = interior_endpoint_;
    }
  } else {
    interior_point = interior_point_;
  }
  return ret;
}

}  // namespace common
}  // namespace oceanbase
