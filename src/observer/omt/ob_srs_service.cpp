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

#define USING_LOG_PREFIX SERVER_OMT
#include "lib/stat/ob_diagnostic_info_guard.h"
#include "ob_srs_service.h"
#include "share/ob_sql_client_decorator.h"
#include "src/share/ob_server_struct.h"
#include "sql/engine/cmd/ob_srs_importer.h"
#include "share/ob_internal_table_change_notifier.h"
#include "share/geo/ob_geo_utils.h"

using namespace oceanbase::share;
using namespace oceanbase::common;
using namespace oceanbase::common::sqlclient;

namespace oceanbase
{
namespace omt
{

int ObSrsService::server_module_init(ObSrsService* &srs_service)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(srs_service->init())) {
    LOG_WARN("fail to init runtime SRS", K(ret));
  }
  return ret;
}

void ObSrsService::destroy()
{
  if (OB_LIKELY(inited_)) {
    recycle_old_snapshots();
    if (OB_NOT_NULL(last_sys_snapshot_)) {
      last_sys_snapshot_->~ObSrsCacheSnapShot();
      allocator_.free(last_sys_snapshot_);
      last_sys_snapshot_ = NULL;
    }
    allocator_.~ObFIFOAllocator();
  }
}

int ObSrsService::init()
{
  int ret = OB_SUCCESS;
  sql_proxy_ = GCTX.sql_proxy_;
  lib::ObMemAttr mem_attr("SrsService");
  if (inited_) {
    ret = OB_INIT_TWICE;
    LOG_WARN("ObSrsService init twice.", K(ret));
  } else if (OB_FAIL(allocator_.init(&alloc_, OB_MALLOC_MIDDLE_BLOCK_SIZE, mem_attr))) {
    LOG_WARN("ObSrsService allocator init failed.", K(ret));
  } else {
    page_allocator_.set_allocator(&allocator_);
    page_allocator_.set_attr(mem_attr);
    mode_arena_.init(DEFAULT_PAGE_SIZE, page_allocator_);
    inited_ = true;
  }
  if (OB_SUCC(ret)) {
    infinite_plane_.minX_ = INT32_MIN;
    infinite_plane_.minY_ = INT32_MIN;
    infinite_plane_.maxX_ = INT32_MAX;
    infinite_plane_.maxY_ = INT32_MAX;
    share::ObInternalTableChangeNotifier::get_instance().register_module(
        table::ObModuleDataArg::GIS,
        []() -> int {
          SRS_SERVICE->mark_stale();
          LOG_INFO("[SRS] marked stale by notifier");
          return OB_SUCCESS;
        });
  }
  return ret;
}

ObSrsCacheGuard::~ObSrsCacheGuard()
{
  if (OB_NOT_NULL(srs_cache_)) {
    srs_cache_->dec_ref_count();
  }
}

int ObSrsCacheGuard::get_srs_item(uint64_t original_srs_id, const ObSrsItem *&srs_item)
{
  int ret = OB_SUCCESS;
  const ObSrsItem *tmp_srs_item = NULL;
  uint64_t srs_id = original_srs_id;
  if (OB_ISNULL(srs_cache_)) {
    ret = OB_ERR_NULL_VALUE;
    LOG_WARN("srs_cache is null", K(ret));
  } else if (srs_id > UINT_MAX32) {
    ret = OB_ERR_WARN_DATA_OUT_OF_RANGE;
    LOG_WARN("srs id out of range", K(ret), K(srs_id));
  } else if (OB_SUCC(srs_cache_->get_srs_item(srs_id, tmp_srs_item))) {
    srs_item = tmp_srs_item;
  } else {
    LOG_WARN("failed to find srs item", K(ret), K(srs_id));
    if (ret == OB_HASH_NOT_EXIST) {
      ret = OB_ERR_SRS_NOT_FOUND;
      LOG_USER_ERROR(OB_ERR_SRS_NOT_FOUND, static_cast<uint32_t>(srs_id));
    }
  }
  return ret;
}

