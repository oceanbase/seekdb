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
#include "seekdb/geo/srs_wkt_grammar.hpp"
#include "share/geo/ob_srs_wkt_parser.h"

using namespace boost::spirit;
using namespace oceanbase::common;

BOOST_FUSION_ADAPT_STRUCT(ObGeographicRs,
                          (ObString, rs_name)
                          (ObRsDatum, datum_info)
                          (ObPrimem, primem)
                          (ObRsUnit, unit)
                          (ObRsAxisPair, axis)
                          (ObRsAuthority, authority))

BOOST_FUSION_ADAPT_STRUCT(ObRsDatum,
                          (ObString, name) 
                          (ObSpheroid, spheroid) 
                          (ObTowgs84, towgs84) 
                          (ObRsAuthority, authority))

BOOST_FUSION_ADAPT_STRUCT(ObSpheroid,
                          (ObString, name)
                          (double, semi_major_axis)
                          (double, inverse_flattening)
                          (ObRsAuthority, authority))

BOOST_FUSION_ADAPT_STRUCT(ObRsAuthority,
                          (bool, is_valid)
                          (ObString, org_name)
                          (ObString, org_code))

BOOST_FUSION_ADAPT_STRUCT(ObTowgs84,
                          (bool, is_valid)
                          (double, value[0])
                          (double, value[1])
                          (double, value[2])
                          (double, value[3])
                          (double, value[4])
                          (double, value[5])
                          (double, value[6]))

BOOST_FUSION_ADAPT_STRUCT(ObRsAxisPair,
                          (ObRsAxis, x)
                          (ObRsAxis, y))

BOOST_FUSION_ADAPT_STRUCT(ObRsAxis,
                          (ObString, name)
                          (ObAxisDirection, direction))

BOOST_FUSION_ADAPT_STRUCT(ObPrimem,
                          (ObString, name)
                          (double, longtitude)
                          (ObRsAuthority, authority))

BOOST_FUSION_ADAPT_STRUCT(ObRsUnit,
                          (ObString, type)
                          (double, conversion_factor)
                          (ObRsAuthority, authority))

BOOST_FUSION_ADAPT_STRUCT(ObProjection,
                          (ObString, name)
                          (ObRsAuthority, authority))

BOOST_FUSION_ADAPT_STRUCT(ObProjectionPram,
                          (ObString, name)
                          (double, value)
                          (ObRsAuthority, authority))

BOOST_FUSION_ADAPT_STRUCT(ObProjectionRs,
                          (ObString, rs_name)
                          (ObGeographicRs, projected_rs)
                          (ObProjection, projection)
                          (ObProjectionPrams, proj_params)
                          (ObRsUnit, unit)
                          (ObRsAxisPair, axis)
                          (ObRsAuthority, authority))


namespace boost
{
namespace spirit
{
namespace traits
{

template<>
struct is_container<ObProjectionPrams> : boost::mpl::true_{};

template<>
struct container_value<ObProjectionPrams>
{
    typedef ObProjectionPram type;
};

template<>
struct push_back_container<ObProjectionPrams, ObProjectionPram>
{
    static bool call(ObProjectionPrams& c, ObProjectionPram val)
    {
        if (c.vals.push_back(val) != OB_SUCCESS) throw std::bad_alloc{};
        return true;
    }
};


}
}
}

