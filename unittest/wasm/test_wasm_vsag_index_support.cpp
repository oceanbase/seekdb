// Copyright (c) 2026 OceanBase.
// SPDX-License-Identifier: Apache-2.0
#include "impl/bitset/computable_bitset.h"
#include "impl/conjugate_graph.h"
#include "vsag_test_allocator.h"
#include <cassert>
#include <cstdio>
#include <sstream>
#include <thread>

static void check_bitset()
{
  // Exercise VSAG against the same CRoaring 3.0.0 used by the engine.
  auto bits = vsag::Bitset::Make();
  std::thread workers[3];
  for (int t = 0; t < 3; ++t) {
    workers[t] = std::thread([&, t] {
      for (int i = 0; i < 200; ++i) { bits->Set(t * 200 + i); }
    });
  }
  for (auto &worker : workers) { worker.join(); }
  bits->Set(UINT32_MAX);
  assert(bits->Count() == 601);
  bits->Set(17, false);
  assert(!bits->Test(17) && bits->Test(UINT32_MAX));
  auto sparse = std::dynamic_pointer_cast<vsag::ComputableBitset>(bits);
  assert(sparse);
  std::stringstream stream;
  IOStreamWriter writer(stream);
  sparse->Serialize(writer);
  auto restored = vsag::ComputableBitset::MakeInstance(vsag::ComputableBitsetType::SparseBitset);
  IOStreamReader reader(stream);
  restored->Deserialize(reader);
  assert(restored->Count() == 600);
  for (int i = 0; i < 601; ++i) { assert(restored->Test(i) == (i < 600 && i != 17)); }
  assert(restored->Test(UINT32_MAX));
}

static void check_conjugate_graph()
{
  VsagTestAllocator allocator;
  vsag::ConjugateGraph graph(&allocator);
  constexpr int64_t source = (int64_t{1} << 40) + 7;
  constexpr int64_t target = -(int64_t{1} << 41) + 9;
  auto added = graph.AddNeighbor(source, target);
  assert(added.has_value() && added.value());
  auto duplicate = graph.AddNeighbor(source, target);
  assert(duplicate.has_value() && !duplicate.value());
  auto binary = graph.Serialize();
  assert(binary.has_value());
  // The header is used as a byte offset when reopening the graph.
  assert(binary->size == graph.GetMemoryUsage());
  vsag::ConjugateGraph restored(&allocator);
  auto status = restored.Deserialize(binary.value());
  assert(status.has_value());
  std::priority_queue<std::pair<float, vsag::LabelType>> results;
  results.emplace(100.0F, source);
  auto enhanced = restored.EnhanceResult(results, [&](int64_t id) {
    assert(id == target);
    return 1.25F;
  });
  assert(enhanced.has_value() && enhanced.value() == 1);
  assert(results.size() == 1 && results.top().second == target && results.top().first == 1.25F);
}

int main()
{
  check_bitset();
  check_conjugate_graph();
  std::puts("VSAG index support: bitset concurrency and graph recovery passed");
}
