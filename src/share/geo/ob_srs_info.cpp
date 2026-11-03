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
#include "ob_srs_info.h"
#include "share/geo/srs_metadata_adapter.h"
#include "seekdb/geo/srs_projection_parameters.hpp"
#include "seekdb/geo/srs_semantics.hpp"
#include "share/geo/ob_geo_common.h"
#include "common/mysqlclient/ob_mysql_global.h"
#include "lib/charset/ob_dtoa.h"

namespace oceanbase
{
namespace common
{

int ObSrsUtils::check_authority(const ObRsAuthority& auth, const char *target_auth_name, int target_auth_code, bool allow_invalid, bool &res)
{
  int ret = OB_SUCCESS;
  res = true;
  if (!auth.is_valid) {
    if (!allow_invalid) {
      res = false; 
    }
  } else {
    int code = ObCharset::strntoll(auth.org_code.ptr(), auth.org_code.length(), 10, &ret);
    if (OB_FAIL(ret)) {
      res = false;
    } else if (auth.org_name.case_compare(target_auth_name) || code != target_auth_code) {
      res = false;
    }
  }
  return ret;
}

int ObSrsUtils::check_is_wgs84(const ObGeographicRs *rs, bool &is_wgs84)
{
  if (rs == nullptr) return OB_INVALID_ARGUMENT;
  return seekdb::geo::srs::is_wgs84(*rs, ObSrsUtils::check_authority, is_wgs84);
}

// todo@dazhi: compare the param_name and param_alias when epsg isnot comparable ?
int ObSrsUtils::get_simple_proj_params(const ObProjectionPrams &parsed_params,
                                       ObVector<ObSimpleProjPram, common::ObFIFOAllocator> &params)
{
  int ret = OB_SUCCESS;
  FOREACH_X(parsed_param, parsed_params.vals, OB_SUCC(ret)) {
    if (parsed_param->authority.org_name.case_compare("EPSG") == 0) {
      FOREACH(param, params) {
        ObString epsg_code_str = parsed_param->authority.org_code;
        int epsg_code = ObCharset::strntoll(epsg_code_str.ptr(), epsg_code_str.length(), 10, &ret);
        if (OB_FAIL(ret)) {
        } else if (epsg_code == param->epsg_code_) {
          param->value_ = parsed_param->value;
          break;
        }
      }
    }
  }
  FOREACH_X(param, params, OB_SUCC(ret)) {
    if (std::isnan(param->value_)) {
      int epsg_code = param->epsg_code_;
      ret = OB_ERR_UNEXPECTED; // todo@dazhi: ER_SRS_PROJ_PARAMETER_MISSING
    }
  }
  return ret;
}

int ObSpatialReferenceSystemBase::create_project_srs(ObIAllocator* allocator, uint64_t srs_id,
                                                     const ObProjectionRs *rs, ObSpatialReferenceSystemBase *&srs_info)
{
  int ret = OB_SUCCESS;
  int epsg_code = 0;
  if (OB_ISNULL(rs) || OB_ISNULL(allocator)) {
    ret = OB_ERR_NULL_VALUE;
  } else {
    ObString epsg_code_str = rs->projection.authority.org_code;
    epsg_code = ObCharset::strntoll(epsg_code_str.ptr(), epsg_code_str.length(), 10, &ret);
  }

  if (OB_SUCC(ret)) {
    switch (epsg_code) {
      case static_cast<int>(ObProjectionType::POPULAR_VISUAL_PSEUDO_MERCATOR) : {
        ret = create_srs_internal<ObPopularVisualPseudoMercatorSrs>(allocator, srs_id, rs, srs_info);
        break;
      }
      case static_cast<int>(ObProjectionType::LAMBERT_AZIMUTHAL_EQUAL_AREA_SPHERICAL) : {
        ret = create_srs_internal<ObLambertAzimuthalEqualAreaSphericalSrs>(allocator, srs_id, rs, srs_info);
        break;
      }
      case static_cast<int>(ObProjectionType::EQUIDISTANT_CYLINDRICAL) : {
        ret = create_srs_internal<ObEquidistantCylindricalSrs>(allocator, srs_id, rs, srs_info);
        break;
      }
      case static_cast<int>(ObProjectionType::EQUIDISTANT_CYLINDRICAL_SPHERICAL) : {
        ret = create_srs_internal<ObEquidistantCylindricalSphericalSrs>(allocator, srs_id, rs, srs_info);
        break;
      }
      case static_cast<int>(ObProjectionType::KROVAK_NORTH_ORIENTATED) : {
        ret = create_srs_internal<ObKrovakNorthOrientatedSrs>(allocator, srs_id, rs, srs_info);
        break;
      }
      case static_cast<int>(ObProjectionType::KROVAK_MODIFIED) : {
        ret = create_srs_internal<ObKrovakModifiedSrs>(allocator, srs_id, rs, srs_info);
        break;
      }
      case static_cast<int>(ObProjectionType::KROVAK_MODIFIED_NORTH_ORIENTATED) : {
        ret = create_srs_internal<ObKrovakModifiedNorthOrientatedSrs>(allocator, srs_id, rs, srs_info);
        break;
      }
      case static_cast<int>(ObProjectionType::LAMBERT_CONIC_CONFORMAL_2SP_MICHIGAN) : {
        ret = create_srs_internal<ObLambertConicConformal2SPMichiganSrs>(allocator, srs_id, rs, srs_info);
        break;
      }
      case static_cast<int>(ObProjectionType::COLOMBIA_URBAN) : {
        ret = create_srs_internal<ObColombiaUrbanSrs>(allocator, srs_id, rs, srs_info);
        break;
      }
      case static_cast<int>(ObProjectionType::LAMBERT_CONIC_CONFORMAL_1SP) : {
        ret = create_srs_internal<ObLambertConicConformal1SPSrs>(allocator, srs_id, rs, srs_info);
        break;
      }
      case static_cast<int>(ObProjectionType::LAMBERT_CONIC_CONFORMAL_2SP) : {
        ret = create_srs_internal<ObLambertConicConformal2SPSrs>(allocator, srs_id, rs, srs_info);
        break;
      }
      case static_cast<int>(ObProjectionType::LAMBERT_CONIC_CONFORMAL_2SP_BELGIUM) : {
        ret = create_srs_internal<ObLambertConicConformal2SPBelgiumSrs>(allocator, srs_id, rs, srs_info);
        break;
      }
      case static_cast<int>(ObProjectionType::MERCATOR_VARIANT_A) : {
        ret = create_srs_internal<ObMercatorvariantASrs>(allocator, srs_id, rs, srs_info);
        break;
      }
      case static_cast<int>(ObProjectionType::MERCATOR_VARIANT_B) : {
        ret = create_srs_internal<ObMercatorvariantBSrs>(allocator, srs_id, rs, srs_info);
        break;
      }
      case static_cast<int>(ObProjectionType::CASSINI_SOLDNER) : {
        ret = create_srs_internal<ObCassiniSoldnerSrs>(allocator, srs_id, rs, srs_info);
        break;
      }
      case static_cast<int>(ObProjectionType::TRANSVERSE_MERCATOR) : {
        ret = create_srs_internal<ObTransverseMercatorSrs>(allocator, srs_id, rs, srs_info);
        break;
      }
      case static_cast<int>(ObProjectionType::TRANSVERSE_MERCATOR_SOUTH_ORIENTATED) : {
        ret = create_srs_internal<ObTransverseMercatorSouthOrientatedSrs>(allocator, srs_id, rs, srs_info);
        break;
      }
      case static_cast<int>(ObProjectionType::OBLIQUE_STEREOGRAPHIC) : {
        ret = create_srs_internal<ObObliqueStereographicSrs>(allocator, srs_id, rs, srs_info);
        break;
      }
      case static_cast<int>(ObProjectionType::POLAR_STEREOGRAPHIC_VARIANT_A) : {
        ret = create_srs_internal<ObPolarStereographicVariantASrs>(allocator, srs_id, rs, srs_info);
        break;
      }
      case static_cast<int>(ObProjectionType::NEW_ZEALAND_MAP_GRID) : {
        ret = create_srs_internal<ObNewZealandMapGridSrs>(allocator, srs_id, rs, srs_info);
        break;
      }
      case static_cast<int>(ObProjectionType::HOTINE_OBLIQUE_MERCATOR_VARIANT_A) : {
        ret = create_srs_internal<ObHotineObliqueMercatorvariantASrs>(allocator, srs_id, rs, srs_info);
        break;
      }
      case static_cast<int>(ObProjectionType::LABORDE_OBLIQUE_MERCATOR) : {
        ret = create_srs_internal<ObLabordeObliqueMercatorSrs>(allocator, srs_id, rs, srs_info);
        break;
      }
      case static_cast<int>(ObProjectionType::HOTINE_OBLIQUE_MERCATOR_VARIANT_B) : {
        ret = create_srs_internal<ObHotineObliqueMercatorVariantBSrs>(allocator, srs_id, rs, srs_info);
        break;
      }
      case static_cast<int>(ObProjectionType::TUNISIA_MINING_GRID) : {
        ret = create_srs_internal<ObTunisiaMiningGridSrs>(allocator, srs_id, rs, srs_info);
        break;
      }
      case static_cast<int>(ObProjectionType::LAMBERT_CONIC_NEAR_CONFORMAL) : {
        ret = create_srs_internal<ObLambertConicNearConformalSrs>(allocator, srs_id, rs, srs_info);
        break;
      }
      case static_cast<int>(ObProjectionType::AMERICAN_POLYCONIC) : {
        ret = create_srs_internal<ObAmericanPolyconicSrs>(allocator, srs_id, rs, srs_info);
        break;
      }
      case static_cast<int>(ObProjectionType::KROVAK) : {
        ret = create_srs_internal<ObKrovakSrs>(allocator, srs_id, rs, srs_info);
        break;
      }
      case static_cast<int>(ObProjectionType::LAMBERT_AZIMUTHAL_EQUAL_AREA) : {
        ret = create_srs_internal<ObLambertAzimuthalEqualAreaSrs>(allocator, srs_id, rs, srs_info);
        break;
      }
      case static_cast<int>(ObProjectionType::ALBERS_EQUAL_AREA) : {
        ret = create_srs_internal<ObAlbersEqualAreaSrs>(allocator, srs_id, rs, srs_info);
        break;
      }      
      case static_cast<int>(ObProjectionType::TRANSVERSE_MERCATOR_ZONED_GRID_SYSTEM) : {
        ret = create_srs_internal<ObTransverseMercatorZonedGridSystemSrs>(allocator, srs_id, rs, srs_info);
        break;
      }
      case static_cast<int>(ObProjectionType::LAMBERT_CONIC_CONFORMAL_WEST_ORIENTATED) : {
        ret = create_srs_internal<ObLambertConicConformalWestOrientatedSrs>(allocator, srs_id, rs, srs_info);
        break;
      }
      case static_cast<int>(ObProjectionType::BONNE_SOUTH_ORIENTATED) : {
        ret = create_srs_internal<ObBonneSouthOrientatedSrs>(allocator, srs_id, rs, srs_info);
        break;
      }
      case static_cast<int>(ObProjectionType::POLAR_STEREOGRAPHIC_VARIANT_B) : {
        ret = create_srs_internal<ObPolarStereographicVariantBSrs>(allocator, srs_id, rs, srs_info);
        break;
      }
      case static_cast<int>(ObProjectionType::POLAR_STEREOGRAPHIC_VARIANT_C) : {
        ret = create_srs_internal<ObPolarStereographicVariantCSrs>(allocator, srs_id, rs, srs_info);
        break;
      }
      case static_cast<int>(ObProjectionType::GUAM_PROJECTION) : {
        ret = create_srs_internal<ObGuamProjectionSrs>(allocator, srs_id, rs, srs_info);
        break;
      }
      case static_cast<int>(ObProjectionType::MODIFIED_AZIMUTHAL_EQUIDISTANT) : {
        ret = create_srs_internal<ObModifiedAzimuthalEquidistantSrs>(allocator, srs_id, rs, srs_info);
        break;
      }
      case static_cast<int>(ObProjectionType::HYPERBOLIC_CASSINI_SOLDNER) : {
        ret = create_srs_internal<ObHyperbolicCassiniSoldnerSrs>(allocator, srs_id, rs, srs_info);
        break;
      }
      case static_cast<int>(ObProjectionType::LAMBERT_CYLINDRICAL_EQUAL_AREA_SPHERICAL) : {
        ret = create_srs_internal<ObLambertCylindricalEqualAreaSphericalSrs>(allocator, srs_id, rs, srs_info);
        break;
      }
      case static_cast<int>(ObProjectionType::LAMBERT_CYLINDRICAL_EQUAL_AREA) : {
        ret = create_srs_internal<ObLambertCylindricalEqualAreaSrs>(allocator, srs_id, rs, srs_info);
        break;
      }
      default: {
        ret = create_srs_internal<ObUnknownProjectedSrs>(allocator, srs_id, rs, srs_info);
        break;
      }
    }
  }

  if (OB_FAIL(ret)) {
  }

  return ret;
}

template <typename SRS_T, typename RS_T>
int ObSpatialReferenceSystemBase::create_srs_internal(ObIAllocator* allocator, uint64_t srs_id,
                                                      const RS_T *rs, ObSpatialReferenceSystemBase *&srs_info)
{
  int ret = OB_SUCCESS;
  SRS_T *tmp_srs_info = NULL;
  void *buf = allocator->alloc(sizeof(SRS_T));
  if (OB_ISNULL(buf)) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
  } else {
    tmp_srs_info = new(buf)SRS_T(static_cast<common::ObIAllocator*>(allocator));
    if (OB_FAIL(tmp_srs_info->init(srs_id, rs))) {
    } else {
      srs_info = tmp_srs_info;
    }
  }

