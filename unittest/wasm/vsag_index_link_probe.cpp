// Copyright (c) 2026 OceanBase.
// SPDX-License-Identifier: Apache-2.0
// Retain the real public HNSW Index vtable in a small strict-link diagnostic.
// The broader public Factory behavior is tested by test_wasm_vsag_factory.
#include "index/hnsw.h"
#include "vsag_test_allocator.h"
#include <cassert>

int main()
{
  vsag::IndexCommonParam common;
  common.dim_ = 8;
  common.allocator_ = std::make_shared<VsagTestAllocator>();
  auto json = vsag::JsonType::Parse(R"({"max_degree":8,"ef_construction":100})");
  auto params = vsag::HnswParameters::FromJson(json, common);
  auto index = std::make_shared<vsag::HNSW>(params, common);
  assert(index->InitMemorySpace().has_value());
#ifdef VSAG_DISABLE_STATIC_HNSW
  params.use_static = true;
  bool rejected = false;
  try {
    auto unsupported = std::make_shared<vsag::HNSW>(params, common);
  } catch (const vsag::VsagException &error) {
    rejected = error.error_.type == vsag::ErrorType::UNSUPPORTED_INDEX;
  }
  assert(rejected);
#endif
}
