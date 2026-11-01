// CPU-only format compatibility probe, not a GPU performance measurement.
#include <vsag/vsag.h>
#include <vsag/constants.h>
#include <vsag/filter.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <vector>
#include "lib/vector/hipvs_native_hnsw.h"

template<class T> void append(std::vector<int8_t>& out, const T value) {
  const auto p = reinterpret_cast<const int8_t*>(&value);
  out.insert(out.end(), p, p + sizeof(T));
}
void require(bool ok, const char* label) {
  if (!ok) throw std::runtime_error(label);
  std::cout << "PASS " << label << std::endl;
}
template<class T> void success(const T& result, const char* label) {
  if (!result.has_value()) throw std::runtime_error(std::string(label) + ": " + result.error().message);
  require(true, label);
}
const std::string params = R"({"dtype":"float32","metric_type":"l2","dim":2,"use_old_serial_format":true,"hnsw":{"max_degree":16,"ef_construction":40,"use_conjugate_graph":false,"use_static":false}})";
vsag::IndexPtr make_index() {
  auto result = vsag::Factory::CreateIndex("hnsw", params);
  success(result, "create actual VSAG HNSW");
  return result.value();
}
struct Exclude : vsag::Filter {
  explicit Exclude(int64_t id) : id_(id) {}
  bool CheckValid(int64_t id) const override { return id != id_; }
  float ValidRatio() const override { return 0.75f; }
  int64_t id_;
};
vsag::DatasetPtr query(vsag::IndexPtr index, float* values, int64_t k, vsag::FilterPtr filter = nullptr) {
  auto data = vsag::Dataset::Make()->Owner(false)->Dim(2)->NumElements(1)->Float32Vectors(values);
  auto result = index->KnnSearch(data, k, R"({"hnsw":{"ef_search":40,"skip_ratio":0}})", filter);
  success(result, "KnnSearch");
  return result.value();
}
int main() try {
  std::cout << "VSAG version: " << vsag::version() << "\nBinarySet key: " << vsag::HNSW_DATA << std::endl;
  require(sizeof(size_t) == 8 && sizeof(float) == 4, "64-bit host native format");
  // Standard hnswlib header: six uint64s, int32 maxlevel, uint32 entrypoint,
  // three uint64s (maxM,maxM0,M), double multiplier, uint64 ef_construction.
  std::vector<int8_t> bytes;
  const uint64_t n = 3, m = 16, max_m0 = 32;
  const uint64_t offset_data = 4 + max_m0 * 4, label_offset = offset_data + 2 * 4;
  append<uint64_t>(bytes, 0); append<uint64_t>(bytes, n); append<uint64_t>(bytes, n);
  append<uint64_t>(bytes, label_offset + 8); append<uint64_t>(bytes, label_offset); append<uint64_t>(bytes, offset_data);
  append<int32_t>(bytes, 0); append<uint32_t>(bytes, 0);
  append<uint64_t>(bytes, m); append<uint64_t>(bytes, max_m0); append<uint64_t>(bytes, m);
  append<double>(bytes, 1.0 / std::log(double(m))); append<uint64_t>(bytes, 40);
  require(bytes.size() == 96, "standard hnswlib 96-byte header");
  const int64_t ids[] = {101, 10000000005LL, -27};
  const float vectors[][2] = {{0,0}, {2,0}, {0,4}};
  for (uint32_t i = 0; i < n; ++i) {
    append<uint32_t>(bytes, 2);
    for (uint32_t j = 0; j < n; ++j) if (j != i) append<uint32_t>(bytes, j);
    for (uint32_t j = 2; j < max_m0; ++j) append<uint32_t>(bytes, 0);
    append<float>(bytes, vectors[i][0]); append<float>(bytes, vectors[i][1]); append<uint64_t>(bytes, i);
  }
  for (uint32_t i = 0; i < n; ++i) append<uint32_t>(bytes, 0);
  const auto original = bytes;
  std::string validation_error;
  const auto reject = [&](std::vector<int8_t> broken, const char *label) {
    require(!oceanbase::common::obvsag::validate_hipvs_hnsw(broken, &vectors[0][0], ids, n, 2, m, validation_error), label);
  };
  auto broken = original; broken.resize(95); reject(broken, "reject truncated header");
  broken = original; broken.pop_back(); reject(broken, "reject truncated upper graph");
  broken = original; broken.push_back(0); reject(broken, "reject trailing garbage");
  broken = original; broken[16] = 2; reject(broken, "reject missing input vector");
  broken = original; broken[56] = 15; reject(broken, "reject incompatible M");
  broken = original; broken[96] = 33; reject(broken, "reject invalid degree");
  broken = original; broken[100] = 3; reject(broken, "reject invalid neighbor");
  broken = original; broken[96 + label_offset] = 3; reject(broken, "reject out-of-range source label");
  broken = original; broken[96 + label_offset + label_offset + 8] = 0; reject(broken, "reject duplicate source label");
  broken = original; broken[96 + offset_data] = 1; reject(broken, "reject altered FP32 vector");
  require(oceanbase::common::obvsag::validate_hipvs_hnsw(bytes, &vectors[0][0], ids, n, 2, m, validation_error), "validate and map complete GPU-format graph");
  vsag::Binary binary;
  binary.size = bytes.size(); binary.data.reset(new int8_t[bytes.size()]);
  std::memcpy(binary.data.get(), bytes.data(), bytes.size());
  vsag::BinarySet set; set.Set(vsag::HNSW_DATA, binary);
  {
    auto baseline = make_index();
    auto base = vsag::Dataset::Make()->Owner(false)->Dim(2)->NumElements(3)->Ids(ids)->Float32Vectors(&vectors[0][0]);
    success(baseline->Build(base), "CPU baseline build for format diagnosis");
    auto native = baseline->Serialize(); success(native, "CPU baseline binary serialize");
    auto blob = native.value().Get(vsag::HNSW_DATA);
    std::cout << "CPU header bytes:";
    for(size_t i=0;i<100 && i<blob.size;i++) std::cout << " " << std::hex << unsigned(uint8_t(blob.data[i]));
    std::cout << std::dec << " length=" << blob.size << std::endl;
  }
  auto index = make_index();
  success(index->Deserialize(set), "deserialize native hnswlib bytes via BinarySet");
  require(index->GetNumElements() == 3, "all 3 nodes imported");
  float zero[] = {0,0};
  auto result = query(index, zero, 3);
  require(result->GetDim() == 3, "all result IDs present");
  require(std::equal(ids, ids + 3, result->GetIds()), "noncontiguous signed 64-bit labels preserved");
  require(result->GetDistances()[0] == 0 && result->GetDistances()[1] == 4 && result->GetDistances()[2] == 16, "L2 distances preserved");
  auto filtered = query(index, zero, 2, std::make_shared<Exclude>(101));
  require(filtered->GetDim() == 2 && filtered->GetIds()[0] == ids[1] && filtered->GetIds()[1] == ids[2], "seekdb-style Filter CheckValid excludes VID");
  std::stringstream stream(std::ios::in | std::ios::out | std::ios::binary);
  success(index->Serialize(stream), "VSAG stream persistence after import");
  auto restored = make_index();
  stream.seekg(0);
  success(restored->Deserialize(stream), "VSAG stream restore");
  auto restored_result = query(restored, zero, 3);
  require(std::equal(ids, ids + 3, restored_result->GetIds()), "stream roundtrip IDs and ordering");
  int64_t add_id = 90000000007LL;
  float add_vector[] = {0.1f,0.1f};
  auto addition = vsag::Dataset::Make()->Owner(false)->Dim(2)->NumElements(1)->Ids(&add_id)->Float32Vectors(add_vector);
  auto added = restored->Add(addition);
  success(added, "incremental Add after imported/restored graph");
  require(added.value().empty() && restored->GetNumElements() == 4, "no Add failures, count is 4");
  auto new_query = query(restored, add_vector, 1);
  require(new_query->GetIds()[0] == add_id && new_query->GetDistances()[0] == 0, "incremental new VID searchable");
  std::cout << "COMPATIBILITY PASS; GPU execution not tested" << std::endl;
  return 0;
} catch (const std::exception& error) { std::cerr << "FAIL " << error.what() << std::endl; return 1; }