namespace oceanbase
{
namespace common
{

typedef boost::variant<ObGeographicRs, ObProjectionRs> ObGeoRs;

struct CoreSrsModel {
  using Geographic = ObGeographicRs;
  using Projected = ObProjectionRs;
  using CoordinateSystem = ObGeoRs;
  using Datum = ObRsDatum;
  using Spheroid = ObSpheroid;
  using Authority = ObRsAuthority;
  using Towgs84 = ObTowgs84;
  using AxisPair = ObRsAxisPair;
  using Axis = ObRsAxis;
  using PrimeMeridian = ObPrimem;
  using Unit = ObRsUnit;
  using Parameters = ObProjectionPrams;
  using Parameter = ObProjectionPram;
  using Projection = ObProjection;
  using Direction = ObAxisDirection;
  using String = ObString;
  ObIAllocator &allocator;
  bool allocation_failed = false;
  bool assign(ObString &out, const std::string &value)
  {
    if (value.empty()) { out.reset(); return true; }
    void *buffer = allocator.alloc(value.size());
    if (buffer == nullptr) { allocation_failed = true; return false; }
    MEMCPY(buffer, value.data(), value.size());
    out.assign_ptr(static_cast<char *>(buffer), value.size());
    return true;
  }
};

static int parse_coordinate_system(ObIAllocator &allocator, const ObString &text, ObGeoRs &rs)
try {
  if (text.ptr() == nullptr || text.length() <= 0) return OB_ERR_PARSER_SYNTAX;
  CoreSrsModel model{allocator};
  const bool parsed = seekdb::geo::srs::parse_wkt(
      std::string_view(text.ptr(), text.length()), model, rs);
  return model.allocation_failed ? OB_ALLOCATE_MEMORY_FAILED : parsed ? OB_SUCCESS : OB_ERR_PARSER_SYNTAX;
}
catch (const std::bad_alloc &) { return OB_ALLOCATE_MEMORY_FAILED; }

int ObSrsWktParser::parse_srs_wkt(common::ObIAllocator &allocator, uint64_t srid,
                                  const common::ObString &srs_str,
                                  ObSpatialReferenceSystemBase *&srs) {
  int ret = OB_SUCCESS;
  ObGeoRs geo_rs; 
  ObSpatialReferenceSystemBase *tmp_result;
  ObGeographicRs *geog_rs = NULL; 
  ObProjectionRs *proj_rs = NULL;

  if (srs_str.empty()) {
    ret = OB_ERR_UNEXPECTED;
  } else if (OB_FAIL(parse_coordinate_system(allocator, srs_str, geo_rs))) {
  } else if (OB_NOT_NULL(geog_rs = boost::get<ObGeographicRs>(&geo_rs))) {
    if (OB_FAIL(ObSpatialReferenceSystemBase::create_geographic_srs(&allocator, srid, geog_rs, tmp_result))) {
    } else {
      srs = tmp_result;
    }
  } else if (OB_NOT_NULL(proj_rs = boost::get<ObProjectionRs>(&geo_rs))) {
    if (OB_FAIL(ObSpatialReferenceSystemBase::create_project_srs(&allocator, srid, proj_rs, tmp_result))) {
    } else {
      srs = tmp_result;
    }
  } else {
    ret = OB_ERR_UNEXPECTED;
  }

  return ret;
}

int ObSrsWktParser::parse_geog_srs_wkt(common::ObIAllocator& allocator, const common::ObString &srs_str, ObGeographicRs &result) {
  int ret = OB_SUCCESS;
  ObGeoRs geo_rs; 
  ObGeographicRs *geog_rs; 
  if (OB_FAIL(parse_coordinate_system(allocator, srs_str, geo_rs))) {
  } else if (OB_ISNULL(geog_rs = boost::get<ObGeographicRs>(&geo_rs))) {
    ret = OB_ERR_UNEXPECTED;
  } else {
    result = *geog_rs;
  }
  return ret;
}

int ObSrsWktParser::parse_proj_srs_wkt(common::ObIAllocator& allocator, const common::ObString &srs_str, ObProjectionRs &result) try {
  int ret = OB_SUCCESS;
  ObGeoRs geo_rs; 
  ObProjectionRs *proj_rs; 
  if (OB_FAIL(parse_coordinate_system(allocator, srs_str, geo_rs))) {
  } else if (OB_ISNULL(proj_rs = boost::get<ObProjectionRs>(&geo_rs))) {
    ret = OB_ERR_UNEXPECTED;
  } else {
    result = *proj_rs;
  }
  return ret;
}
catch (const std::bad_alloc &) { return OB_ALLOCATE_MEMORY_FAILED; }

} // common
} // oceanbase
