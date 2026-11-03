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
#include "ob_geo_to_s2_visitor.h"
#include "seekdb/geo/s2_covering.hpp"
#include "share/geo/ob_geo_dispatcher.h"

namespace oceanbase {
namespace common {

bool ObWkbToS2Visitor::prepare(ObGeometry *geo)
{
  bool bret = true;
  if (OB_ISNULL(geo)) {
    bret = false;
  }
  return bret;
}

int ObWkbToS2Visitor::add_cell_from_point(S2Point point)
{
  int ret = OB_SUCCESS;
  S2CellId cell_id = S2CellId(point).parent(options_.max_level());
  if (OB_FAIL(vector_push_back<S2CellId>(S2cells_, cell_id))) {
  }
  return ret;
}

int ObWkbToS2Visitor::add_cell_from_point(S2LatLng point)
{
  int ret = OB_SUCCESS;
  S2CellId cell_id = S2CellId(point).parent(options_.max_level());
  if (OB_FAIL(vector_push_back<S2CellId>(S2cells_, cell_id))) {
  }
  return ret;
}

template<typename T_IBIN>
int ObWkbToS2Visitor::MakeS2Point(T_IBIN *geo, S2Cell *&res)
{
  int ret = OB_SUCCESS;
  S2LatLng latlng = S2LatLng::FromDegrees(geo->y(), geo->x());
  if (OB_FAIL(add_cell_from_point(latlng))) {
  } else {
    mbr_ = mbr_.is_empty() ? S2LatLngRect(latlng, latlng) : mbr_.Union(S2LatLngRect(latlng, latlng));
    S2Cell* p = new S2Cell(latlng);
    if (OB_ISNULL(p)) {
      ret = OB_ERR_UNEXPECTED;
    } else {
      res = p;
      bounder_.AddPoint(S2Point(latlng));
    }
  }
  return ret;
}

double ObWkbToS2Visitor::stToUV(double s)
{
  return seekdb::geo::s2_index::st_to_uv(s);
}

bool ObWkbToS2Visitor::exceedsBounds(double x, double y)
{
  static_assert(OB_GEO_BOUNDS_DELTA == seekdb::geo::s2_index::bounds_margin);
  return bound_ == nullptr || seekdb::geo::s2_index::outside_bounds(
      {bound_->minX_, bound_->maxX_, bound_->minY_, bound_->maxY_}, x, y);
}

S2Point ObWkbToS2Visitor::MakeS2PointFromXy(double x, double y)
{
  S2Point ret{-1, -1, -1};
  if (!exceedsBounds(x, y)) {
    double s = (x - bound_->minX_) / (bound_->maxX_ - bound_->minX_);
    double t = (y - bound_->minY_) / (bound_->maxY_ - bound_->minY_);
    double u = stToUV(s);
    double v = stToUV(t);
    ret = S2Point(1, u, v);
  } else {
    invalid_ = true;
  }
  return ret;
}

template<typename T_IBIN>
int ObWkbToS2Visitor::MakeProjS2Point(T_IBIN *geo, S2Cell *&res)
{
  int ret = OB_SUCCESS;
  S2Point point = MakeS2PointFromXy(geo->x(), geo->y());
  S2Cell* p = NULL;
  if (!invalid_) {
    if (OB_FAIL(add_cell_from_point(point))) {
    } else {
      p = new S2Cell(point);
      if (OB_ISNULL(p)) {
        ret = OB_ERR_UNEXPECTED;
      } else {
        res = p;
        bounder_.AddPoint(point);
      }
    }
  }
  return ret;
}

template<typename T_IBIN>
int ObWkbToS2Visitor::MakeS2Polyline(T_IBIN *geo, S2Polyline *&res)
{
  int ret = OB_SUCCESS;
  std::vector<S2LatLng> vertices;
  const typename T_IBIN::value_type *line = reinterpret_cast<const typename T_IBIN::value_type *>(geo->val());
  typename T_IBIN::value_type::iterator iter = line->begin();
  for ( ; iter != line->end() && OB_SUCC(ret); iter++) {
    S2LatLng latlng = S2LatLng::FromDegrees(iter->template get<1>(),
                                            iter->template get<0>());
    if (OB_FAIL(add_cell_from_point(latlng))) {
    } else if (OB_FAIL(vector_push_back<S2LatLng>(vertices, latlng))) {
    } else {
      bounder_.AddPoint(S2Point(latlng));
    }
  }
  if (OB_SUCC(ret)) {
    S2Polyline* ptr = new S2Polyline(vertices);
    if (OB_ISNULL(ptr)) {
      ret = OB_ERR_UNEXPECTED;
    } else {
      res = ptr;
    }
  }
  
  return ret;
}

template<typename T_IBIN>
int ObWkbToS2Visitor::MakeProjS2Polyline(T_IBIN *geo, S2Polyline *&res)
{
  int ret = OB_SUCCESS;
  std::vector<S2Point> vertices;
  const typename T_IBIN::value_type *line = reinterpret_cast<const typename T_IBIN::value_type *>(geo->val());
  typename T_IBIN::value_type::iterator iter = line->begin();
  for ( ; iter != line->end() && OB_SUCC(ret); iter++) {
    S2Point p = MakeS2PointFromXy(iter->template get<0>(),
                                  iter->template get<1>());
    if (OB_FAIL(add_cell_from_point(p))) {
    } else if (OB_FAIL(vector_push_back<S2Point>(vertices, p))) {
    } else {
      bounder_.AddPoint(p);
    }
  }

 if (OB_SUCC(ret)) {
    S2Polyline* ptr = new S2Polyline(vertices);
    if (OB_ISNULL(ptr)) {
      ret = OB_ERR_UNEXPECTED;
    } else {
      res = ptr;
    }
  }
  return ret;
}

template<typename T_IBIN, typename T_BIN,
         typename T_BIN_RING, typename T_BIN_INNER_RING>
int ObWkbToS2Visitor::MakeS2Polygon(T_IBIN *geo, S2Polygon *&res)
{
  int ret = OB_SUCCESS;
  T_BIN& poly = *(T_BIN *)(geo->val());
  T_BIN_RING& exterior = poly.exterior_ring();
  T_BIN_INNER_RING& inner_rings = poly.inner_rings();
  std::vector<std::unique_ptr<S2Loop>> s2poly;
  if (poly.size() != 0) {
    std::vector<S2Point> vertices;
    typename T_BIN_RING::iterator iter = exterior.begin();
    for (; iter != exterior.end() && OB_SUCC(ret); ++iter) {
      S2LatLng latlng = S2LatLng::FromDegrees(iter->template get<1>(), iter->template get<0>());
      S2Point tmp = S2Point(latlng);
      if (OB_FAIL(add_cell_from_point(latlng))) {
      } else if (OB_FAIL(vector_push_back<S2Point>(vertices, tmp))) {
      } else {
        bounder_.AddPoint(tmp);
      }
    }
    if (OB_SUCC(ret)) {
      S2Loop *loop = new S2Loop(vertices);
      if (OB_ISNULL(loop)) {
        ret = OB_ERR_UNEXPECTED;
      } else {
        loop->Normalize();
        if (OB_FAIL(ret)) {
        } else if (OB_FAIL(vector_emplace_back(s2poly, loop))) {
        }
      }      
    }
  }

  typename T_BIN_INNER_RING::iterator iterInnerRing = inner_rings.begin();
  for (; iterInnerRing != inner_rings.end() && OB_SUCC(ret); ++iterInnerRing) {
    std::vector<S2Point> vertices;
    typename T_BIN_RING::iterator iter = (*iterInnerRing).begin();
    for (; iter != (*iterInnerRing).end() && OB_SUCC(ret); ++iter) {
      S2LatLng latlng = S2LatLng::FromDegrees(iter->template get<1>(), iter->template get<0>());
      S2Point tmp = S2Point(latlng);
      if (OB_FAIL(add_cell_from_point(latlng))) {
      } else if (OB_FAIL(vector_push_back<S2Point>(vertices, tmp))) {
      } else {
        bounder_.AddPoint(tmp);
      }
    }
    if (OB_SUCC(ret)) {
      S2Loop *loop = new S2Loop(vertices);
      if (OB_ISNULL(loop)) {
        ret = OB_ERR_UNEXPECTED;
      } else {
        loop->Normalize();
        if (OB_FAIL(ret)) {
        } else if (OB_FAIL(vector_emplace_back(s2poly, loop))) {
        }
      }      
    }
  }

  S2Polygon* py = new S2Polygon(std::move(s2poly));
  if (OB_FAIL(ret)) {
  } else if (OB_ISNULL(py)) {
    ret = OB_ERR_UNEXPECTED;
  } else {
    res = py;
  }
  return ret;
}

template<typename T_IBIN, typename T_BIN,
         typename T_BIN_RING, typename T_BIN_INNER_RING>
int ObWkbToS2Visitor::MakeProjS2Polygon(T_IBIN *geo, S2Polygon *&res)
{
  int ret = OB_SUCCESS;
  T_BIN& poly = *(T_BIN *)(geo->val());
  T_BIN_RING& exterior = poly.exterior_ring();
  T_BIN_INNER_RING& inner_rings = poly.inner_rings();
  std::vector<std::unique_ptr<S2Loop>> s2poly;
  if (poly.size() != 0) {
    std::vector<S2Point> vertices;
    typename T_BIN_RING::iterator iter = exterior.begin();
    for (; iter != exterior.end() && OB_SUCC(ret); ++iter) {
      S2Point tmp = MakeS2PointFromXy(iter->template get<0>(), iter->template get<1>());
      if (OB_FAIL(add_cell_from_point(tmp))) {
      } else if (OB_FAIL(vector_push_back<S2Point>(vertices, tmp))) {
      } else {
        bounder_.AddPoint(tmp);
      }
    }
    if (OB_SUCC(ret)) {
      S2Loop *loop = new S2Loop(vertices);
      if (OB_ISNULL(loop)) {
        ret = OB_ERR_UNEXPECTED;
      } else {
        loop->Normalize();
        if (OB_FAIL(ret)) {
        } else if (OB_FAIL(vector_emplace_back(s2poly, loop))) {
        }
      }      
    }
  }

  typename T_BIN_INNER_RING::iterator iterInnerRing = inner_rings.begin();
  for (; iterInnerRing != inner_rings.end() && OB_SUCC(ret); ++iterInnerRing) {
    std::vector<S2Point> vertices;
    typename T_BIN_RING::iterator iter = (*iterInnerRing).begin();
    for (; iter != (*iterInnerRing).end() && OB_SUCC(ret); ++iter) {
      S2Point tmp = MakeS2PointFromXy(iter->template get<0>(), iter->template get<1>());
      if (OB_FAIL(add_cell_from_point(tmp))) {
      } else if (OB_FAIL(vector_push_back<S2Point>(vertices, tmp))) {
      } else {
        bounder_.AddPoint(tmp);
      }
    }
    if (OB_SUCC(ret)) {
      S2Loop *loop = new S2Loop(vertices);
      if (OB_ISNULL(loop)) {
        ret = OB_ERR_UNEXPECTED;
      } else {
        loop->Normalize();
        if (OB_FAIL(ret)) {
        } else if (OB_FAIL(vector_emplace_back(s2poly, loop))) {
        }
      }
    }
  }

  S2Polygon* py = new S2Polygon(std::move(s2poly));
  if (OB_FAIL(ret)) {
  } else if (OB_ISNULL(py)) {
    ret = OB_ERR_UNEXPECTED;
  } else {
    res = py;
  }
  return ret;
}


int ObWkbToS2Visitor::visit(ObIWkbGeogPoint *geo)
{
  INIT_SUCC(ret);
  S2Cell *res = NULL;
  if (geo->length() < (WKB_GEO_BO_SIZE + WKB_GEO_TYPE_SIZE)) {
    ret = OB_ERR_GIS_INVALID_DATA;
  } else if (OB_FAIL(MakeS2Point<ObIWkbGeogPoint>(geo, res))) {
  } else if (OB_FAIL(vector_emplace_back<S2Cell>(s2v_, res))) {
  }
  return ret;
}

int ObWkbToS2Visitor::visit(ObIWkbGeomPoint *geo)
{
  INIT_SUCC(ret);
  S2Cell *cell = nullptr;
  if (!invalid_) {
    if (OB_FAIL(MakeProjS2Point(geo, cell))) {
    } else if (OB_FAIL(vector_emplace_back<S2Cell>(s2v_, cell))) {
    }
  }
  return ret;
}

int ObWkbToS2Visitor::visit(ObIWkbGeogLineString *geo)
{
  INIT_SUCC(ret);
  S2Polyline *polyline = nullptr;
  if (geo->length() < WKB_COMMON_WKB_HEADER_LEN) {
    ret = OB_ERR_GIS_INVALID_DATA;
  } else if (OB_FAIL(MakeS2Polyline<ObIWkbGeogLineString>(geo, polyline))) {
  } else if (OB_FAIL(vector_emplace_back<S2Polyline>(s2v_, polyline))) {
  } else {
    mbr_ = mbr_.is_empty() ? polyline->GetRectBound() : mbr_.Union(polyline->GetRectBound());
  }
  return ret;
}

int ObWkbToS2Visitor::visit(ObIWkbGeomLineString *geo)
{
  INIT_SUCC(ret);
  if (!invalid_) {
    S2Polyline *line = nullptr;
    if (OB_FAIL(MakeProjS2Polyline<ObIWkbGeomLineString>(geo, line))) {
    } else if (OB_FAIL(vector_emplace_back<S2Polyline>(s2v_, line))) {
    }
  }
  return ret;
}

int ObWkbToS2Visitor::visit(ObIWkbGeogPolygon *geo)
{
  INIT_SUCC(ret);
  S2Polygon *polygon = nullptr;
  if (geo->length() < WKB_COMMON_WKB_HEADER_LEN) {
    ret = OB_ERR_GIS_INVALID_DATA;
  } else if ((ret = MakeS2Polygon<ObIWkbGeogPolygon, ObWkbGeogPolygon,
                                  ObWkbGeogLinearRing, ObWkbGeogPolygonInnerRings>(geo, polygon)) != OB_SUCCESS) {
  } else if (OB_FAIL(vector_emplace_back<S2Polygon>(s2v_, polygon))) {
  } else {
    mbr_ = mbr_.is_empty() ? polygon->GetRectBound() : mbr_.Union(polygon->GetRectBound());
  }
  
  return ret;
}

int ObWkbToS2Visitor::visit(ObIWkbGeomPolygon *geo)
{
  INIT_SUCC(ret);
  if (!invalid_) {
    S2Polygon *poly = nullptr;
    if (geo->length() < WKB_COMMON_WKB_HEADER_LEN) {
      ret = OB_ERR_GIS_INVALID_DATA;
    } else if ((ret = MakeProjS2Polygon<ObIWkbGeomPolygon, ObWkbGeomPolygon,
                                        ObWkbGeomLinearRing, ObWkbGeomPolygonInnerRings>(geo, poly)) != OB_SUCCESS) {
    } else if (OB_FAIL(vector_emplace_back<S2Polygon>(s2v_, poly))) {
    }
  }
  return ret;
}

int64_t ObWkbToS2Visitor::get_cellids(ObS2Cellids &cells, bool is_query,
                                      bool need_buffer, S1Angle distance)
{
  int ret = OB_SUCCESS;
  try {
    const auto result = seekdb::geo::s2_index::cell_ids(cell_union_, options_,
        s2v_.size() > 1, invalid_, has_reset_, is_query, need_buffer, distance);
    for (const auto cell : result) {
      if (OB_FAIL(cells.push_back(cell))) break;
    }
  } catch (...) {
    ret = ob_boost_geometry_exception_handle();
  }
  return ret;
}

bool ObWkbToS2Visitor::is_full_range_cell_union(S2CellUnion &cellids)
{
  return seekdb::geo::s2_index::full_range(cellids, is_geog_);
}

int ObWkbToS2Visitor::get_s2_cell_union()
{
  int ret = OB_SUCCESS;
  try {
    if (!invalid_) {
      seekdb::geo::s2_index::cover_regions(s2v_, options_, is_geog_,
          bounder_, cell_union_, mbr_, S2cells_);
    }
  } catch (...) {
    ret = ob_boost_geometry_exception_handle();
  }
  return ret;
}

int64_t ObWkbToS2Visitor::get_cellids_and_unrepeated_ancestors(
    ObS2Cellids &cells, ObS2Cellids &ancestors, bool need_buffer, S1Angle distance)
{
  int ret = OB_SUCCESS;
  try {
    const auto result = seekdb::geo::s2_index::cells_and_ancestors(
        cell_union_, options_, s2v_.size() > 1, invalid_, has_reset_, need_buffer, distance);
    for (const auto cell : result.cells) {
      if (OB_FAIL(cells.push_back(cell))) break;
    }
    for (size_t i = 0; OB_SUCC(ret) && i < result.ancestors.size(); ++i) {
      if (OB_FAIL(ancestors.push_back(result.ancestors[i]))) {}
    }
  } catch (...) {
    ret = ob_boost_geometry_exception_handle();
  }
  return ret;
}

int64_t ObWkbToS2Visitor::get_inner_cover_cellids(ObS2Cellids &cells)
{
  int ret = OB_SUCCESS;
  try {
    const auto result = seekdb::geo::s2_index::vertex_cell_ids(S2cells_, invalid_);
    for (const auto cell : result) {
      if (OB_FAIL(cells.push_back(cell))) break;
    }
  } catch (...) {
    ret = ob_boost_geometry_exception_handle();
  }
  return ret;
}

int64_t ObWkbToS2Visitor::get_mbr(S2LatLngRect &mbr, bool need_buffer, S1Angle distance)
{
  mbr = seekdb::geo::s2_index::geographic_mbr(mbr_, invalid_, has_reset_, need_buffer, distance);
  return OB_SUCCESS;
}

void ObWkbToS2Visitor::reset()
{
  s2v_.clear();
  mbr_ = S2LatLngRect::Empty();
  S2cells_.clear();
  invalid_ = false;
  has_reset_ = true;
  cell_union_.Clear();
  // Reinitialize the edge bounder; ending its lifetime without reconstruction
  // made the out-of-bounds retry visit an already destroyed object.
  bounder_.~S2LatLngRectBounder();
  new (&bounder_) S2LatLngRectBounder();
}

template <typename ElementType>
int ObWkbToS2Visitor::vector_push_back(std::vector<ElementType> &vector, ElementType &element)
{
  int ret = OB_SUCCESS;
  try {
    vector.push_back(element);
  } catch(...) {
    ret = ob_boost_geometry_exception_handle();
  }
  return ret;
}

int ObWkbToS2Visitor::vector_emplace_back(std::vector<std::unique_ptr<S2Loop>> &vector, S2Loop *element)
{
  int ret = OB_SUCCESS;
  try {
    vector.emplace_back(element);
  } catch(...) {
    ret = ob_boost_geometry_exception_handle();
  }
  return ret;
}
template <typename ElementType>
int ObWkbToS2Visitor::vector_emplace_back(std::vector<std::unique_ptr<S2Region>> &vector, ElementType *element)
{
  int ret = OB_SUCCESS;
  try {
    vector.emplace_back(element);
  } catch(...) {
    ret = ob_boost_geometry_exception_handle();
  }
  return ret;
}

} // namespace common
} // namespace oceanbase
