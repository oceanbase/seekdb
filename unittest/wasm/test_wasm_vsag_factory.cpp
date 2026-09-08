// Copyright (c) 2026 OceanBase.
// SPDX-License-Identifier: Apache-2.0
#include "vsag/factory.h"
#include "vsag/options.h"
#include "vsag_test_allocator.h"
#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <sstream>
#include <thread>
#include <vector>

constexpr int count = 32, dim = 8;
static std::string parameters(const std::string &name, const std::string &quantizer)
{
  const std::string common = R"({"dim":8,"dtype":"float32","metric_type":"l2","use_old_serial_format":true,)";
  if (name == "hnsw") return common + R"("hnsw":{"max_degree":16,"ef_construction":100}})";
  return common + R"("index_param":{"max_degree":16,"ef_construction":100,"build_thread_count":1,"support_remove":true,"base_quantization_type":")" +
         quantizer + R"(","use_reorder":)" + (quantizer == "rabitq" ? "true" : "false") +
         R"(,"precise_quantization_type":"fp32","precise_io_type":"block_memory_io"}})";
}

static void exercise(const std::string &name, const std::string &quantizer)
{
  VsagTestAllocator allocator;
  std::array<float, count * dim> vectors;
  std::array<int64_t, count> labels;
  std::array<bool, count> active;
  active.fill(true);
  for (int i = 0; i < count; ++i) {
    labels[i] = (i % 2 ? -(int64_t{1} << 40) : (int64_t{1} << 41)) + i;
    for (int d = 0; d < dim; ++d) vectors[i * dim + d] = std::sin(float((i+1)*(d+3))) * 0.7F;
  }
  const auto config = parameters(name, quantizer);
  auto create = [&] {
    auto result = vsag::Factory::CreateIndex(name, config, &allocator);
    if (!result) std::fprintf(stderr, "create %s/%s failed: %s\n", name.c_str(), quantizer.c_str(), result.error().message.c_str());
    assert(result.has_value());
    return result.value();
  };
  auto index = create();
  auto dataset = [&](int start, int size) {
    return vsag::Dataset::Make()->NumElements(size)->Dim(dim)->Ids(labels.data()+start)
        ->Float32Vectors(vectors.data()+start*dim)->Owner(false);
  };
  auto built = index->Build(dataset(0,24));
  assert(built.has_value() && built->empty());
  auto added = index->Add(dataset(24,8));
  assert(added.has_value() && added->empty());
  assert(index->GetNumElements() == count);
  auto verify = [&] {
    const std::string search = name == "hnsw" ? R"({"hnsw":{"ef_search":100}})" : R"({"hgraph":{"ef_search":100}})";
    for (int row : {2,7,9,17,29}) {
      std::array<float, dim> query;
      std::copy_n(vectors.data()+row*dim, dim, query.data());
      query[0] += 0.03F;
      auto data = vsag::Dataset::Make()->NumElements(1)->Dim(dim)->Float32Vectors(query.data())->Owner(false);
      std::vector<std::pair<double,int64_t>> expected;
      for (int i = 0; i < count; ++i) if (active[i]) {
        double distance = 0;
        for (int d = 0; d < dim; ++d) {
          const double delta = double(query[d])-vectors[i*dim+d];
          distance += delta*delta;
        }
        expected.emplace_back(distance, labels[i]);
      }
      std::sort(expected.begin(), expected.end());
      auto result = index->KnnSearch(data, 5, search);
      if (!result) std::fprintf(stderr, "query failed: %s\n", result.error().message.c_str());
      assert(result.has_value() && result.value()->GetDim() == 5);
      for (int k = 0; k < 5; ++k) {
        const auto id = result.value()->GetIds()[k];
        const auto distance = result.value()->GetDistances()[k];
        if (quantizer == "sq8") {
          // SQ8 without refinement intentionally returns approximate distances.
          // Check live/unique labels, top-one identity and bounded distance/rank
          // error on this bounded fixture, rather than requiring fp32 output.
          auto exact = std::find_if(expected.begin(), expected.end(), [id](const auto &entry) { return entry.second == id; });
          assert(exact != expected.end());
          if (k == 0) assert(id == expected[0].second);
          assert(exact->first <= expected[4].first + 0.05);
          assert(std::abs(distance-exact->first) < 0.05);
          for (int previous = 0; previous < k; ++previous) assert(result.value()->GetIds()[previous] != id);
          if (k > 0) assert(result.value()->GetDistances()[k-1] <= distance);
        } else {
          assert(id == expected[k].second);
          assert(std::abs(distance-expected[k].first) < 1e-4);
        }
      }
    }
  };
  verify();
  for (int d = 0; d < dim; ++d) vectors[7*dim+d] *= 0.8F;
  auto updated = index->UpdateVector(labels[7], dataset(7,1), true);
  assert(updated.has_value() && updated.value());
  const int64_t new_label = (int64_t{1} << 45) + 9;
  auto renamed = index->UpdateId(labels[9], new_label);
  assert(renamed.has_value() && renamed.value());
  labels[9] = new_label;
  auto removed = index->Remove(labels[11]);
  assert(removed.has_value() && removed.value());
  active[11] = false;
  verify();
  {
    std::thread readers[3];
    for (auto &reader : readers) reader = std::thread(verify);
    for (auto &reader : readers) reader.join();
  }
  std::stringstream stream;
  auto saved = index->Serialize(stream);
  assert(saved.has_value());
  index.reset();
  index = create();
  auto restored = index->Deserialize(stream);
  if (!restored) std::fprintf(stderr, "restore %s/%s failed: %s\n", name.c_str(), quantizer.c_str(), restored.error().message.c_str());
  assert(restored.has_value());
  verify();
  std::printf("public factory %s/%s: query, mutation and stream restore passed\n", name.c_str(), quantizer.c_str());
}

int main()
{
  vsag::Options::Instance().set_num_threads_building(1);
  vsag::Options::Instance().set_num_threads_io(1);
  vsag::Options::Instance().set_block_size_limit(256 * 1024);
  exercise("hnsw", "fp32");
  exercise("hgraph", "fp32");
  exercise("hgraph", "sq8");
  exercise("hgraph", "rabitq");
  auto unknown = vsag::Factory::CreateIndex("unknown", parameters("hnsw", "fp32"));
  assert(!unknown && unknown.error().type == vsag::ErrorType::UNSUPPORTED_INDEX);
#ifdef __EMSCRIPTEN__
  auto diskann = vsag::Factory::CreateIndex("diskann", parameters("hnsw", "fp32"));
  assert(!diskann && diskann.error().type == vsag::ErrorType::UNSUPPORTED_INDEX);
  auto static_hnsw = vsag::Factory::CreateIndex("hnsw", R"({"dim":8,"dtype":"float32","metric_type":"l2","hnsw":{"max_degree":16,"ef_construction":100,"use_static":true}})");
  assert(!static_hnsw && static_hnsw.error().type == vsag::ErrorType::UNSUPPORTED_INDEX);
#endif
}