int ObSrsService::get_srs_guard(ObSrsCacheGuard &srs_guard)
{
  int ret = OB_SUCCESS;
  if (!srs_guard.empty()) {
    return ret;
  }
  lib::ObMutexGuard guard(srs_load_lock_);
  if (!ATOMIC_LOAD(&srs_stale_) && OB_NOT_NULL(last_sys_snapshot_)) {
    last_sys_snapshot_->inc_ref_count();
    srs_guard.set_srs_snapshot(last_sys_snapshot_);
  } else if (OB_FAIL(refresh_sys_srs())) {
    ATOMIC_STORE(&srs_stale_, false);
    ret = OB_ERR_SRS_EMPTY;
    LOG_WARN("srs data not available", K(ret));
    LOG_USER_ERROR(OB_ERR_SRS_EMPTY);
  } else {
    ATOMIC_STORE(&srs_stale_, false);
    last_sys_snapshot_->inc_ref_count();
    srs_guard.set_srs_snapshot(last_sys_snapshot_);
  }
  return ret;
}

int ObSrsService::get_srs_bounds(uint64_t srid, const ObSrsItem *srs_item, const ObSrsBoundsItem *&bounds_item)
{
  int ret = OB_SUCCESS;
  if (srid == 0) {
    bounds_item = &infinite_plane_;
  } else if (OB_ISNULL(srs_item)) {
    ret = OB_ERR_NULL_VALUE;
    LOG_ERROR("srs item is null", K(ret));
  } else {
    const ObSrsBoundsItem *tmp_bounds = srs_item->get_bounds();
    if (isnan(tmp_bounds->minX_) || isnan(tmp_bounds->minY_)
        || isnan(tmp_bounds->maxX_) || isnan(tmp_bounds->maxY_)) {
      ret = OB_INVALID_ARGUMENT;
      LOG_WARN("invalid bounds info", K(ret), K(srid), K(srs_item->get_srid()), K(*tmp_bounds));
    } else {
      bounds_item = tmp_bounds;
    }
  }
  return ret;
}

int ObSrsService::refresh_sys_srs()
{
  int ret = OB_SUCCESS;
  ObSrsCacheSnapShot *srs = NULL;

  if (OB_FAIL(fetch_all_srs(srs))) {
    if (ret == OB_ERR_EMPTY_QUERY) {
      LOG_DEBUG("srs table is empty");
    } else {
      LOG_WARN("failed to fetch srs snapshot", K(ret));
    }
  } else {
    if (last_sys_snapshot_ != NULL) {
      if (last_sys_snapshot_->get_ref_count() > 0) {
        if (OB_FAIL(srs_old_snapshots_.push_back(last_sys_snapshot_))) {
          LOG_WARN("failed to push last_snapshot to recycle queue", K(ret));
        }
      }
      if (OB_SUCC(ret)) {
        // ref_count > 0: already pushed to old queue; == 0: safe to free
        if (last_sys_snapshot_->get_ref_count() <= 0) {
          last_sys_snapshot_->~ObSrsCacheSnapShot();
          allocator_.free(last_sys_snapshot_);
        }
      }
    }
    if (OB_SUCC(ret)) {
      last_sys_snapshot_ = srs;
      for (int64_t i = srs_old_snapshots_.size() - 1; i >= 0; i--) {
        ObSrsCacheSnapShot *snap = srs_old_snapshots_[i];
        if (OB_NOT_NULL(snap) && snap->get_ref_count() <= 0) {
          srs_old_snapshots_.remove(i);
          snap->~ObSrsCacheSnapShot();
          allocator_.free(snap);
        }
      }
      LOG_INFO("[SRS] refresh succeeded", K(srs->get_srs_count()),
               K(srs_old_snapshots_.size()));
    }
  }
  return ret;
}

void ObSrsService::recycle_old_snapshots()
{
  for (int64_t i = srs_old_snapshots_.size() - 1; i >= 0; i--) {
    ObSrsCacheSnapShot *snap = srs_old_snapshots_[i];
    if (OB_NOT_NULL(snap)) {
      srs_old_snapshots_.remove(i);
      snap->~ObSrsCacheSnapShot();
      allocator_.free(snap);
    }
  }
}

