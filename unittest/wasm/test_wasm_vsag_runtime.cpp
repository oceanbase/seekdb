// Copyright (c) 2026 OceanBase.
// SPDX-License-Identifier: Apache-2.0
#include "vsag/allocator.h"
#include "vsag/dataset.h"
#include "vsag/options.h"
#include "vsag/vsag.h"

#include <array>
#include <cassert>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <map>
#include <new>
#include <stdexcept>
#include <thread>

namespace {
class TrackingAllocator : public vsag::Allocator {
public:
  std::string Name() override { return "WasmVSAGTest"; }
  void *Allocate(uint64_t size) override {
    if (remaining_-- == 0 || size > SIZE_MAX) { throw std::bad_alloc(); }
    void *p = std::malloc(static_cast<size_t>(size));
    if (!p) { throw std::bad_alloc(); }
    assert(live_.emplace(p, size).second);
    return p;
  }
  void *Reallocate(void *p, uint64_t size) override {
    if (!p) { return Allocate(size); }
    if (remaining_-- == 0 || size > SIZE_MAX) { throw std::bad_alloc(); }
    const auto old_size = live_.at(p);
    void *next = std::malloc(static_cast<size_t>(size));
    if (!next) { throw std::bad_alloc(); }
    std::memcpy(next, p, static_cast<size_t>(std::min(old_size, size)));
    Deallocate(p);
    assert(live_.emplace(next, size).second);
    return next;
  }
  void Deallocate(void *p) override {
    if (p) {
      assert(live_.erase(p) == 1);
      std::free(p);
    }
  }
  ~TrackingAllocator() override { assert(live_.empty()); }
  int remaining_ = 100;
  std::map<void *, uint64_t> live_;
};

void dataset_ownership()
{
  const std::array<int64_t, 2> ids{INT64_MIN, INT64_MAX};
  const std::array<float, 6> vectors{1, -2, 3.25f, -4, 0, 8};
  auto borrowed = vsag::Dataset::Make()->Owner(false)->NumElements(2)->Dim(3)
      ->Ids(ids.data())->Float32Vectors(vectors.data());
  assert(borrowed->GetIds() == ids.data() && borrowed->GetFloat32Vectors() == vectors.data());
  borrowed->Statistics("{\"dim\":3,\"label\":\"向量\"}");
  assert((borrowed->GetStatistics({"dim", "label", "missing"}) ==
          std::vector<std::string>{"3", "\"向量\"", ""}));
  for (int failure = 0; failure <= 2; ++failure) {
    TrackingAllocator allocator;
    allocator.remaining_ = failure;
    bool failed = false;
    try {
      auto copy = borrowed->DeepCopy(&allocator);
      assert(failure == 2);
      assert(copy->GetNumElements() == 2 && copy->GetDim() == 3);
      assert(copy->GetPaths() == nullptr);
      assert(copy->GetIds() != ids.data() && copy->GetFloat32Vectors() != vectors.data());
      assert(std::memcmp(copy->GetIds(), ids.data(), sizeof(ids)) == 0);
      assert(std::memcmp(copy->GetFloat32Vectors(), vectors.data(), sizeof(vectors)) == 0);
      assert(allocator.live_.size() == 2);
    } catch (const std::bad_alloc &) {
      failed = true;
    }
    assert(failed == (failure < 2));
    assert(allocator.live_.empty());
    assert(borrowed->GetIds()[0] == INT64_MIN && borrowed->GetIds()[1] == INT64_MAX);
  }
  const std::array<std::string, 2> paths{"/向量/a", "/vectors/b"};
  borrowed->Paths(paths.data());
  auto owned = borrowed->DeepCopy();
  assert(owned->GetPaths() != paths.data());
  assert(owned->GetPaths()[0] == paths[0] && owned->GetPaths()[1] == paths[1]);
  borrowed.reset();
  assert(owned->GetFloat32Vectors()[2] == 3.25f && owned->GetIds()[1] == INT64_MAX);
  owned.reset();
  assert(ids[0] == INT64_MIN && vectors[0] == 1);
}

void options_errors()
{
  auto &options = vsag::Options::Instance();
  const auto initial = options.block_size_limit();
  const uint64_t sizes[] = {256 * 1024, (UINT64_C(1) << 32) + 1};
  for (auto size : sizes) {
    options.set_block_size_limit(size);
    assert(options.block_size_limit() == size);
    bool caught = false;
    try {
      options.set_block_size_limit(256 * 1024 - 1);
    } catch (const std::runtime_error &) {
      caught = true;
    }
    assert(caught && options.block_size_limit() == size);
  }
  options.set_block_size_limit(initial);
  const auto threads = options.num_threads_building();
  for (uint64_t bad : {UINT64_C(0), UINT64_C(201), UINT64_MAX}) {
    bool caught = false;
    try {
      options.set_num_threads_building(bad);
    } catch (const std::runtime_error &) {
      caught = true;
    }
    assert(caught && options.num_threads_building() == threads);
  }
}
} // namespace

int main()
{
  assert(vsag::init());
  assert(vsag::init());
  assert(vsag::version().find("129b82c") != std::string::npos);
  assert(vsag::Options::Instance().logger() != nullptr);
  options_errors();
  dataset_ownership();
  std::array<std::thread, 3> threads;
  for (auto &thread : threads) { thread = std::thread(dataset_ownership); }
  for (auto &thread : threads) { thread.join(); }
  std::puts("VSAG initialization, dataset ownership and exception checks passed");
}
