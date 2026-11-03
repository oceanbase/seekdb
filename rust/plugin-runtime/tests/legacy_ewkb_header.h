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

// Frozen reference codec used only by the GIS plugin differential test.
#include "share/geo/ob_geo_utils.h"
namespace oceanbase { namespace common {
struct LegacyEwkbHeader {
  static int get_header_info_from_ewkb(const ObString &, ObGeoWkbHeader &);
  static int construct_ewkb_data(ObString &, ObString &);
};
int LegacyEwkbHeader::get_header_info_from_ewkb(const ObString &ewkb,
                                                        ObGeoWkbHeader &header)
{
  int ret = OB_SUCCESS;
  if (ewkb.length() < EWKB_COMMON_WKB_HEADER_LEN) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid ewkb length", K(ewkb.length()));
  } else {
    header.srid_ = 0;
    header.bo_ = static_cast<ObGeoWkbByteOrder>(*(ewkb.ptr()));
    char *ptr = const_cast<char *>(ewkb.ptr() + WKB_GEO_BO_SIZE);
    uint32_t wkb_type = ObGeoWkbByteOrderUtil::read<uint32_t>(ptr, header.bo_);
    if (wkb_type & ObGeoTypeUtil::EWKB_M_FLAG) {
      ret = OB_INVALID_ARGUMENT;
      LOG_WARN("invalid ewkb type, higher than two dimension is not supported", K(ewkb.length()), K(wkb_type));
    } else {
      bool has_srid = wkb_type & ObGeoTypeUtil::EWKB_SRID_FLAG ? true : false;
      if (has_srid && ewkb.length() < (EWKB_WITH_SRID_LEN)) {
        ret = OB_INVALID_ARGUMENT;
        LOG_WARN("invalid ewkb length", K(ewkb.length()), K(wkb_type));
      } else {
        uint32_t offset = wkb_type & ObGeoTypeUtil::EWKB_Z_FLAG ? ObGeoTypeUtil::WKB_3D_TYPE_OFFSET : 0;
        wkb_type &= 0x0FFFFFFF;
        if (wkb_type >= 4000) {
          ret = OB_INVALID_ARGUMENT;
          LOG_WARN("invalid ewkb type", K(ewkb.length()), K(wkb_type));
        } else {
          wkb_type %= 1000;
          if (wkb_type > static_cast<uint32_t>(ObGeoType::GEOMETRYCOLLECTION)) {
            ret = OB_INVALID_ARGUMENT;
            LOG_WARN("invalid ewkb type", K(ewkb.length()), K(wkb_type));
          } else {
            header.type_ = static_cast<ObGeoType>(wkb_type + offset);
          }
        }
        if (OB_SUCC(ret) && has_srid) {
          header.srid_ = ObGeoWkbByteOrderUtil::read<uint32_t>(
              const_cast<char *>(ewkb.ptr() + EWKB_COMMON_WKB_HEADER_LEN), header.bo_);
        }
      }
    }
  }
  return ret;
}

int LegacyEwkbHeader::construct_ewkb_data(ObString &ewkb,
                                                  ObString &ewkb_data)
{
  int ret = OB_SUCCESS;
  ObGeoWkbHeader header;
  if (ewkb.length() < EWKB_COMMON_WKB_HEADER_LEN) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid ewkb length", K(ewkb.length()));
  } else if (OB_FAIL(get_header_info_from_ewkb(ewkb, header))) {
  } else {
    char *ptr = const_cast<char *>(ewkb.ptr() + WKB_GEO_BO_SIZE);
    uint32_t wkb_type = ObGeoWkbByteOrderUtil::read<uint32_t>(ptr, header.bo_);
    bool has_srid = wkb_type & ObGeoTypeUtil::EWKB_SRID_FLAG ? true : false;
    if (has_srid) {
      // get srid, [bo][type][srid] -> [**][bo][type]
      ObGeoWkbByteOrderUtil::write<uint32_t>(
          const_cast<char *>(ewkb.ptr() + EWKB_COMMON_WKB_HEADER_LEN),
          static_cast<uint32_t>(header.type_), header.bo_);
      *const_cast<uint8_t *>(reinterpret_cast<const uint8_t *>(ewkb.ptr() + WKB_GEO_SRID_SIZE))
          = static_cast<uint8_t>(header.bo_);
      ewkb_data.assign_ptr(ewkb.ptr() + WKB_GEO_SRID_SIZE, ewkb.length() - WKB_GEO_SRID_SIZE);
    } else {
      ObGeoWkbByteOrderUtil::write<uint32_t>(const_cast<char *>(ewkb.ptr() + WKB_GEO_BO_SIZE),
          static_cast<uint32_t>(header.type_), header.bo_);
      ewkb_data.assign_ptr(ewkb.ptr(), ewkb.length());
    }
  }
  return ret;
}

}}