int ObSrsCacheSnapShot::get_srs_item(uint64_t srid, const ObSrsItem *&srs_item)
{
  int ret = OB_SUCCESS;
  const ObSrsItem *tmp_srs_item = NULL;
  if (OB_SUCC(srs_item_map_.get_refactored(srid, tmp_srs_item))) {
    srs_item = tmp_srs_item;
  }
  return ret;
}

int ObSrsService::fetch_all_srs(ObSrsCacheSnapShot *&srs_snapshot)
{
  int ret = OB_SUCCESS;
  ObSrsCacheSnapShot *snapshot = NULL;
  uint32_t res_count = 0;

  int64_t srs_cnt = 0;
  const int TOTAL_SRS_CNT = 5152;

  if (OB_FAIL(table::ObSRSImporter::get_srs_cnt(sql_proxy_, srs_cnt))) {
    LOG_WARN("get srs cnt failed", K(ret));
  } else if (srs_cnt < TOTAL_SRS_CNT) {
    if (srs_cnt > 1) {
      LOG_INFO("srs is importing, retry fetch later", K(srs_cnt));
    }
    ret = OB_ERR_EMPTY_QUERY;
  } else {
    ObSqlString sql;
    auto &sql_client_retry_weak = *sql_proxy_;
    SMART_VAR(ObMySQLProxy::MySQLResult, res) {
      ObASHSetInnerSqlWaitGuard ash_inner_sql_guard(ObInnerSqlWaitTypeId::OMT_FETCH_ALL_SRS);
      ObMySQLResult *result = NULL;
      if (OB_FAIL(sql.append_fmt("SELECT * FROM %s WHERE (SRS_ID < %d AND SRS_ID != 0) OR SRS_ID > %d",
          OB_ALL_SPATIAL_REFERENCE_SYSTEMS_TNAME, USER_SRID_MIN, USER_SRID_MAX))) {
        LOG_WARN("append sql failed", K(ret));
      } else if (OB_FAIL(sql_client_retry_weak.read(res, sql.ptr()))) {
        LOG_WARN("execute sql failed", K(sql), K(ret));
      } else if (OB_UNLIKELY(NULL == (result = res.get_result()))) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("fail to get result. ", K(ret));
      } else {
        while (OB_SUCC(ret) && OB_SUCCESS == (ret = result->next())) {
          const ObSrsItem *srs_item = NULL;
          const ObSrsItem *tmp = NULL;
          res_count++;
          if (OB_ISNULL(snapshot)) {
            snapshot = OB_NEWx(ObSrsCacheSnapShot, &allocator_);
            if (OB_ISNULL(snapshot)) {
              ret = OB_ALLOCATE_MEMORY_FAILED;
              LOG_WARN("failed to create ObSrsCacheSnapShot", K(ret));
            } else if (OB_FAIL(snapshot->init())) {
              LOG_WARN("failed to init ObSrsCacheSnapShot", K(ret));
            }
          }
          if (OB_FAIL(ret)) {
          } else if (OB_FAIL(snapshot->parse_srs_item(result, srs_item))) {
            LOG_WARN("failed to parse srs item from sys_table", K(ret));
            result->print_info();
          } else if (OB_FAIL(snapshot->get_srs_item(srs_item->get_srid(), tmp))) {
            if (ret == OB_HASH_NOT_EXIST) {
              if (OB_FAIL(snapshot->add_srs_item(srs_item->get_srid(), srs_item))) {
                LOG_WARN("failed to add srs item to snapshot", K(ret), K(srs_item->get_srid()));
              }
            } else {
              LOG_WARN("failed to get srs item from snapshot", K(ret));
            }
          } else {
            ret = OB_ERR_UNEXPECTED;
            LOG_WARN("duplicated srid in snapshot", K(ret));
            result->print_info();
          }
        }

        if (ret == OB_ITER_END) {
          if (res_count == 0) {
            ret = OB_ERR_EMPTY_QUERY;
          } else {
            if (OB_FAIL(generate_pg_reserved_srs(snapshot))) {
              LOG_WARN("failed to geneate pg reserved srs", K(ret));
              snapshot->~ObSrsCacheSnapShot();
              allocator_.free(snapshot);
            } else {
              srs_snapshot = snapshot;
            }
          }
        } else if (snapshot != NULL) {
          snapshot->~ObSrsCacheSnapShot();
          allocator_.free(snapshot);
          LOG_WARN("failed to get all srs item, iter quit", K(ret));
        }
      }
    }
  }
  return ret;
}

