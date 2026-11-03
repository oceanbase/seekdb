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
#include "share/geo/srs_metadata_adapter.h"
#include "share/geo/ob_srs_info.h"
#include "share/geo/ob_geo_utils.h"
#include "seekdb/geo/srs_semantics.hpp"
#include "lib/charset/ob_dtoa.h"
#include "common/mysqlclient/ob_mysql_global.h"
#include "lib/string/ob_string_buffer.h"
#include <limits>

namespace oceanbase::common {
int build_srs_geographic_proj4(ObIAllocator &allocator, double semi_major, double inverse_flattening,
                               bool wgs84, const double *towgs84, ObString &out)
{
  if (towgs84 == nullptr) return OB_INVALID_ARGUMENT;
  const bool has_towgs84 = !std::isnan(towgs84[0]);
  if (!wgs84 && !has_towgs84) { out.reset(); return OB_SUCCESS; }
  ObStringBuffer buffer(&allocator);
  const auto number = [&](double value) {
    char bytes[FLOATING_POINT_BUFFER]{};
    const size_t length = ob_fcvt(value, std::numeric_limits<double>::max_digits10,
                                  FLOATING_POINT_BUFFER - 1, bytes, nullptr);
    return buffer.append(bytes, length);
  };
  int ret = buffer.append("+proj=lonlat +a=");
  if (ret == OB_SUCCESS) ret = number(semi_major);
  if (ret == OB_SUCCESS) ret = buffer.append(inverse_flattening == 0.0 ? " +b=" : " +rf=");
  // A sphere has b=a, not b=0. The old formatter used inverse_flattening here.
  if (ret == OB_SUCCESS) ret = number(inverse_flattening == 0.0 ? semi_major : inverse_flattening);
  if (ret == OB_SUCCESS) ret = buffer.append(" +towgs84=");
  if (ret == OB_SUCCESS && has_towgs84) {
    for (int i = 0; ret == OB_SUCCESS && i < WGS84_PARA_NUM; ++i) {
      ret = number(towgs84[i]);
      if (ret == OB_SUCCESS && i != WGS84_PARA_NUM - 1) ret = buffer.append(",");
    }
  } else if (ret == OB_SUCCESS) ret = buffer.append("0,0,0,0,0,0,0");
  if (ret == OB_SUCCESS) ret = buffer.append(" +no_defs");
  if (ret == OB_SUCCESS) ret = ob_write_string(allocator, buffer.string(), out, true);
  return ret;
}

namespace {
seekdb::geo::srs::Coordinates coordinates(const ObSpatialReferenceSystemBase &srs)
{
  static_assert(int(ObAxisDirection::EAST) == 1 && int(ObAxisDirection::SOUTH) == 2 &&
                int(ObAxisDirection::WEST) == 3 && int(ObAxisDirection::NORTH) == 4);
  return {srs.srs_type() == ObSrsType::GEOGRAPHIC_SRS, int(srs.axis_direction(0)),
          int(srs.axis_direction(1)), srs.angular_unit(), srs.prime_meridian()};
}
} // namespace

bool ObSrsItem::is_lat_long_order() const
{
  return coordinates(*srs_info_).latitude_first();
}

bool ObSrsItem::is_latitude_north() const
{
  return coordinates(*srs_info_).north();
}

bool ObSrsItem::is_longtitude_east() const
{
  return coordinates(*srs_info_).east();
}

int ObSrsItem::from_radians_to_srs_unit(double radians, double &srs_unit_val) const
{
  return coordinates(*srs_info_).from_radians(radians, srs_unit_val) ? OB_SUCCESS : OB_ERR_UNEXPECTED;
}

int ObSrsItem::get_proj4_param(ObIAllocator *allocator, ObString &proj_param) const
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(srs_info_->get_proj4_param(allocator, proj_param))) {
  } else if (proj_param.empty()) {
    proj_param = get_proj4text();
  }
  return ret;
}


int ObSrsItem::from_srs_unit_to_radians(double unit_value, double &radians) const
{
  return coordinates(*srs_info_).to_radians(unit_value, radians) ? OB_SUCCESS : OB_ERR_UNEXPECTED;
}

int ObSrsItem::latitude_convert_to_radians(double value, double &latitude) const
{
  return coordinates(*srs_info_).latitude_to_radians(value, latitude) ? OB_SUCCESS : OB_ERR_UNEXPECTED;
}

int ObSrsItem::latitude_convert_from_radians(double latitude, double &value) const
{
  return coordinates(*srs_info_).latitude_from_radians(latitude, value) ? OB_SUCCESS : OB_ERR_UNEXPECTED;
}

