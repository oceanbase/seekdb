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
#include "../../../plugins/gis/projection.h"
#include <boost/geometry/srs/transformation.hpp>
#include <boost/geometry/geometries/point.hpp>
#include <boost/geometry/geometries/linestring.hpp>
#include <cmath>
#include <iostream>
#include <cstdlib>

#define CHECK(e) do { if (!(e)) { std::cerr << __LINE__ << ": " << #e << std::endl; std::abort(); } } while (false)
int main()
{
  namespace bg = boost::geometry;
  const std::string source = "+proj=longlat +datum=WGS84";
  // Use the original direct Boost range call as the reference (not the new
  // wrapper). This proves adapter parity, not independent geodetic accuracy.
  unsigned comparisons = 0;
  for (const std::string target : {
      "+proj=utm +zone=31 +datum=WGS84 +units=m",
      "+proj=utm +zone=31 +south +datum=WGS84 +units=km",
      "+proj=tmerc +lat_0=0 +lon_0=3 +k=0.9996 +x_0=500000 +datum=WGS84",
      "+proj=lcc +lat_1=33 +lat_2=45 +lat_0=39 +lon_0=0 +datum=WGS84",
      "+proj=aea +lat_1=29.5 +lat_2=45.5 +lat_0=23 +lon_0=0 +datum=WGS84",
      "+proj=laea +lat_0=45 +lon_0=0 +datum=WGS84",
      "+proj=stere +lat_0=90 +lat_ts=70 +lon_0=0 +datum=WGS84",
      "+proj=cass +lat_0=0 +lon_0=0 +datum=WGS84",
      "+proj=longlat +a=6378137 +rf=298.257223563 +towgs84=1,2,3,0,0,0,0",
      "+proj=longlat +datum=WGS84 +pm=paris"}) {
    const bg::srs::transformation<> original{bg::srs::proj4(source), bg::srs::proj4(target)};
    const seekdb::gis::Projection adapter(source, target), reverse(target, source);
    using P = bg::model::point<double, 3, bg::cs::cartesian>;
    bg::model::linestring<P> input, expected, original_inverse;
    for (unsigned i = 0; i < 100; ++i) {
      P p;
      bg::set<0>(p, (-4 + double(i % 9)) * M_PI / 180);
      bg::set<1>(p, (20 + double(i % 41)) * M_PI / 180);
      bg::set<2>(p, double(i * 5)); input.push_back(p);
    }
    CHECK(original.forward(input, expected) && expected.size() == input.size());
    CHECK(original.inverse(expected, original_inverse) && original_inverse.size() == input.size());
    double max_roundtrip_error = 0;
    for (size_t i = 0; i < input.size(); ++i) {
      double x = bg::get<0>(input[i]), y = bg::get<1>(input[i]), z = bg::get<2>(input[i]);
      CHECK(adapter.forward(x, y, z, 3));
      CHECK(std::abs(x - bg::get<0>(expected[i])) < 1e-8);
      CHECK(std::abs(y - bg::get<1>(expected[i])) < 1e-8);
      CHECK(std::abs(z - bg::get<2>(expected[i])) < 1e-8);
      CHECK(reverse.forward(x, y, z, 3));
      CHECK(std::abs(x - bg::get<0>(original_inverse[i])) < 1e-12);
      CHECK(std::abs(y - bg::get<1>(original_inverse[i])) < 1e-12);
      CHECK(std::abs(z - bg::get<2>(original_inverse[i])) < 1e-8);
      max_roundtrip_error = std::max(max_roundtrip_error, std::max(
          std::abs(x - bg::get<0>(input[i])), std::abs(y - bg::get<1>(input[i]))));
      // Original Boost Cassini inverse is approximate off the central
      // meridian. Require identical original inverse results above rather
      // than claiming accuracy the existing backend does not provide.
      if (target.find("+proj=cass ") != 0) {
        CHECK(std::abs(x - bg::get<0>(input[i])) < 1e-8);
        CHECK(std::abs(y - bg::get<1>(input[i])) < 1e-8);
      }
      CHECK(std::abs(z - bg::get<2>(input[i])) < 1e-4);
      ++comparisons;
    }
    if (target.find("+proj=cass ") == 0)
      std::cout << "Original Cassini maximum roundtrip angular error: " << max_roundtrip_error << " radians" << std::endl;
  }
  seekdb::gis::Projection mercator(source, "+proj=merc +a=6378137 +b=6378137 +units=m");
  double x = 0.1, y = 2, z = 0;
  CHECK(!mercator.forward(x, y, z, 2) && x == 0.1 && y == 2 && z == 0);
  y = 0.5;
  CHECK(!mercator.forward(x, y, z, 4) && x == 0.1 && y == 0.5);
  std::cout << "PASS: " << comparisons << " original Boost range/point adapter comparisons and inverse controls; no catalog/SQL claim" << std::endl;
}
