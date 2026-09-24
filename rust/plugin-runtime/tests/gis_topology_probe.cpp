/*
 * Copyright (c) 2026 OceanBase.
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

// Diagnostic regression for known algorithm gaps. Compiles the actual private
// implementation, not a substitute geometry model. No loader, SQL or storage
// claim. Until the backend is corrected this deliberately exits nonzero; it is
// not a passing test of the old approximate results.
#include "../../../plugins/gis/geometry_engine.cpp"
#include <iostream>

int main()
{
  int failures = 0;
  const auto check = [&](const char *name, double actual, double expected) {
    const bool matches = std::isfinite(actual) && std::abs(actual - expected) <= 1e-12;
    std::cout << (matches ? "PASS " : "FAIL ") << name
              << ": actual=" << actual << ", expected=" << expected << '\n';
    if (!matches) ++failures;
  };
  Geometry diagonal, opposite, parallel, point, horizontal;
  diagonal.type = opposite.type = parallel.type = horizontal.type = 2;
  point.type = 1;
  diagonal.points = {{0, 0}, {2, 2}};
  opposite.points = {{0, 2}, {2, 0}};
  parallel.points = {{0, 1}, {1, 2}};
  point.points = {{1, 1}};
  horizontal.points = {{0, 0}, {2, 0}};

  check("distinct diagonals are not equal",
        relation_result(SEEKDB_GIS_REL_EQUALS, diagonal, opposite, 0), 0);
  check("disjoint parallel segments do not intersect",
        relation_result(SEEKDB_GIS_REL_INTERSECTS, diagonal, parallel, 0), 0);
  check("point-to-segment distance uses its interior", geometry_distance(point, horizontal), 1);
  const auto united = combine_rectangles(rectangle(0, 0, 0, 1, 1), rectangle(0, 2, 0, 3, 1),
                                        SEEKDB_GIS_OP_UNION);
  check("union of disjoint unit squares has area two", geometry_area(united), 2);
  std::cout << "Topology mismatches: " << failures << '\n';
  return failures == 0 ? 0 : 1;
}