int ObSrsItem::longtitude_convert_to_radians(double value, double &longtitude) const
{
  return coordinates(*srs_info_).longitude_to_radians(value, longtitude) ? OB_SUCCESS : OB_ERR_UNEXPECTED;
}

int ObSrsItem::longtitude_convert_from_radians(double longtitude, double &value) const
{
  return coordinates(*srs_info_).longitude_from_radians(longtitude, value) ? OB_SUCCESS : OB_ERR_UNEXPECTED;
}

double ObSrsItem::semi_minor_axis() const
{
  return seekdb::geo::srs::semi_minor_axis(is_geographical_srs(), srs_info_->semi_major_axis(),
                                         srs_info_->inverse_flattening());
}

bool ObSrsItem::is_geographical_srs() const
{
  return srs_info_->srs_type() == ObSrsType::GEOGRAPHIC_SRS;
}


uint32_t ObSrsItem::get_srid() const
{
  return srs_info_ == nullptr ? 0 : srs_info_->get_srid();
}

int64_t ObSrsBoundsItem::to_string(char *buf, const int64_t buf_len) const
{
  int64_t pos = 0;
  J_KV(K(minX_), K(minY_), K(maxX_), K(maxY_));
  return pos;
}

int ObGeoTypeUtil::get_pg_reserved_prj4text(ObIAllocator *allocator, uint32_t srid, ObString &prj4_param)
{
  if (allocator == nullptr) return OB_INVALID_ARGUMENT;
  int ret = OB_SUCCESS;
  const uint32_t MAX_PRJ4_LEN = 512;
  char tmp_buf[MAX_PRJ4_LEN] = {0};
  if (srid == SRID_WORLD_MERCATOR_PG) {
    strncpy(tmp_buf, "+proj=merc +lon_0=0 +k=1 +x_0=0 +y_0=0 +ellps=WGS84 +datum=WGS84 +units=m +no_defs",
            MAX_PRJ4_LEN);
  } else if (srid >= SRID_NORTH_UTM_START_PG && srid <= SRID_NORTH_UTM_END_PG) {
    snprintf(tmp_buf, MAX_PRJ4_LEN, "+proj=utm +zone=%d +ellps=WGS84 +datum=WGS84 +units=m +no_defs",
             srid - SRID_NORTH_UTM_START_PG + 1 );
  } else if (srid == SRID_NORTH_LAMBERT_PG) {
		strncpy(tmp_buf, "+proj=laea +lat_0=90 +lon_0=-40 +x_0=0 +y_0=0 +ellps=WGS84 +datum=WGS84 +units=m +no_defs",
            MAX_PRJ4_LEN);
  } else if (srid == SRID_NORTH_STEREO_PG) {
		strncpy(tmp_buf, "+proj=stere +lat_0=90 +lat_ts=71 +lon_0=0 +k=1 +x_0=0 +y_0=0 +ellps=WGS84 +datum=WGS84 +units=m +no_defs",
            MAX_PRJ4_LEN);
  } else if (srid >= SRID_SOUTH_UTM_START_PG &&
            srid <= SRID_SOUTH_UTM_END_PG) {
    snprintf(tmp_buf, MAX_PRJ4_LEN, "+proj=utm +zone=%d +south +ellps=WGS84 +datum=WGS84 +units=m +no_defs",
             srid - SRID_SOUTH_UTM_START_PG + 1 );
  } else if (srid == SRID_SOUTH_LAMBERT_PG) {
		strncpy(tmp_buf, "+proj=laea +lat_0=-90 +lon_0=0 +x_0=0 +y_0=0 +ellps=WGS84 +datum=WGS84 +units=m +no_defs",
            MAX_PRJ4_LEN);
  } else if (srid >= SRID_LAEA_START_PG && srid < SRID_LAEA_END_PG) {
			int zone = srid - SRID_LAEA_START_PG;
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
      }
      if (OB_SUCC(ret)) {
        while (lon_0 > 180) {
          lon_0 -= 360;
        }
        while (lon_0 < -180) {
          lon_0 += 360;
        }
        snprintf(tmp_buf, MAX_PRJ4_LEN, "+proj=laea +ellps=WGS84 +datum=WGS84 +lat_0=%g +lon_0=%g +units=m +no_defs",
                 lat_0, lon_0);
      }
  } else {
    ret = OB_ERR_UNEXPECTED;
  }

  if (OB_SUCC(ret)) {
    ObString prj4_tmp = ObString::make_string(tmp_buf);
    if (OB_FAIL(ob_write_string(*allocator, prj4_tmp, prj4_param, true))) {
    }
  }
  return ret;
}


} // namespace oceanbase::common
