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

#if SEEKDB_ENABLE_CORE_GIS

#define USING_LOG_PREFIX LIB
#include "ob_s2adapter.h"
#include "share/geo/ob_geo_func_envelope.h"
#include "share/geo/ob_geo_3d.h"
#include "seekdb/geo/s2_mbr.hpp"
#include "seekdb/geo/s2_covering.hpp"

namespace oceanbase {
namespace common {

int ObSpatialMBR::filter(const ObSpatialMBR &other, ObDomainOpType type, bool &pass_through) const
{
  namespace mbr = seekdb::geo::index_mbr;
  pass_through = true;
  if (is_geog_ != other.is_geog_) return OB_INVALID_ARGUMENT;
  mbr::Relation relation;
  switch (type) {
    case ObDomainOpType::T_GEO_COVERS: relation = mbr::Relation::covers; break;
    case ObDomainOpType::T_GEO_DWITHIN:
    case ObDomainOpType::T_GEO_INTERSECTS: relation = mbr::Relation::intersects; break;
    case ObDomainOpType::T_GEO_COVEREDBY: relation = mbr::Relation::covered_by; break;
    case ObDomainOpType::T_GEO_DFULLYWITHIN: return OB_NOT_SUPPORTED;
    default: return OB_INVALID_ARGUMENT;
  }
  return mbr::filter({x_min_, x_max_, y_min_, y_max_},
                    {other.x_min_, other.x_max_, other.y_min_, other.y_max_},
                    is_geog_, is_point_, other.is_point_, relation, pass_through)
      ? OB_SUCCESS : OB_INVALID_ARGUMENT;
}

int ObSpatialMBR::to_char(char *buf, int64_t &buf_len) const
{
  size_t size = 0;
  const bool ok = seekdb::geo::index_mbr::encode({x_min_, x_max_, y_min_, y_max_},
      is_point_, buf, OB_DEFAULT_MBR_SIZE, size);
  buf_len = size;
  return ok ? OB_SUCCESS : OB_INVALID_ARGUMENT;
}

int ObSpatialMBR::from_string(ObString &mbr_str, ObDomainOpType type,
                              ObSpatialMBR &spa_mbr, bool is_point)
{
  seekdb::geo::index_mbr::Box box;
  if (mbr_str.length() < 0 || !seekdb::geo::index_mbr::decode(
      mbr_str.ptr(), mbr_str.length(), is_point, box)) return OB_INVALID_ARGUMENT;
  spa_mbr = ObSpatialMBR(box.xmin, box.xmax, box.ymin, box.ymax, type);
  spa_mbr.is_point_ = is_point;
  return OB_SUCCESS;
}

int ObSpatialMBR::generate_latlng_rect(S2LatLngRect &rect) const
{
  INIT_SUCC(ret);
  S1Angle lat_lo = S1Angle::Degrees(y_min_);
  S1Angle lat_hi = S1Angle::Degrees(y_max_);
  S1Angle lng_lo = S1Angle::Degrees(x_min_);
  S1Angle lng_hi = S1Angle::Degrees(x_max_);
  S2LatLng lo(lat_lo, lng_lo);
  S2LatLng hi(lat_hi, lng_hi);
  new (&rect) S2LatLngRect(lo, hi);
  return ret;
}

int ObSpatialMBR::generate_box(ObCartesianBox &rect) const
{
  INIT_SUCC(ret);
  ObWkbGeomInnerPoint min_point(x_min_, y_min_);
  ObWkbGeomInnerPoint max_point(x_max_, y_max_);
  new (&rect) ObCartesianBox(min_point, max_point);
  return ret;
}

OB_DEF_SERIALIZE(ObSpatialMBR)
{
  INIT_SUCC(ret);
  OB_UNIS_ENCODE(y_min_);
  OB_UNIS_ENCODE(y_max_);
  OB_UNIS_ENCODE(x_min_);
  OB_UNIS_ENCODE(x_max_);
  OB_UNIS_ENCODE(static_cast<int64_t>(mbr_type_));
  OB_UNIS_ENCODE(is_point_);
  OB_UNIS_ENCODE(is_geog_);
  return ret;
}

OB_DEF_SERIALIZE_SIZE(ObSpatialMBR)
{
  int64_t len = 0;
  OB_UNIS_ADD_LEN(y_min_);
  OB_UNIS_ADD_LEN(y_max_);
  OB_UNIS_ADD_LEN(x_min_);
  OB_UNIS_ADD_LEN(x_max_);
  OB_UNIS_ADD_LEN(static_cast<int64_t>(mbr_type_));
  OB_UNIS_ADD_LEN(is_point_);
  OB_UNIS_ADD_LEN(is_geog_);
  return len;
}

OB_DEF_DESERIALIZE(ObSpatialMBR)
{
  INIT_SUCC(ret);
  int64_t mbr_type = 0;
  OB_UNIS_DECODE(y_min_);
  OB_UNIS_DECODE(y_max_);
  OB_UNIS_DECODE(x_min_);
  OB_UNIS_DECODE(x_max_);
  OB_UNIS_DECODE(mbr_type);
  if (OB_SUCC(ret)) {
    mbr_type_ = static_cast<ObDomainOpType>(mbr_type);
  }
  OB_UNIS_DECODE(is_point_);
  OB_UNIS_DECODE(is_geog_);
  return ret;
}

int64_t ObS2Adapter::get_child_of_cellid(uint64_t id, uint64_t &child_start, uint64_t &child_end)
{
  seekdb::geo::s2_index::CellInfo info;
  if (!seekdb::geo::s2_index::cell_info(id, info)) return OB_INVALID_ARGUMENT;
  child_start = info.range_min;
  child_end = info.range_max;
  return OB_SUCCESS;
}

int64_t ObS2Adapter::get_cellids(ObS2Cellids &cells, bool is_query)
{
  INIT_SUCC(ret);
  if(OB_FAIL(visitor_->get_cellids(cells, is_query, need_buffer_, distance_))) {
  }
  return ret;
}

int64_t ObS2Adapter::get_cellids_and_unrepeated_ancestors(ObS2Cellids &cells, ObS2Cellids &ancestors)
{
  INIT_SUCC(ret);
  if(OB_FAIL(visitor_->get_cellids_and_unrepeated_ancestors(cells, ancestors, need_buffer_, distance_))) {
  }
  return ret;
}

int64_t ObS2Adapter::get_inner_cover_cellids(ObS2Cellids &cells)
{
  INIT_SUCC(ret);
  if(OB_FAIL(visitor_->get_inner_cover_cellids(cells))) {
  }
  return ret;
}

int64_t ObS2Adapter::get_ancestors(uint64_t cell, ObS2Cellids &cells)
{
  INIT_SUCC(ret);
  static_assert(OB_GEO_S2REGION_OPTION_LEVEL_MOD == 1);
  seekdb::geo::s2_index::CellInfo info;
  if (!seekdb::geo::s2_index::cell_info(cell, info)) return OB_INVALID_ARGUMENT;
  for (uint32_t i = 0; OB_SUCC(ret) && i < info.ancestor_count; ++i) {
    if (OB_FAIL(cells.push_back(info.ancestors[i]))) {}
  }
  return ret;
}

int64_t ObS2Adapter::get_mbr(ObSpatialMBR &mbr)
{
  INIT_SUCC(ret);
  if (OB_ISNULL(geo_)) {
    ret = OB_ERR_NULL_VALUE;
  } else {
    mbr.is_geog_ = is_geog_;
    mbr.is_point_ = (geo_->type() == ObGeoType::POINT);
    if (is_geog_) {
      S2LatLngRect rect;
      if (OB_FAIL(visitor_->get_mbr(rect, need_buffer_, distance_))) {
      } else if (rect.is_empty()) {
        LOG_DEBUG("It's might be empty geometry collection", K(geo_->type()), K(geo_->is_empty()));
      } else {
        mbr.y_min_ = rect.lat_lo().degrees();
        mbr.y_max_ = rect.lat_hi().degrees();
        mbr.x_min_ = rect.lng_lo().degrees();
        mbr.x_max_ = rect.lng_hi().degrees();
      }
    } else {
      CREATE_WITH_TEMP_CONTEXT(lib::ContextParam().set_mem_attr("GISModule", ObCtxIds::DEFAULT_CTX_ID)) {
        ObCartesianBox box;
        ObGeoEvalCtx gis_context(CURRENT_CONTEXT, NULL);
        if (OB_FAIL(gis_context.append_geo_arg(geo_))) {
        } else if (OB_FAIL(ObGeoFuncEnvelope::eval(gis_context, box))) {
        } else if (box.is_empty()) {
          LOG_DEBUG("It's might be empty geometry collection", K(geo_->type()), K(geo_->is_empty()));
        } else {
          mbr.x_min_ = box.min_corner().get<0>();
          mbr.y_min_ = box.min_corner().get<1>();
          mbr.x_max_ = box.max_corner().get<0>();
          mbr.y_max_ = box.max_corner().get<1>();
        }
      }
    }
  }
  return ret;
}

int64_t ObS2Adapter::init(const ObString &swkb, const ObSrsBoundsItem *bound)
{
  INIT_SUCC(ret);
  if (OB_ISNULL(visitor_)) {
   if (OB_ISNULL(visitor_ = new ObWkbToS2Visitor(bound, options_, is_geog_))) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
    } else if (OB_ISNULL(swkb.ptr())) {
      ret = OB_INVALID_ARGUMENT;
    } else {
      ObGeoType type = ObGeoType::GEOTYPEMAX;
      ObGeometry *geo = NULL;
      ObString wkb;
      uint32_t offset;
      if (OB_FAIL(ObGeoTypeUtil::get_type_from_wkb(swkb, type))) {
      } else if (OB_FAIL(ObGeoTypeUtil::create_geo_by_type(*allocator_, type, is_geog_, true, geo))) {
      } else if (OB_FAIL(ObGeoTypeUtil::get_wkb_from_swkb(swkb, wkb, offset))) {
      } else {
        geo->set_data(wkb);
        if (ObGeoTypeUtil::is_3d_geo_type(type)) {
          ObGeometry3D *geo_3d = static_cast<ObGeometry3D *>(geo);
          if (OB_FAIL(geo_3d->to_2d_geo(*allocator_, geo))) {
          }
        }
      }
      
      if (OB_SUCC(ret)) {
        geo_ = geo;
        if (OB_FAIL(geo->do_visit(*visitor_))) {
        } else if (OB_FAIL(visitor_->get_s2_cell_union())) {
        } else if (visitor_->is_invalid()) {
          // 1. get valid geo inside bounds
          ObGeometry *corrected_geo = NULL;
          bool need_do_visit = true;
          if (OB_FAIL(ObGeoTypeUtil::get_mbr_polygon(*allocator_, bound, *geo, corrected_geo))) {
            if (ret == OB_EMPTY_RESULT) {
              ret = OB_SUCCESS;
              need_do_visit = false;
            } else {
            }
          }

          if (OB_FAIL(ret)) {
          } else if (need_do_visit) {
            // 2. reset visitor_
            visitor_->reset();
            // 3. do_visit again
            if (OB_FAIL(corrected_geo->do_visit(*visitor_))) {
            } else if (OB_FAIL(visitor_->get_s2_cell_union())) {
            } 
          }
        }
      }
    }
  }
  return ret;
}

ObS2Adapter::~ObS2Adapter()
{
  delete visitor_;
}

} // namespace common
} // namespace oceanbase

#endif // SEEKDB_ENABLE_CORE_GIS