int ObSrsCacheSnapShot::extract_bounds_numberic(ObMySQLResult *result, const char *field_name, double &value)
{
  int ret = OB_SUCCESS;
  number::ObNumber nmb;
  if (OB_SUCC(result->get_number(field_name, nmb))) {
    const char *nmb_buf = nmb.format();
    if (OB_ISNULL(nmb_buf)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("nmb_buf is NULL", K(ret));
    } else {
      double val = 0.0;
      char *endptr = NULL;
      int err = 0;
      ObString num_str(strlen(nmb_buf), nmb_buf);
      val = ObCharset::strntodv2(num_str.ptr(), num_str.length(), &endptr, &err);
      if (EOVERFLOW == err && (-DBL_MAX == value || DBL_MAX == value)) {
        ret = OB_DATA_OUT_OF_RANGE;
        LOG_WARN("invalid numberic value", K(ret), K(err), K(num_str));
      } else {
        value = val;
      }
    }
  } else if (OB_ERR_NULL_VALUE) {
    ret = OB_SUCCESS;
  } else {
    LOG_WARN("failed to get number", K(ret), KP(field_name));
  }
  return ret;
}

int ObSrsCacheSnapShot::parse_srs_item(ObMySQLResult *result, const ObSrsItem *&srs_item)
{
  int ret = OB_SUCCESS;
  ObString srs_name, organization, definition, description, proj4text;
  uint64_t organization_coordsys_id = 0;
  uint64_t srs_id = 0;
  double min_x = NAN;
  double min_y = NAN;
  double max_x = NAN;
  double max_y = NAN;
  ObSpatialReferenceSystemBase *srs_info = NULL;
  lib::ObMallocHookAttrGuard malloc_guard(lib::ObMemAttr("SRSWKTParser"));

  EXTRACT_UINT_FIELD_MYSQL(*result, "srs_id", srs_id, uint64_t);
  EXTRACT_VARCHAR_FIELD_MYSQL(*result, "srs_name", srs_name);
  EXTRACT_VARCHAR_FIELD_MYSQL_SKIP_RET(*result, "organization", organization);
  EXTRACT_UINT_FIELD_MYSQL(*result, "organization_coordsys_id", organization_coordsys_id, uint64_t);
  EXTRACT_VARCHAR_FIELD_MYSQL(*result, "definition", definition);
  EXTRACT_VARCHAR_FIELD_MYSQL_SKIP_RET(*result, "description", description);
  EXTRACT_VARCHAR_FIELD_MYSQL_SKIP_RET(*result, "proj4text", proj4text);

  if (OB_FAIL(extract_bounds_numberic(result, "minX", min_x))) {
    LOG_WARN("failed to extract minx value", K(ret));
  } else if (OB_FAIL(extract_bounds_numberic(result, "minY", min_y))) {
    LOG_WARN("failed to extract miny value", K(ret));
  } else if (OB_FAIL(extract_bounds_numberic(result, "maxX", max_x))) {
    LOG_WARN("failed to extract maxx value", K(ret));
  } else if (OB_FAIL(extract_bounds_numberic(result, "maxY", max_y))) {
    LOG_WARN("failed to extract maxy value", K(ret));
  } else if (OB_FAIL(ObSrsWktParser::parse_srs_wkt(allocator_, srs_id, definition, srs_info))) {
    LOG_WARN("failed to parse srs wkt from definition", K(ret), K(definition));
  } else {
    ObSrsItem *new_srs_item = OB_NEWx(ObSrsItem, (&allocator_), srs_info);
    if (OB_ISNULL(new_srs_item)) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
      LOG_WARN("fail to alloc memory for srs item", K(ret));
    } else if (!proj4text.empty()) {
      srs_info->set_bounds(min_x, min_y, max_x, max_y);
      if (OB_FAIL(srs_info->set_proj4text(allocator_, proj4text))) {
        LOG_WARN("fail to set proj4text for srs item", K(ret), K(srs_id));
      }
    }
    if (OB_SUCC(ret)) {
      srs_item = new_srs_item;
    }
  }
  return ret;
}