  if (OB_FAIL(ret) && tmp_srs_info != NULL) {
    allocator->free(tmp_srs_info);
  }
  return ret;
}

int ObSpatialReferenceSystemBase::create_geographic_srs(ObIAllocator* allocator, uint64_t srs_id,
                                                        const ObGeographicRs *rs, ObSpatialReferenceSystemBase *&srs_info)
{
  return create_srs_internal<ObGeographicSrs, ObGeographicRs>(allocator, srs_id, rs, srs_info);
}

ObGeographicSrs::ObGeographicSrs(common::ObIAllocator* alloc)
  : semi_major_axis_(NAN), inverse_flattening_(NAN),
    is_wgs84_(false), prime_meridian_(NAN), angular_factor_(NAN), bounds_info_(),
    proj4text_()
{ 
  for (uint8_t i = 0; i < WGS84_PARA_NUM; i++) {
    wgs84_[i] = NAN;
  }
  for (uint8_t i = 0; i < AXIS_DIRECTION_NUM; i++) {
    axis_dir_[i] = ObAxisDirection::INIT;
  }
}

int ObGeographicSrs::init(uint32_t srs_id, const ObGeographicRs *rs)
{
  int ret = OB_SUCCESS;

  id_ = srs_id;
  semi_major_axis_ = rs->datum_info.spheroid.semi_major_axis;
  inverse_flattening_ = rs->datum_info.spheroid.inverse_flattening;
  prime_meridian_ = rs->primem.longtitude;
  angular_factor_ = rs->unit.conversion_factor;

  if (!seekdb::geo::srs::valid_geographic(id_, semi_major_axis_, inverse_flattening_, prime_meridian_, angular_factor_)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid srs value", K(id_), K(semi_major_axis_), K(inverse_flattening_), K(prime_meridian_), K(angular_factor_));
  } else {
    axis_dir_[0] = rs->axis.x.direction;
    axis_dir_[1] = rs->axis.y.direction;
    if (rs->datum_info.towgs84.is_valid) {
      for (uint8_t i = 0; i < WGS84_PARA_NUM && OB_SUCC(ret); i++) {
        wgs84_[i] = rs->datum_info.towgs84.value[i];
        if (std::isinf(wgs84_[i])) {
          ret = OB_INVALID_ARGUMENT;
          LOG_WARN("invalid wgs84 value", K(wgs84_[i]));
        }
      }
    }
  }

  if (OB_SUCC(ret) && OB_FAIL(ObSrsUtils::check_is_wgs84(rs, is_wgs84_))) {
  }

  return ret;
}

int ObGeographicSrs::get_proj4_param(ObIAllocator *allocator, ObString &proj4_param) const
{
  if (allocator == nullptr) return OB_INVALID_ARGUMENT;
  return build_srs_geographic_proj4(*allocator, semi_major_axis_, inverse_flattening_, is_wgs84_, wgs84_, proj4_param);
}


int ObProjectedSrs::register_proj_params()
{
  simple_proj_prams_.reset();
  const auto *schema = seekdb::geo::srs::find_projection(static_cast<int>(get_projection_type()));
  int ret = OB_SUCCESS;
  if (schema != nullptr) {
    for (size_t i = 0; OB_SUCC(ret) && i < schema->count; ++i) {
      if (OB_FAIL(simple_proj_prams_.push_back(schema->codes[i]))) {}
    }
  }
  return ret;
}

int ObProjectedSrs::init(uint64_t srs_id,  const ObProjectionRs *rs)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(geographic_srs_.init(srs_id, &(rs->projected_rs)))) {
  } else if (std::isnan(rs->unit.conversion_factor)){
    ret = OB_INVALID_ARGUMENT;
  } else {
    id_ = srs_id;
    linear_unit_ = rs->unit.conversion_factor;
    axis_dir_[0] = rs->axis.x.direction;
    axis_dir_[1] = rs->axis.y.direction;
    if ((axis_dir_[0] == ObAxisDirection::INIT) ^
        (axis_dir_[1] == ObAxisDirection::INIT)) {
      ret = OB_INVALID_ARGUMENT;
    } else if (OB_FAIL(register_proj_params())) {
    } else if (simple_proj_prams_.size() > 0 &&
               OB_FAIL(ObSrsUtils::get_simple_proj_params(rs->proj_params, simple_proj_prams_))) {
    }
  }
  return ret;
}

int ObProjectedSrs::get_proj4_param(ObIAllocator *allocator, ObString &proj4_param) const
{
  UNUSEDx(allocator, proj4_param);
  return OB_SUCCESS;
}

void ObGeographicSrs::set_bounds(double min_x, double min_y, double max_x, double max_y)
{
  bounds_info_.minX_ = min_x;
  bounds_info_.minY_ = min_y;
  bounds_info_.maxX_ = max_x;
  bounds_info_.maxY_ = max_y;
}


}  // namespace common
}  // namespace oceanbase
