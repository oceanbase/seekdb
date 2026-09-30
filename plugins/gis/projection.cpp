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
#include "projection.h"
#include "seekdb/geo/projection.hpp"
#include "seekdb/plugin/srs_spi.h"
#include <boost/geometry/geometries/point.hpp>
#include <cmath>

namespace seekdb::gis {
namespace {
std::string checked_definition(const std::string &text, bool require_datum)
{
  if (text.empty() || text.size() > SEEKDB_PLUGIN_SRS_MAX_PROJ4_BYTES ||
      text.find('\0') != std::string::npos) throw ProjectionInputError{};
  const boost::geometry::srs::detail::proj4_parameters parameters(text);
  bool projection = false, identity_grid = false;
  for (const auto &p : parameters) {
    if (p.name == "proj") {
      if (projection || p.value.empty() || p.value == "geocent") throw ProjectionInputError{};
      projection = true;
    }
    // No filesystem/grid-data capability is exposed by this service. The
    // default Boost empty-grid policy cannot implement real grids. @null is
    // the explicit identity grid used by the original EPSG:3857 catalog row;
    // it needs no file and retains Boost's WGS84 grid-datum convention.
    if (p.name == "init" || (p.name == "nadgrids" && p.value != "@null") || p.name == "geoidgrids" ||
        (p.name == "axis" && p.value != "enu")) throw ProjectionInputError{};
    if (p.name == "nadgrids" && p.value == "@null") identity_grid = true;
  }
  if (!projection) throw ProjectionInputError{};
  // A named datum can introduce grids even without a literal +nadgrids.
  const auto *datum = boost::geometry::projections::detail::pj_datum_find_datum<double>(parameters);
  if (datum != nullptr && !datum->nadgrids.empty()) throw ProjectionInputError{};
  if (require_datum && !identity_grid && (datum == nullptr || datum->towgs84.empty())) {
    boost::geometry::srs::detail::towgs84<double> shift;
    if (!boost::geometry::projections::detail::pj_datum_find_towgs84(parameters, shift))
      throw ProjectionInputError{};
  }
  return text;
}
} // namespace

struct Projection::State {
  seekdb::geo::projection::Transformer transformer;
  State(const std::string &source, const std::string &target, bool require_datum)
    : transformer(seekdb::geo::projection::Definition(checked_definition(source, require_datum)),
                  seekdb::geo::projection::Definition(checked_definition(target, require_datum))) {}
};
Projection::Projection(const std::string &source, const std::string &target, bool require_datum)
try
  : state_(std::make_unique<State>(source, target, require_datum)) {}
catch (const std::bad_alloc &) { throw; }
catch (...) { throw ProjectionInputError{}; }
Projection::~Projection() = default;

bool Projection::forward(double &x, double &y, double &z, uint32_t dimensions) const
{
  namespace bg = boost::geometry;
  if ((dimensions != 2 && dimensions != 3) || !std::isfinite(x) || !std::isfinite(y) ||
      (dimensions == 3 && !std::isfinite(z))) return false;
  // Cartesian Boost points carry normalized radians unchanged for geographic
  // proj4 definitions; projected coordinates use the definition's linear unit.
  // Preserve 2D semantics: Boost applies its own implicit-height policy rather
  // than treating this as an explicit 3D input. In 3D Boost also scales vertical
  // units and updates height during datum conversion.
  if (dimensions == 2) {
    bg::model::point<double, 2, bg::cs::cartesian> input, output;
    bg::set<0>(input, x); bg::set<1>(input, y);
    if (!state_->transformer.forward(input, output) || !std::isfinite(bg::get<0>(output)) ||
        !std::isfinite(bg::get<1>(output))) return false;
    x = bg::get<0>(output); y = bg::get<1>(output);
  } else {
    bg::model::point<double, 3, bg::cs::cartesian> input, output;
    bg::set<0>(input, x); bg::set<1>(input, y); bg::set<2>(input, z);
    if (!state_->transformer.forward(input, output) || !std::isfinite(bg::get<0>(output)) ||
        !std::isfinite(bg::get<1>(output)) || !std::isfinite(bg::get<2>(output))) return false;
    x = bg::get<0>(output); y = bg::get<1>(output); z = bg::get<2>(output);
  }
  return true;
}
} // namespace seekdb::gis
