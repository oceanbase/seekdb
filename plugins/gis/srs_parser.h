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
#pragma once

#include <array>
#include <cmath>
#include <string>
#include <string_view>
#include <vector>
#include <boost/variant.hpp>

namespace seekdb::gis::srs {
// Plugin-owned parse records; names, authority strings and parameter order are
// retained exactly. No SRID guessing, projection validation or host pointers.
enum class Direction { INIT = 0, EAST, SOUTH, WEST, NORTH, OTHER };
struct Authority { bool is_valid = false; std::string org_name, org_code; };
struct Spheroid { std::string name; double semi_major_axis = NAN, inverse_flattening = NAN; Authority authority; };
struct Towgs84 { bool is_valid = false; double value[7]{}; };
struct Datum { std::string name; Spheroid spheroid; Towgs84 towgs84; Authority authority; };
struct PrimeMeridian { std::string name; double longtitude = NAN; Authority authority; };
struct Unit { std::string type; double conversion_factor = NAN; Authority authority; };
struct Axis { std::string name; Direction direction = Direction::INIT; };
struct AxisPair { Axis x, y; };
struct Geographic {
  std::string rs_name; Datum datum_info; PrimeMeridian primem; Unit unit; AxisPair axis; Authority authority;
};
struct Projection { std::string name; Authority authority; };
struct Parameter { std::string name; double value = 0; Authority authority; };
using Parameters = std::vector<Parameter>;
struct Projected {
  std::string rs_name; Geographic projected_rs; Projection projection; Parameters proj_params;
  Unit unit; AxisPair axis; Authority authority;
};
using CoordinateSystem = boost::variant<Geographic, Projected>;

// Syntax only. SRS semantic validation and projection strategy selection are
// separate steps. Failure leaves out unchanged; allocation errors propagate to
// the enclosing C++/ABI adapter. Callers impose their catalog/service size cap.
bool parse(std::string_view input, CoordinateSystem &out);
} // namespace seekdb::gis::srs

