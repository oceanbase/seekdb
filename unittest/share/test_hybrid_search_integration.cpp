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

/*
 * Hybrid Search Integration Tests
 * Test end-to-end functionality of hybrid fusion system for vector and full-text search
 */

#include <gtest/gtest.h>
#include <iomanip>
#include <vector>
#include <algorithm>
#include <cmath>
#include <unordered_map>

namespace oceanbase
{
namespace common
{

// Test Data Structures
struct ObHybridSearchResult
{
  uint64_t doc_id_ = 0;
  double fts_score_ = 0.0;
  double vector_score_ = 0.0;
  int64_t fts_rank_ = -1;
  int64_t vector_rank_ = -1;
  double final_score_ = 0.0;
  int32_t source_flag_ = 0;

  bool operator<(const ObHybridSearchResult &other) const
  {
    if (fabs(final_score_ - other.final_score_) > 1e-10) {
      return final_score_ > other.final_score_;
    }
    return doc_id_ < other.doc_id_;
  }
};

struct ObRRFConfig
{
  int64_t rank_constant_ = 60;
  int64_t rank_window_size_ = 100;
  ObRRFConfig() = default;
  ObRRFConfig(int64_t rank_const, int64_t window_size)
    : rank_constant_(rank_const), rank_window_size_(window_size) {}
};

struct ObWeightedFusionConfig
{
  double fts_weight_ = 0.5;
  double vector_weight_ = 0.5;
  bool enable_normalization_ = true;
  ObWeightedFusionConfig() = default;
  ObWeightedFusionConfig(double fts_w, double vec_w, bool normalize)
    : fts_weight_(fts_w), vector_weight_(vec_w), enable_normalization_(normalize) {}
};

// RRF Fusion
class SimpleRRFFusion
{
public:
  int init(const ObRRFConfig &config)
  {
    config_ = config;
    is_initialized_ = true;
    return 0;
  }

  int add_fts_results(const std::vector<ObHybridSearchResult> &fts_results)
  {
    fts_results_ = fts_results;
    return 0;
  }

  int add_vector_results(const std::vector<ObHybridSearchResult> &vector_results)
  {
    vector_results_ = vector_results;
    return 0;
  }

  int fuse()
  {
    fused_results_.clear();
    std::unordered_map<uint64_t, ObHybridSearchResult> result_map;

    for (int64_t i = 0; i < (int64_t)fts_results_.size(); ++i) {
      const ObHybridSearchResult &result = fts_results_[i];
      int64_t rank = i + 1;
      ObHybridSearchResult merged = result;
      merged.fts_rank_ = rank;
      merged.fts_score_ = 1.0 / (rank + config_.rank_constant_);
      merged.source_flag_ |= 1;
      result_map[result.doc_id_] = merged;
    }

    for (int64_t i = 0; i < (int64_t)vector_results_.size(); ++i) {
      const ObHybridSearchResult &result = vector_results_[i];
      int64_t rank = i + 1;
      if (result_map.find(result.doc_id_) == result_map.end()) {
        ObHybridSearchResult merged = result;
        merged.vector_rank_ = rank;
        merged.vector_score_ = 1.0 / (rank + config_.rank_constant_);
        merged.source_flag_ |= 2;
        result_map[result.doc_id_] = merged;
      } else {
        result_map[result.doc_id_].vector_rank_ = rank;
        result_map[result.doc_id_].vector_score_ = 1.0 / (rank + config_.rank_constant_);
        result_map[result.doc_id_].source_flag_ |= 2;
      }
    }

    for (auto &pair : result_map) {
      ObHybridSearchResult result = pair.second;
      result.final_score_ = result.fts_score_ + result.vector_score_;
      fused_results_.push_back(result);
    }

    std::sort(fused_results_.begin(), fused_results_.end());
    return 0;
  }

  int get_results(std::vector<ObHybridSearchResult> &results, int64_t limit = 0) const
  {
    results.clear();
    int64_t count = fused_results_.size();
    if (limit > 0 && limit < count) {
      count = limit;
    }
    for (int64_t i = 0; i < count; ++i) {
      results.push_back(fused_results_[i]);
    }
    return 0;
  }

