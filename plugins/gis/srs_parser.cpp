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
#include "srs_parser.h"
#include "seekdb/geo/srs_wkt_grammar.hpp"

BOOST_FUSION_ADAPT_STRUCT(seekdb::gis::srs::Geographic,
                          (std::string, rs_name)
                          (seekdb::gis::srs::Datum, datum_info)
                          (seekdb::gis::srs::PrimeMeridian, primem)
                          (seekdb::gis::srs::Unit, unit)
                          (seekdb::gis::srs::AxisPair, axis)
                          (seekdb::gis::srs::Authority, authority))

BOOST_FUSION_ADAPT_STRUCT(seekdb::gis::srs::Datum,
                          (std::string, name) 
                          (seekdb::gis::srs::Spheroid, spheroid) 
                          (seekdb::gis::srs::Towgs84, towgs84) 
                          (seekdb::gis::srs::Authority, authority))

BOOST_FUSION_ADAPT_STRUCT(seekdb::gis::srs::Spheroid,
                          (std::string, name)
                          (double, semi_major_axis)
                          (double, inverse_flattening)
                          (seekdb::gis::srs::Authority, authority))

BOOST_FUSION_ADAPT_STRUCT(seekdb::gis::srs::Authority,
                          (bool, is_valid)
                          (std::string, org_name)
                          (std::string, org_code))

BOOST_FUSION_ADAPT_STRUCT(seekdb::gis::srs::Towgs84,
                          (bool, is_valid)
                          (double, value[0])
                          (double, value[1])
                          (double, value[2])
                          (double, value[3])
                          (double, value[4])
                          (double, value[5])
                          (double, value[6]))

BOOST_FUSION_ADAPT_STRUCT(seekdb::gis::srs::AxisPair,
                          (seekdb::gis::srs::Axis, x)
                          (seekdb::gis::srs::Axis, y))

BOOST_FUSION_ADAPT_STRUCT(seekdb::gis::srs::Axis,
                          (std::string, name)
                          (seekdb::gis::srs::Direction, direction))

BOOST_FUSION_ADAPT_STRUCT(seekdb::gis::srs::PrimeMeridian,
                          (std::string, name)
                          (double, longtitude)
                          (seekdb::gis::srs::Authority, authority))

BOOST_FUSION_ADAPT_STRUCT(seekdb::gis::srs::Unit,
                          (std::string, type)
                          (double, conversion_factor)
                          (seekdb::gis::srs::Authority, authority))

BOOST_FUSION_ADAPT_STRUCT(seekdb::gis::srs::Projection,
                          (std::string, name)
                          (seekdb::gis::srs::Authority, authority))

BOOST_FUSION_ADAPT_STRUCT(seekdb::gis::srs::Parameter,
                          (std::string, name)
                          (double, value)
                          (seekdb::gis::srs::Authority, authority))

BOOST_FUSION_ADAPT_STRUCT(seekdb::gis::srs::Projected,
                          (std::string, rs_name)
                          (seekdb::gis::srs::Geographic, projected_rs)
                          (seekdb::gis::srs::Projection, projection)
                          (seekdb::gis::srs::Parameters, proj_params)
                          (seekdb::gis::srs::Unit, unit)
                          (seekdb::gis::srs::AxisPair, axis)
                          (seekdb::gis::srs::Authority, authority))


namespace seekdb::gis::srs {
namespace {
struct Model {
  using Geographic = seekdb::gis::srs::Geographic;
  using Projected = seekdb::gis::srs::Projected;
  using CoordinateSystem = seekdb::gis::srs::CoordinateSystem;
  using Datum = seekdb::gis::srs::Datum;
  using Spheroid = seekdb::gis::srs::Spheroid;
  using Authority = seekdb::gis::srs::Authority;
  using Towgs84 = seekdb::gis::srs::Towgs84;
  using AxisPair = seekdb::gis::srs::AxisPair;
  using Axis = seekdb::gis::srs::Axis;
  using PrimeMeridian = seekdb::gis::srs::PrimeMeridian;
  using Unit = seekdb::gis::srs::Unit;
  using Parameters = seekdb::gis::srs::Parameters;
  using Parameter = seekdb::gis::srs::Parameter;
  using Projection = seekdb::gis::srs::Projection;
  using Direction = seekdb::gis::srs::Direction;
  using String = std::string;
  bool assign(std::string &out, const std::string &value) { out = value; return true; }
};
}
bool parse(std::string_view input, CoordinateSystem &out)
{
  Model model;
  return seekdb::geo::srs::parse_wkt(input, model, out);
}
} // namespace seekdb::gis::srs

