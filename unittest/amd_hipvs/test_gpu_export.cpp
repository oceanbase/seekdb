// Validate a real hipVS export with the exact VSAG dependency used by seekdb.
#include "lib/vector/hipvs_native_hnsw.h"
#include <vsag/vsag.h>
#include <vsag/constants.h>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <numeric>

void require(bool ok, const char *message) { if (!ok) throw std::runtime_error(message); }
template<class T> void require_result(const T &result) {
  if (!result.has_value()) throw std::runtime_error(result.error().message);
}
int main(int argc, char **argv) try {
  require(argc == 5, "usage: test_gpu_export native.hnsw vectors.f32 rows dim");
  const int64_t rows = std::stoll(argv[3]);
  const int dim = std::stoi(argv[4]);
  require(rows > 0 && rows <= 1000000 && dim > 0 && dim <= 16384, "invalid shape");
  std::vector<float> vectors(rows * dim);
  std::ifstream raw(argv[2], std::ios::binary);
  raw.read(reinterpret_cast<char *>(vectors.data()), vectors.size() * sizeof(float));
  require(bool(raw) && raw.peek() == std::char_traits<char>::eof(), "invalid vector file length");
  std::ifstream native(argv[1], std::ios::binary | std::ios::ate);
  const auto length = native.tellg();
  require(length >= 96 && length < (1LL << 32), "invalid export length");
  std::vector<int8_t> bytes(static_cast<size_t>(length));
  native.seekg(0); native.read(reinterpret_cast<char *>(bytes.data()), length);
  require(bool(native), "incomplete native export");
  std::vector<int64_t> ids(rows);
  for (int64_t i = 0; i < rows; ++i) ids[i] = (1000000000001LL + i * 7) * (i % 2 ? -1 : 1);
  std::string error;
  const bool valid = oceanbase::common::obvsag::validate_hipvs_hnsw(bytes, vectors.data(), ids.data(), rows, dim, 16, error);
  require(valid, error.c_str());
  const auto params = std::string("{\"dtype\":\"float32\",\"metric_type\":\"l2\",\"dim\":") + std::to_string(dim) +
      ",\"use_old_serial_format\":true,\"hnsw\":{\"max_degree\":16,\"ef_construction\":200,\"use_conjugate_graph\":false,\"use_static\":false}}";
  const auto make = [&]() {
    auto created = vsag::Factory::CreateIndex("hnsw", params); require_result(created); return created.value();
  };
  vsag::Binary binary;
  binary.size = bytes.size(); binary.data.reset(new int8_t[bytes.size()]);
  std::memcpy(binary.data.get(), bytes.data(), bytes.size());
  vsag::BinarySet set; set.Set(vsag::HNSW_DATA, binary);
  auto index = make(); require_result(index->Deserialize(set));
  require(index->GetNumElements() == rows, "lost vectors on GPU graph import");
  auto cpu = make();
  auto input = vsag::Dataset::Make()->Owner(false)->Dim(dim)->NumElements(rows)->Ids(ids.data())->Float32Vectors(vectors.data());
  auto built = cpu->Build(input); require_result(built); require(built.value().empty(), "CPU build omitted IDs");
  const auto query = [&](const vsag::IndexPtr &idx, const float *q, int k) {
    auto data = vsag::Dataset::Make()->Owner(false)->Dim(dim)->NumElements(1)->Float32Vectors(q);
    auto result = idx->KnnSearch(data, k, "{\"hnsw\":{\"ef_search\":200,\"skip_ratio\":0}}");
    require_result(result); require(result.value()->GetDim() == k, "missing query results");
    return result.value();
  };
  for (int64_t i = 0; i < rows; ++i) {
    auto result = query(index, vectors.data() + i * dim, 1);
    require(result->GetIds()[0] == ids[i] && std::abs(result->GetDistances()[0]) < 1e-6,
            "wrong ID or vector after GPU graph import");
  }
  std::stringstream saved(std::ios::in | std::ios::out | std::ios::binary);
  require_result(index->Serialize(saved));
  auto restored = make(); saved.seekg(0); require_result(restored->Deserialize(saved));
  const int nq = std::min<int64_t>(30, rows), k = std::min<int64_t>(10, rows);
  int cpu_hits = 0, gpu_hits = 0;
  for (int qid = 0; qid < nq; ++qid) {
    std::vector<float> q(dim);
    for (int d = 0; d < dim; ++d) q[d] = vectors[qid * dim + d] * 0.9f;
    std::vector<std::pair<double, int64_t>> truth;
    for (int64_t i = 0; i < rows; ++i) {
      double distance = 0;
      for (int d = 0; d < dim; ++d) { double v = double(q[d]) - vectors[i * dim + d]; distance += v * v; }
      truth.emplace_back(distance, ids[i]);
    }
    std::sort(truth.begin(), truth.end());
    auto before = query(index, q.data(), k);
    auto after = query(restored, q.data(), k);
    auto baseline = query(cpu, q.data(), k);
    for (int j = 0; j < k; ++j) {
      require(before->GetIds()[j] == after->GetIds()[j], "persistence changed results");
      for (int expected = 0; expected < k; ++expected) {
        gpu_hits += before->GetIds()[j] == truth[expected].second;
        cpu_hits += baseline->GetIds()[j] == truth[expected].second;
      }
    }
  }
  const double gpu_recall = double(gpu_hits) / (nq * k), cpu_recall = double(cpu_hits) / (nq * k);
  require(gpu_recall >= 0.95 && cpu_recall >= 0.95, "smoke-test Recall@K below 0.95");
  std::vector<float> added(dim, 0.0f); int64_t new_id = 900000000000001LL;
  auto addition = vsag::Dataset::Make()->Owner(false)->Dim(dim)->NumElements(1)->Ids(&new_id)->Float32Vectors(added.data());
  auto added_result = restored->Add(addition); require_result(added_result);
  require(added_result.value().empty(), "incremental add failed");
  require(query(restored, added.data(), 1)->GetIds()[0] == new_id, "new document not searchable");
  std::cout << "REAL_GPU_EXPORT_VSAG_PASS rows=" << rows << " dim=" << dim
            << " all_ids=" << rows << " gpu_recall=" << gpu_recall << " cpu_recall=" << cpu_recall
            << " persistence=true incremental_add=true vsag=" << vsag::version() << std::endl;
  return 0;
} catch (const std::exception &error) { std::cerr << "FAIL " << error.what() << std::endl; return 1; }