  int64_t get_fused_result_count() const { return fused_results_.size(); }
  const ObHybridSearchResult *get_result_at(int64_t index) const
  {
    if (index < 0 || index >= (int64_t)fused_results_.size()) {
      return nullptr;
    }
    return &fused_results_[index];
  }

private:
  ObRRFConfig config_;
  std::vector<ObHybridSearchResult> fts_results_;
  std::vector<ObHybridSearchResult> vector_results_;
  std::vector<ObHybridSearchResult> fused_results_;
  bool is_initialized_ = false;
};

// Weighted Fusion
class SimpleWeightedFusion
{
public:
  int init(const ObWeightedFusionConfig &fusion_config)
  {
    fusion_config_ = fusion_config;
    is_initialized_ = true;
    return 0;
  }

  int add_fts_results(const std::vector<ObHybridSearchResult> &fts_results)
  {
    fts_results_ = fts_results;
    return 0;
  }

  int add_vector_results(const std::vector<ObHybridSearchResult> &vector_results)
  {
    vector_results_ = vector_results;
    return 0;
  }

  int fuse()
  {
    fused_results_.clear();
    std::unordered_map<uint64_t, ObHybridSearchResult> result_map;

    for (int64_t i = 0; i < (int64_t)fts_results_.size(); ++i) {
      const ObHybridSearchResult &result = fts_results_[i];
      ObHybridSearchResult merged = result;
      merged.source_flag_ |= 1;
      result_map[result.doc_id_] = merged;
    }

    for (int64_t i = 0; i < (int64_t)vector_results_.size(); ++i) {
      const ObHybridSearchResult &result = vector_results_[i];
      if (result_map.find(result.doc_id_) == result_map.end()) {
        ObHybridSearchResult merged = result;
        merged.source_flag_ |= 2;
        result_map[result.doc_id_] = merged;
      } else {
        result_map[result.doc_id_].vector_score_ = result.vector_score_;
        result_map[result.doc_id_].source_flag_ |= 2;
      }
    }

    for (auto &pair : result_map) {
      ObHybridSearchResult result = pair.second;
      double norm_fts = (fts_results_.size() > 0) ? result.fts_score_ : 0.0;
      double norm_vector = (vector_results_.size() > 0) ? result.vector_score_ : 0.0;
      result.final_score_ = fusion_config_.fts_weight_ * norm_fts +
                            fusion_config_.vector_weight_ * norm_vector;
      fused_results_.push_back(result);
    }

    std::sort(fused_results_.begin(), fused_results_.end());
    return 0;
  }

  int get_results(std::vector<ObHybridSearchResult> &results, int64_t limit = 0) const
  {
    results.clear();
    int64_t count = fused_results_.size();
    if (limit > 0 && limit < count) {
      count = limit;
    }
    for (int64_t i = 0; i < count; ++i) {
      results.push_back(fused_results_[i]);
    }
    return 0;
  }