int ObSrsCacheSnapShot::add_pg_reserved_srs_item(const ObString &pg_wkt, const uint32_t srs_id)
{
  int ret = OB_SUCCESS;
  ObString proj4text;
  ObSpatialReferenceSystemBase *srs_info = NULL;
  lib::ObMallocHookAttrGuard malloc_guard(lib::ObMemAttr("SRSWKTParser"));

  if (OB_FAIL(ObSrsWktParser::parse_srs_wkt(allocator_, srs_id, pg_wkt, srs_info))) {
    LOG_WARN("failed to parse pg reserved srs wkt", K(ret), K(srs_id), K(pg_wkt));
  } else {
    ObSrsItem *new_srs_item = OB_NEWx(ObSrsItem, (&allocator_), srs_info);
    if (OB_ISNULL(new_srs_item)) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
      LOG_WARN("fail to alloc memory for srs item", K(ret));
    } else if (OB_FAIL(ObGeoTypeUtil::get_pg_reserved_prj4text(&allocator_, srs_id, proj4text))) {
      LOG_WARN("fail to generate proj4text for pg srs item", K(ret));
    } else if (OB_FAIL(add_srs_item(new_srs_item->get_srid(), new_srs_item))) {
      LOG_WARN("failed to add pg srs item to snapshot", K(ret), K(new_srs_item->get_srid()));
    } else {
      srs_info->set_proj4text(proj4text);
    }
  }
  return ret;
}

