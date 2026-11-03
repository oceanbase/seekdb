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
#include "seekdb/geo/s2_covering.hpp"
#include "seekdb/geo/s2_mbr.hpp"
#include <s2/s2cell.h>
#include <s2/s2polyline.h>
#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <set>

namespace indexer = seekdb::geo::s2_index;
#define CHECK(expression) do { if (!(expression)) { \
  std::cerr << __LINE__ << ": " << #expression << std::endl; std::abort(); \
} } while (false)

int main()
{
  for (int face = 0; face < 6; ++face) for (int level = 0; level <= 30; ++level) {
    const auto id = S2CellId::FromFace(face).child_begin(30).parent(level);
    indexer::CellInfo info;
    CHECK(indexer::cell_info(id.id(), info));
    CHECK(info.cell == id.id() && info.range_min == id.range_min().id() && info.range_max == id.range_max().id());
    CHECK(info.ancestor_count == static_cast<unsigned>(level));
    for (unsigned i = 0; i < 30; ++i)
      CHECK(info.ancestors[i] == (i < info.ancestor_count ? id.parent(level - 1 - i).id() : 0));
  }
  indexer::CellInfo sentinel;
  CHECK(indexer::cell_info(UINT64_MAX, sentinel) && sentinel.range_min == UINT64_MAX &&
        sentinel.range_max == UINT64_MAX && sentinel.ancestor_count == 0);
  for (uint64_t id : {UINT64_C(0), UINT64_C(2), UINT64_C(0xc000000000000001)}) {
    CHECK(!indexer::cell_info(id, sentinel));
    CHECK(sentinel.cell == UINT64_MAX && sentinel.range_min == UINT64_MAX);
  }
  for (bool query : {false, true}) {
    const auto options = indexer::options(query);
    CHECK(options.max_cells() == (query ? 50 : 4));
    CHECK(options.max_level() == 30 && options.level_mod() == 1 && options.min_level() == 0);
    // Reference is the original visitor's direct region-coverer/union loop,
    // not a second call to the extracted cover_regions helper.
    for (int seed = 0; seed < 100; ++seed) {
      std::vector<std::unique_ptr<S2Region>> regions;
      std::vector<S2CellId> vertices;
      S2LatLngRectBounder bounder;
      S2LatLngRect mbr = S2LatLngRect::Empty();
      const S2LatLng first = S2LatLng::FromDegrees(seed * 0.41 - 20, seed * 1.13 - 55);
      const S2LatLng second = S2LatLng::FromDegrees(seed * 0.21 + 10, seed * 0.39 - 30);
      regions.push_back(std::make_unique<S2Cell>(first));
      const std::vector<S2LatLng> line{first, second};
      regions.push_back(std::make_unique<S2Polyline>(line));
      for (const auto point : line) {
        bounder.AddPoint(point.ToPoint());
        vertices.emplace_back(point);
      }
      S2RegionCoverer original(options);
      S2CellUnion expected;
      for (const auto &region : regions) expected = expected.Union(original.GetCovering(*region));
      S2CellUnion actual;
      indexer::cover_regions(regions, options, true, bounder, actual, mbr, vertices);
      CHECK(actual == expected);
      const auto source = actual;
      for (bool buffer : {false, true}) {
        auto reference = expected;
        const auto distance = S1Angle::Radians(0.002);
        if (buffer) reference.Expand(distance, 2);
        reference.Normalize();
        std::vector<uint64_t> expected_cells;
        std::set<uint64_t> expected_ancestors;
        for (const auto cell : reference) {
          expected_cells.push_back(cell.id());
          for (int level = cell.level() - 1; level >= 0; --level) expected_ancestors.insert(cell.parent(level).id());
        }
        for (auto id : expected_cells) expected_ancestors.erase(id);
        const auto sets = indexer::cells_and_ancestors(actual, options, true, false, false, buffer, distance);
        CHECK(sets.cells == expected_cells);
        CHECK(std::set<uint64_t>(sets.ancestors.begin(), sets.ancestors.end()) == expected_ancestors);
        CHECK(sets.ancestors.size() == expected_ancestors.size());
        CHECK(indexer::cell_ids(actual, options, true, false, false, false, buffer, distance) == expected_cells);
        CHECK(actual == source); // Getter must not mutate and cumulatively buffer the stored cover.
        CHECK(indexer::cells_and_ancestors(actual, options, true, false, false, buffer, distance).cells == sets.cells);
        const auto reset = indexer::cell_ids(actual, options, true, false, true, false, buffer, distance);
        CHECK(reset.size() == expected_cells.size() + 1 && reset.back() == UINT64_MAX);
        const auto invalid = indexer::cells_and_ancestors(actual, options, true, true, false, buffer, distance);
        CHECK(invalid.cells == std::vector<uint64_t>{UINT64_MAX} && invalid.ancestors.empty());
      }
      S2CellUnion expected_vertices(vertices);
      std::vector<uint64_t> expected_ids;
      for (const auto id : expected_vertices) expected_ids.push_back(id.id());
      CHECK(indexer::vertex_cell_ids(vertices, false) == expected_ids);
    }
  }
  std::vector<std::unique_ptr<S2Region>> faces;
  for (int i = 0; i < 6; ++i) faces.push_back(std::make_unique<S2Cell>(S2CellId::FromFace(i)));
  S2LatLngRectBounder bounder;
  bounder.AddPoint(S2LatLng::FromDegrees(1, 1).ToPoint());
  bounder.AddPoint(S2LatLng::FromDegrees(2, 2).ToPoint());
  S2LatLngRect mbr = S2LatLngRect::Empty();
  std::vector<S2CellId> vertices;
  S2CellUnion covering;
  const auto options = indexer::options();
  indexer::cover_regions(faces, options, true, bounder, covering, mbr, vertices);
  const auto expected_rect = bounder.GetBound().Expanded(S2LatLng::FromDegrees(0.00001, 0.00001));
  CHECK(mbr == expected_rect && vertices.size() == 4);
  CHECK(covering == S2RegionCoverer(options).GetCovering(expected_rect));
  const auto distance = S1Angle::Radians(0.01);
  CHECK(indexer::geographic_mbr(mbr, false, false, true, distance) ==
        expected_rect.ExpandedByDistance(distance).ExpandedByDistance(S1Angle::Degrees(DBL_EPSILON)));
  CHECK(mbr == expected_rect);
  CHECK(indexer::geographic_mbr(mbr, false, true, false, distance).is_full());
  CHECK(indexer::geographic_mbr(S2LatLngRect::Empty(), false, false, true, distance).is_empty());

  indexer::ProjectionBounds bounds{-100, 100, -200, 200};
  S2Point point;
  CHECK(indexer::project_point(bounds, 0, 0, point) && point == S2Point(1, 0, 0));
  CHECK(indexer::project_point(bounds, -98, 196, point));
  CHECK(!indexer::project_point(bounds, -98.001, 196, point));
  CHECK(!indexer::project_point(bounds, 0, std::numeric_limits<double>::infinity(), point));
  CHECK(!indexer::valid_bounds({0, 0, 0, 1}));
  CHECK(!indexer::valid_bounds({-1e308, 1e308, 0, 1}));
  for (int i = 0; i <= 100; ++i) {
    const double s = i / 100.0;
    const double expected = s >= 0.5 ? (4 * s * s - 1) / 3 : (1 - 4 * (1 - s) * (1 - s)) / 3;
    CHECK(std::abs(indexer::st_to_uv(s) - expected) < 1e-15);
  }
  std::cout << "PASS: 200 original per-region cover controls, ancestor sets, buffer immutability, full-range fallback, SRS bounds" << std::endl;
  namespace mbr_test = seekdb::geo::index_mbr;
  const mbr_test::Box boxes[] = {{-10, 10, -10, 10}, {0, 1, 0, 1}, {1, 2, 1, 2},
      {170, -170, -20, 20}, {178, -178, -10, 10}, {-180, 180, -90, 90},
      {0, 0, 0, 0}, {1e-14, 1e-14, 0, 0}};
  size_t comparisons = 0;
  for (const auto &row : boxes) for (const auto &query : boxes) {
    const bool row_point = row.xmin == row.xmax && row.ymin == row.ymax;
    const bool query_point = query.xmin == query.xmax && query.ymin == query.ymax;
    const S2LatLngRect left(S2LatLng::FromDegrees(row.ymin, row.xmin), S2LatLng::FromDegrees(row.ymax, row.xmax));
    const S2LatLngRect right(S2LatLng::FromDegrees(query.ymin, query.xmin), S2LatLng::FromDegrees(query.ymax, query.xmax));
    for (auto relation : {mbr_test::Relation::covers, mbr_test::Relation::intersects, mbr_test::Relation::covered_by}) {
      bool reject = true;
      CHECK(mbr_test::filter(row, query, true, row_point, query_point, relation, reject));
      bool expected = relation == mbr_test::Relation::covers ? !right.Contains(left)
                    : relation == mbr_test::Relation::intersects ? !left.Intersects(right) : !left.Contains(right);
      if (row_point && query_point && left.ApproxEquals(right)) expected = false;
      CHECK(reject == expected);
      ++comparisons;
    }
  }
  bool reject = false;
  CHECK(!mbr_test::filter({NAN, 0, 0, 1}, boxes[0], false, false, false, mbr_test::Relation::intersects, reject));
  CHECK(!mbr_test::filter({0, 1, 2, 1}, boxes[0], false, false, false, mbr_test::Relation::intersects, reject));
  CHECK(!mbr_test::filter({-181, 0, 0, 1}, boxes[0], true, false, false, mbr_test::Relation::intersects, reject));
  CHECK(!mbr_test::filter(boxes[3], boxes[0], false, false, false, mbr_test::Relation::intersects, reject));
  CHECK(mbr_test::filter(boxes[6], boxes[7], true, true, true, mbr_test::Relation::intersects, reject) && !reject);
  CHECK(mbr_test::filter(boxes[6], boxes[7], false, true, true, mbr_test::Relation::intersects, reject) && reject);
  char encoded[33]{};
  size_t size = 99;
  CHECK(!mbr_test::encode(boxes[1], false, encoded + 1, 31, size) && size == 0);
  CHECK(mbr_test::encode(boxes[3], false, encoded + 1, 32, size) && size == 32);
  const double expected_storage[] = {-20, 20, 170, -170};
  CHECK(std::memcmp(encoded + 1, expected_storage, sizeof(expected_storage)) == 0);
  mbr_test::Box decoded{};
  CHECK(mbr_test::decode(encoded + 1, size, false, decoded) && decoded.xmin == 170 && decoded.xmax == -170);
  std::cout << "PASS: " << comparisons << " direct S2 MBR predicate controls, point tolerance, invalid bounds and storage codec" << std::endl;
}