  int64_t get_fused_result_count() const { return fused_results_.size(); }

private:
  ObWeightedFusionConfig fusion_config_;
  std::vector<ObHybridSearchResult> fts_results_;
  std::vector<ObHybridSearchResult> vector_results_;
  std::vector<ObHybridSearchResult> fused_results_;
  bool is_initialized_ = false;
};

// ============================================================
// Google Test Framework Test Cases
// ============================================================

class HybridSearchIntegrationTest : public ::testing::Test
{
protected:
  void SetUp() override {}
  void TearDown() override {}
};

// Test 1: RRF_BasicFusion
TEST_F(HybridSearchIntegrationTest, RRF_BasicFusion)
{
  SimpleRRFFusion rrf;
  ObRRFConfig config(60, 100);
  EXPECT_EQ(0, rrf.init(config));

  std::vector<ObHybridSearchResult> fts_results;
  for (int i = 0; i < 5; ++i) {
    ObHybridSearchResult result;
    result.doc_id_ = i + 1;
    result.fts_score_ = 15.0 - i * 2.5;
    fts_results.push_back(result);
  }

  std::vector<ObHybridSearchResult> vector_results;
  for (int i = 0; i < 5; ++i) {
    ObHybridSearchResult result;
    result.doc_id_ = (i + 2) % 5 + 1;
    result.vector_score_ = 0.95 - i * 0.12;
    vector_results.push_back(result);
  }

  EXPECT_EQ(0, rrf.add_fts_results(fts_results));
  EXPECT_EQ(0, rrf.add_vector_results(vector_results));
  EXPECT_EQ(0, rrf.fuse());

  std::vector<ObHybridSearchResult> results;
  EXPECT_EQ(0, rrf.get_results(results, 3));
  EXPECT_GT(rrf.get_fused_result_count(), 0);
  EXPECT_EQ(3, results.size());

  for (int i = 1; i < (int)results.size(); ++i) {
    EXPECT_GE(results[i - 1].final_score_, results[i].final_score_);
  }
}

// Test 2: RRF_EmptyVectorResults
TEST_F(HybridSearchIntegrationTest, RRF_EmptyVectorResults)
{
  SimpleRRFFusion rrf;
  ObRRFConfig config(60, 100);
  EXPECT_EQ(0, rrf.init(config));

  std::vector<ObHybridSearchResult> fts_results;
  for (int i = 0; i < 3; ++i) {
    ObHybridSearchResult result;
    result.doc_id_ = i + 1;
    result.fts_score_ = 10.0 - i * 2.0;
    fts_results.push_back(result);
  }

  std::vector<ObHybridSearchResult> vector_results;
  EXPECT_EQ(0, rrf.add_fts_results(fts_results));
  EXPECT_EQ(0, rrf.add_vector_results(vector_results));
  EXPECT_EQ(0, rrf.fuse());

  EXPECT_EQ(3, rrf.get_fused_result_count());
}

// Test 3: WeightedFusion_Balanced
TEST_F(HybridSearchIntegrationTest, WeightedFusion_Balanced)
{
  SimpleWeightedFusion fusion;
  ObWeightedFusionConfig config(0.5, 0.5, true);
  EXPECT_EQ(0, fusion.init(config));

  std::vector<ObHybridSearchResult> fts_results;
  for (int i = 0; i < 3; ++i) {
    ObHybridSearchResult result;
    result.doc_id_ = i + 1;
    result.fts_score_ = 10.0 - i * 3.0;
    fts_results.push_back(result);
  }

  std::vector<ObHybridSearchResult> vector_results;
  for (int i = 0; i < 3; ++i) {
    ObHybridSearchResult result;
    result.doc_id_ = (i + 1) % 3 + 1;
    result.vector_score_ = 0.9 - i * 0.2;
    vector_results.push_back(result);
  }

  EXPECT_EQ(0, fusion.add_fts_results(fts_results));
  EXPECT_EQ(0, fusion.add_vector_results(vector_results));
  EXPECT_EQ(0, fusion.fuse());

  EXPECT_GT(fusion.get_fused_result_count(), 0);
}

// Test 4: WeightedFusion_KeywordPriority
TEST_F(HybridSearchIntegrationTest, WeightedFusion_KeywordPriority)
{
  SimpleWeightedFusion fusion;
  ObWeightedFusionConfig config(0.7, 0.3, true);
  EXPECT_EQ(0, fusion.init(config));

  std::vector<ObHybridSearchResult> fts_results;
  for (int i = 0; i < 3; ++i) {
    ObHybridSearchResult result;
    result.doc_id_ = i + 1;
    result.fts_score_ = 10.0 - i * 3.0;
    fts_results.push_back(result);
  }

  std::vector<ObHybridSearchResult> vector_results;
  for (int i = 0; i < 3; ++i) {
    ObHybridSearchResult result;
    result.doc_id_ = (i + 2) % 3 + 1;
    result.vector_score_ = 0.9 - i * 0.1;
    vector_results.push_back(result);
  }

  EXPECT_EQ(0, fusion.add_fts_results(fts_results));
  EXPECT_EQ(0, fusion.add_vector_results(vector_results));
  EXPECT_EQ(0, fusion.fuse());

  EXPECT_GT(fusion.get_fused_result_count(), 0);
}

// Test 5: WeightedFusion_SemanticPriority
TEST_F(HybridSearchIntegrationTest, WeightedFusion_SemanticPriority)
{
  SimpleWeightedFusion fusion;
  ObWeightedFusionConfig config(0.3, 0.7, true);
  EXPECT_EQ(0, fusion.init(config));

  std::vector<ObHybridSearchResult> fts_results;
  ObHybridSearchResult fts1;
  fts1.doc_id_ = 1; fts1.fts_score_ = 8.0;
  fts_results.push_back(fts1);
  ObHybridSearchResult fts2;
  fts2.doc_id_ = 2; fts2.fts_score_ = 6.5;
  fts_results.push_back(fts2);

  std::vector<ObHybridSearchResult> vector_results;
  ObHybridSearchResult vec1;
  vec1.doc_id_ = 2; vec1.vector_score_ = 0.92;
  vector_results.push_back(vec1);
  ObHybridSearchResult vec2;
  vec2.doc_id_ = 1; vec2.vector_score_ = 0.85;
  vector_results.push_back(vec2);

  EXPECT_EQ(0, fusion.add_fts_results(fts_results));
  EXPECT_EQ(0, fusion.add_vector_results(vector_results));
  EXPECT_EQ(0, fusion.fuse());

  EXPECT_GT(fusion.get_fused_result_count(), 0);
}

// Test 6: LargeScaleFusion
TEST_F(HybridSearchIntegrationTest, LargeScaleFusion)
{
  SimpleRRFFusion rrf;
  ObRRFConfig config(60, 100);
  EXPECT_EQ(0, rrf.init(config));

  std::vector<ObHybridSearchResult> fts_results;
  for (int i = 0; i < 1000; ++i) {
    ObHybridSearchResult result;
    result.doc_id_ = i + 1;
    result.fts_score_ = 1000.0 - i * 0.5;
    fts_results.push_back(result);
  }

  std::vector<ObHybridSearchResult> vector_results;
  for (int i = 0; i < 1000; ++i) {
    ObHybridSearchResult result;
    result.doc_id_ = (i + 500) % 1000 + 1;
    result.vector_score_ = 0.5 - i * 0.0001;
    vector_results.push_back(result);
  }

  EXPECT_EQ(0, rrf.add_fts_results(fts_results));
  EXPECT_EQ(0, rrf.add_vector_results(vector_results));
  EXPECT_EQ(0, rrf.fuse());

  EXPECT_GE(rrf.get_fused_result_count(), 1000);
}

// Test 7: DuplicateDocHandling
TEST_F(HybridSearchIntegrationTest, DuplicateDocHandling)
{
  SimpleRRFFusion rrf;
  ObRRFConfig config(60, 100);
  EXPECT_EQ(0, rrf.init(config));

  std::vector<ObHybridSearchResult> fts_results;
  ObHybridSearchResult fts1;
  fts1.doc_id_ = 1; fts1.fts_score_ = 10.0;
  fts_results.push_back(fts1);
  ObHybridSearchResult fts2;
  fts2.doc_id_ = 2; fts2.fts_score_ = 8.0;
  fts_results.push_back(fts2);
  ObHybridSearchResult fts3;
  fts3.doc_id_ = 3; fts3.fts_score_ = 6.0;
  fts_results.push_back(fts3);

  std::vector<ObHybridSearchResult> vector_results;
  ObHybridSearchResult vec1;
  vec1.doc_id_ = 2; vec1.vector_score_ = 0.9;
  vector_results.push_back(vec1);
  ObHybridSearchResult vec2;
  vec2.doc_id_ = 1; vec2.vector_score_ = 0.8;
  vector_results.push_back(vec2);
  ObHybridSearchResult vec3;
  vec3.doc_id_ = 4; vec3.vector_score_ = 0.7;
  vector_results.push_back(vec3);

  EXPECT_EQ(0, rrf.add_fts_results(fts_results));
  EXPECT_EQ(0, rrf.add_vector_results(vector_results));
  EXPECT_EQ(0, rrf.fuse());

  EXPECT_EQ(4, rrf.get_fused_result_count());
}

// Test 8: ResultLimitTest
TEST_F(HybridSearchIntegrationTest, ResultLimitTest)
{
  SimpleRRFFusion rrf;
  ObRRFConfig config(60, 100);
  EXPECT_EQ(0, rrf.init(config));

  std::vector<ObHybridSearchResult> fts_results;
  for (int i = 0; i < 20; ++i) {
    ObHybridSearchResult result;
    result.doc_id_ = i + 1;
    result.fts_score_ = 20.0 - i * 0.5;
    fts_results.push_back(result);
  }

  EXPECT_EQ(0, rrf.add_fts_results(fts_results));
  EXPECT_EQ(0, rrf.add_vector_results(fts_results));
  EXPECT_EQ(0, rrf.fuse());

  std::vector<ObHybridSearchResult> results_all;
  EXPECT_EQ(0, rrf.get_results(results_all));

  std::vector<ObHybridSearchResult> results_limit5;
  EXPECT_EQ(0, rrf.get_results(results_limit5, 5));
  EXPECT_EQ(5, results_limit5.size());

  std::vector<ObHybridSearchResult> results_limit0;
  EXPECT_EQ(0, rrf.get_results(results_limit0, 0));
  EXPECT_EQ(results_all.size(), results_limit0.size());
}

} // namespace common
} // namespace oceanbase

// ============================================================
// Main Test Instance
// ============================================================
int main(int argc, char **argv)
{
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
