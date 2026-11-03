/*
 * Copyright (c) 2026 OceanBase.
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

/*
 * Core-only GIS ABI shims.  These definitions deliberately do not parse or
 * construct geometry; callers route execution through the GIS plugin SPI.
 */

#include "share/geo/ob_geo_utils.h"
#include "share/geo/ob_geometry_cast.h"
#include "share/geo/ob_geo_common.h"
#include "share/geo/ob_geo_mvt.h"
#include "share/geo/ob_srs_info.h"
#include "share/geo/ob_srs_wkt_parser.h"

#include <cctype>
#include <cstring>

namespace oceanbase
{
namespace common
{

// The lightweight core keeps the public geometry ABI types but does not link
// the legacy codec/SRS implementation.  These small definitions satisfy the
// remaining core ABI references; real geometry work is delegated to the GIS
// plugin through the execution SPI.
template <>
uint32_t ObGeoWkbByteOrderUtil::read<uint32_t>(const char *data, ObGeoWkbByteOrder bo)
{
  uint32_t value = 0;
  if (bo == ObGeoWkbByteOrder::LittleEndian) {
    std::memcpy(&value, data, sizeof(value));
  } else {
    for (int i = 0; i < 4; ++i) {
      reinterpret_cast<char *>(&value)[i] = data[3 - i];
    }
  }
  return value;
}

template <>
double ObGeoWkbByteOrderUtil::read<double>(const char *data, ObGeoWkbByteOrder bo)
{
  double value = 0.0;
  if (bo == ObGeoWkbByteOrder::LittleEndian) {
    std::memcpy(&value, data, sizeof(value));
  } else {
    for (int i = 0; i < 8; ++i) {
      reinterpret_cast<char *>(&value)[i] = data[7 - i];
    }
  }
  return value;
}

double ObGeoWkbByteOrderUtil::read_double(const char *data, ObGeoWkbByteOrder bo)
{
  return ObGeoWkbByteOrderUtil::read<double>(data, bo);
}

int ObGeoTypeUtil::get_srid_from_wkb(const ObString &value, uint32_t &srid)
{
  // Core-GIS-off SQL values carry the plugin's geometry envelope v1:
  // little-endian SRID, version byte 1, then WKB. Only inspect the routing
  // header here; the leased plugin validates the geometry before index rows
  // can be emitted. This is not the legacy SWKB version marker (0x41).
  if (value.ptr() == nullptr || value.length() < 10) return OB_ERR_GIS_INVALID_DATA;
  const auto *bytes = reinterpret_cast<const uint8_t *>(value.ptr());
  if (bytes[4] != 1 || bytes[5] > 1) return OB_ERR_GIS_INVALID_DATA;
  uint32_t result = 0;
  for (unsigned i = 0; i < 4; ++i) result |= uint32_t(bytes[i]) << (8 * i);
  srid = result;
  return OB_SUCCESS;
}

int ObGeoTypeUtil::get_type_from_wkb(const ObString &value, ObGeoType &type)
{
  uint32_t srid = 0;
  const int ret = get_srid_from_wkb(value, srid);
  if (ret != OB_SUCCESS) return ret;
  const auto *bytes = reinterpret_cast<const uint8_t *>(value.ptr());
  uint32_t encoded = 0;
  for (unsigned i = 0; i < 4; ++i) {
    encoded |= uint32_t(bytes[6 + i]) << (8 * (bytes[5] == 1 ? i : 3 - i));
  }
  if (!((encoded >= 1 && encoded <= 7) || (encoded >= 1001 && encoded <= 1007))) {
    return OB_ERR_GIS_INVALID_DATA;
  }
  // Routing only. Full payload validation remains in the leased GIS service.
  type = static_cast<ObGeoType>(encoded);
  return OB_SUCCESS;
}

int mvt_agg_result::init_layer()
{
  return OB_NOT_SUPPORTED;
}

int mvt_agg_result::generate_feature(ObObj *, uint32_t)
{
  return OB_NOT_SUPPORTED;
}

int mvt_agg_result::mvt_pack(ObString &result)
{
  result.reset();
  return OB_NOT_SUPPORTED;
}

bool mvt_agg_result::is_upper_char_exist(const ObString &str)
{
  for (int32_t i = 0; i < str.length(); ++i) {
    if (isupper(static_cast<unsigned char>(str.ptr()[i]))) {
      return true;
    }
  }
  return false;
}

int ObGeoTypeUtil::create_geo_by_type(ObIAllocator &, ObGeoType, bool, bool,
                                      ObGeometry *&geo, uint32_t)
{
  geo = nullptr;
  return OB_NOT_SUPPORTED;
}

int ObGeoTypeUtil::build_geometry(ObIAllocator &, const ObString &, ObGeometry *&geo,
                                  const ObSrsItem *, ObGeoErrLogInfo &, uint8_t)
{
  geo = nullptr;
  return OB_NOT_SUPPORTED;
}

int ObGeoTypeUtil::add_geo_version(ObIAllocator &, const ObString &, ObString &result)
{
  result.reset();
  return OB_NOT_SUPPORTED;
}

int ObGeoTypeUtil::to_wkb(ObIAllocator &, ObGeometry &, const ObSrsItem *, ObString &, bool)
{
  return OB_NOT_SUPPORTED;
}

ObGeoType ObGeoTypeUtil::get_geo_type_by_name(ObString &)
{
  return ObGeoType::GEOMETRY;
}

const char *ObGeoTypeUtil::get_geo_name_by_type(ObGeoType)
{
  return "geometry";
}

int ObGeometryTypeCastUtil::get_tree(ObIAllocator &, const ObString &, ObGeometry *&geo_tree,
                                     const ObSrsItem *, ObGeoErrLogInfo &, const char *)
{
  geo_tree = nullptr;
  return OB_NOT_SUPPORTED;
}

const char *ObGeometryTypeCastUtil::get_cast_name(ObGeoType)
{
  return "geometry";
}

int ObGeometryTypeCastFactory::alloc(ObIAllocator &, ObGeoType, ObGeometryTypeCast *&geo_cast)
{
  geo_cast = nullptr;
  return OB_NOT_SUPPORTED;
}

} // namespace common
} // namespace oceanbase
