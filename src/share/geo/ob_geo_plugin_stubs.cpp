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

uint32_t ObSrsItem::get_srid() const
{
  return (srs_info_ == nullptr) ? 0 : srs_info_->get_srid();
}

int ObSrsWktParser::parse_srs_wkt(common::ObIAllocator &, uint64_t,
                                  const common::ObString &,
                                  ObSpatialReferenceSystemBase *&srs)
{
  srs = nullptr;
  return OB_NOT_SUPPORTED;
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

int ObGeoTypeUtil::get_pg_reserved_prj4text(ObIAllocator *, uint32_t, ObString &result)
{
  result.reset();
  return OB_NOT_SUPPORTED;
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
