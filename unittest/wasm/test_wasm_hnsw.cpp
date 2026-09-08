// Copyright (c) 2026 OceanBase.
// SPDX-License-Identifier: Apache-2.0
#include "algorithm/hnswlib/hnswalg.h"
#include "algorithm/hnswlib/space_l2.h"
#include "storage/stream_reader.h"
#include "storage/stream_writer.h"
#include "vsag_test_allocator.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <cstdlib>
#include <limits>
#include <sstream>
#include <thread>

namespace {
constexpr size_t count = 40, dim = 8;
using Vector = std::array<float, dim>;
using Data = std::array<Vector, count>;
int64_t label(size_t i) { return (INT64_C(1) << 40) * (i % 2 ? -1 : 1) + 17 * static_cast<int64_t>(i); }

std::vector<std::pair<float, int64_t>> nearest(const Data &data, const Vector &query,
                                             size_t deleted = count)
{
  std::vector<std::pair<float, int64_t>> result;
  for (size_t i = 0; i < count; ++i) {
    if (i == deleted) { continue; }
    double distance = 0;
    for (size_t d = 0; d < dim; ++d) {
      const double diff = static_cast<double>(query[d]) - data[i][d];
      distance += diff * diff;
    }
    result.emplace_back(static_cast<float>(distance), label(i));
  }
  std::sort(result.begin(), result.end());
  result.resize(5);
  return result;
}

void check(hnswlib::HierarchicalNSW &index, const Data &data, size_t deleted = count)
{
  for (size_t i = 0; i < count; ++i) {
    auto heap = index.searchKnn(data[i].data(), 5, 100);
    assert(heap.size() == 5);
    std::vector<std::pair<float, int64_t>> actual;
    while (!heap.empty()) { actual.push_back(heap.top()); heap.pop(); }
    std::sort(actual.begin(), actual.end());
    auto expected = nearest(data, data[i], deleted);
    for (size_t k = 0; k < actual.size(); ++k) {
      // An exact tie at the kth neighbor permits either ID. Check the complete
      // distance ordering, and independently validate the returned 64-bit ID.
      assert(std::abs(actual[k].first - expected[k].first) <= 1e-5 * std::max(1.0f, expected[k].first));
      size_t candidate = 0;
      while (candidate < count && label(candidate) != actual[k].second) { ++candidate; }
      assert(candidate < count && candidate != deleted);
      double distance = 0;
      for (size_t d = 0; d < dim; ++d) {
        const double diff = static_cast<double>(data[i][d]) - data[candidate][d];
        distance += diff * diff;
      }
      assert(std::abs(actual[k].first - distance) <= 1e-5 * std::max(1.0, distance));
      for (size_t previous = 0; previous < k; ++previous) {
        assert(actual[previous].second != actual[k].second);
      }
    }
  }
}
} // namespace

int main()
{
  VsagTestAllocator allocator;
  hnswlib::L2Space space(dim);
  hnswlib::HierarchicalNSW index(&space, 64, &allocator, 8, 100, false, false, 256 * 1024);
  assert(index.init_memory_space());
  Data data{};
  for (size_t i = 0; i < count; ++i) {
    for (size_t d = 0; d < dim; ++d) {
      data[i][d] = static_cast<float>(i * (d + 1)) / 8 + (i % 3) / 16.0f;
    }
    assert(index.addPoint(data[i].data(), label(i)));
  }
  check(index, data);
  std::array<std::thread, 3> threads;
  for (auto &thread : threads) { thread = std::thread([&] { check(index, data); }); }
  for (auto &thread : threads) { thread.join(); }
  for (float &value : data[7]) { value += 0.375f; }
  index.updateVector(label(7), data[7].data());
  assert(index.getDistanceByLabel(label(7), data[7].data()) == 0);
  check(index, data);
  index.markDelete(label(11));
  check(index, data, 11);

  std::ostringstream output(std::ios::binary);
  IOStreamWriter writer(output);
  index.saveIndex(writer);
  const auto bytes = output.str();
  assert(bytes.size() == writer.GetCursor() && !bytes.empty());
  std::istringstream input(bytes, std::ios::binary);
  IOStreamReader reader(input);
  hnswlib::HierarchicalNSW restored(&space, 64, &allocator, 8, 100, false, false, 256 * 1024);
  // Match Factory::CreateIndex -> HNSW::InitMemorySpace before Deserialize.
  assert(restored.init_memory_space());
  restored.loadIndex(reader, &space);
  check(restored, data, 11);
  std::puts("VSAG HNSW search, update, deletion, concurrent reads and stream roundtrip passed");
}