int ObSrsService::generate_pg_reserved_srs(ObSrsCacheSnapShot *&srs_snapshot)
{
  int ret = OB_SUCCESS;
  char wkt_buf[MAX_WKT_LEN] = {0};
  if (OB_FAIL(srs_snapshot->add_pg_reserved_srs_item(NORTH_STEREO_WKT, SRID_NORTH_STEREO_PG))) {
    LOG_WARN("failed to parse pg reserved srs item", K(ret), K(SRID_NORTH_STEREO_PG));
  } else if (OB_FAIL(srs_snapshot->add_pg_reserved_srs_item(WORLD_MERCATOR_WKT, SRID_WORLD_MERCATOR_PG))) {
    LOG_WARN("failed to parse pg reserved srs item", K(ret), K(SRID_WORLD_MERCATOR_PG));
  } else if (OB_FAIL(srs_snapshot->add_pg_reserved_srs_item(SOUTH_LAMBERT_WKT, SRID_SOUTH_LAMBERT_PG))) {
    LOG_WARN("failed to parse pg reserved srs item", K(ret), K(SRID_SOUTH_LAMBERT_PG));
  } else if (OB_FAIL(srs_snapshot->add_pg_reserved_srs_item(NORTH_LAMBERT_WKT, SRID_NORTH_LAMBERT_PG))) {
    LOG_WARN("failed to parse pg reserved srs item", K(ret), K(SRID_NORTH_LAMBERT_PG));
  }

  for (int id = SRID_SOUTH_UTM_START_PG; id <= SRID_SOUTH_UTM_END_PG && OB_SUCC(ret); id++) {
    memset(wkt_buf, 0, MAX_WKT_LEN);
    int longitude = -177 + ((id - SRID_SOUTH_UTM_START_PG) * 6);
    snprintf(wkt_buf, MAX_WKT_LEN, SOUTH_UTM_WKT, longitude);
    ObString SOUTH_UTM = ObString::make_string(wkt_buf);
    if (OB_FAIL(srs_snapshot->add_pg_reserved_srs_item(SOUTH_UTM, id))) {
      LOG_WARN("failed to parse pg reserved srs item", K(ret), K(id));
    }
  }
  for (int id = SRID_NORTH_UTM_START_PG; id <= SRID_NORTH_UTM_END_PG && OB_SUCC(ret); id++) {
    memset(wkt_buf, 0, MAX_WKT_LEN);
    int longitude = -177 + ((id - SRID_NORTH_UTM_START_PG) * 6);
    snprintf(wkt_buf, MAX_WKT_LEN, NORTH_UTM_WKT, longitude);
    ObString NORTH_UTM = ObString::make_string(wkt_buf);
    if (OB_FAIL(srs_snapshot->add_pg_reserved_srs_item(NORTH_UTM, id))) {
      LOG_WARN("failed to parse pg reserved srs item", K(ret), K(id));
    }
  }

  for (int id = SRID_LAEA_START_PG; id < SRID_LAEA_END_PG && OB_SUCC(ret); id++) {
    int zone = id - SRID_LAEA_START_PG;
    int xzone = zone % 20;
    int yzone = zone / 20;
    double lat_0 = 30.0 * (yzone - 3) + 15.0;
    double lon_0 = 0.0;
    if  ( yzone == 2 || yzone == 3 ) {
      lon_0 = 30.0 * (xzone - 6) + 15.0;
    } else if ( yzone == 1 || yzone == 4 ) {
      lon_0 = 45.0 * (xzone - 4) + 22.5;
    } else if ( yzone == 0 || yzone == 5 ) {
      lon_0 = 90.0 * (xzone - 2) + 45.0;
    } else {
      ret = OB_INVALID_ARGUMENT;
      LOG_WARN("invalid pg srid", K(ret), K(id), K(xzone), K(yzone));
    }

    if (OB_SUCC(ret)) {
      while (lon_0 > 180) {
        lon_0 -= 360;
      }
      while (lon_0 < -180) {
        lon_0 += 360;
      }

      memset(wkt_buf, 0, MAX_WKT_LEN);
      snprintf(wkt_buf, MAX_WKT_LEN, LAEA_WKT, lat_0, lon_0);
      ObString LAEA = ObString::make_string(wkt_buf);
      if (OB_FAIL(srs_snapshot->add_pg_reserved_srs_item(LAEA, id))) {
        LOG_WARN("failed to parse pg reserved srs item", K(ret), K(id));
      }
    }
  }
  return ret;
}

}  // omt
}  // oceanbase


// ── share/object obj_cast SRS hook registration(see share/object/ob_obj_cast_hooks.h)──
#include "share/object/ob_obj_cast_hooks.h"
namespace oceanbase {
namespace omt {
static int obj_cast_get_srs_item_impl(uint64_t srid, const common::ObSrsItem *&srs,
                                      common::ObSrsGuardErased &guard)
{
  int ret = common::OB_SUCCESS;
  ObSrsCacheGuard *g = new (std::nothrow) ObSrsCacheGuard();
  if (nullptr == g) {
    ret = common::OB_ALLOCATE_MEMORY_FAILED;
  } else if (common::OB_SUCCESS != (ret = SRS_SERVICE->get_srs_guard(*g))) {
    delete g;
  } else if (common::OB_SUCCESS != (ret = g->get_srs_item(srid, srs))) {
    delete g;
  } else {
    guard.impl_ = g;
    guard.release_ = [](void *p) { delete static_cast<ObSrsCacheGuard *>(p); };
  }
  return ret;
}
static const bool g_reg_obj_cast_srs_hook =
    (common::g_obj_cast_get_srs_item = obj_cast_get_srs_item_impl, true);
}  // namespace omt
}  // namespace oceanbase
