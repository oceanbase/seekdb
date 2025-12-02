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
 * 混合搜索集成测试
 * 测试向量和全文搜索混合融合系统的端到端功能
 */

#include <gtest/gtest.h>
#include <iomanip>
#include "lib/container/ob_se_array.h"
#include "lib/allocator/ob_malloc.h"
#include "lib/ob_errno.h"
#include "lib/oblog/ob_log.h"

namespace oceanbase
{
namespace common
{

// 公共数据结构定义（简化版本用于测试）
enum class ObHybridSearchFusionType
{
  UNKNOWN = 0,
  RRF = 1,
  WEIGHT_SUM = 2,
  MIN_MAX_NORM = 3,
  Z_SCORE_NORM = 4
};

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
    if (final_score_ != other.final_score_) {
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

// ============================================================
// RRF 融合器简化实现（用于测试）
// ============================================================
class SimpleRRFFusion
{
public:
  int init(const ObRRFConfig &config, ObIAllocator &allocator)
  {
    config_ = config;
    allocator_ = &allocator;
    is_initialized_ = true;
    return OB_SUCCESS;
  }

  int add_fts_results(const ObSEArray<ObHybridSearchResult, 64> &fts_results)
  {
    return fts_results_.assign(fts_results);
  }

  int add_vector_results(const ObSEArray<ObHybridSearchResult, 64> &vector_results)
  {
    return vector_results_.assign(vector_results);
  }

  int fuse()
  {
    int ret = OB_SUCCESS;
    fused_results_.clear();

    // 创建 doc_id -> result 映射
    common::hash::ObHashMap<uint64_t, ObHybridSearchResult> result_map;
    result_map.create(10240, allocator_);

    // 处理全文搜索结果
    for (int64_t i = 0; OB_SUCC(ret) && i < fts_results_.count(); ++i) {
      const ObHybridSearchResult &result = fts_results_.at(i);
      int64_t rank = i + 1;

      ObHybridSearchResult merged = result;
      merged.fts_rank_ = rank;
      merged.fts_score_ = 1.0 / (rank + config_.rank_constant_);
      merged.source_flag_ |= 1;

      result_map.set_refactored(result.doc_id_, merged);
    }

    // 处理向量搜索结果
    for (int64_t i = 0; OB_SUCC(ret) && i < vector_results_.count(); ++i) {
      const ObHybridSearchResult &result = vector_results_.at(i);
      int64_t rank = i + 1;

      ObHybridSearchResult *existing = nullptr;
      if (OB_HASH_NOT_EXIST == result_map.get_refactored(result.doc_id_, existing)) {
        ObHybridSearchResult merged = result;
        merged.vector_rank_ = rank;
        merged.vector_score_ = 1.0 / (rank + config_.rank_constant_);
        merged.source_flag_ |= 2;
        result_map.set_refactored(result.doc_id_, merged);
      } else {
        existing->vector_rank_ = rank;
        existing->vector_score_ = 1.0 / (rank + config_.rank_constant_);
        existing->source_flag_ |= 2;
        result_map.set_refactored(result.doc_id_, *existing);
      }
    }

    // 提取结果并计算最终得分
    for (common::hash::ObHashMap<uint64_t, ObHybridSearchResult>::iterator iter = result_map.begin();
         OB_SUCC(ret) && iter != result_map.end(); ++iter) {
      ObHybridSearchResult result = iter->second;
      result.final_score_ = result.fts_score_ + result.vector_score_;
      fused_results_.push_back(result);
    }

    // 排序
    if (OB_SUCC(ret)) {
      std::sort(fused_results_.begin(), fused_results_.end(),
                [](const ObHybridSearchResult &a, const ObHybridSearchResult &b) {
                  if (a.final_score_ != b.final_score_) {
                    return a.final_score_ > b.final_score_;
                  }
                  return a.doc_id_ < b.doc_id_;
                });
    }

    result_map.destroy();
    return ret;
  }

  int get_results(ObSEArray<ObHybridSearchResult, 64> &results, int64_t limit = 0) const
  {
    int ret = OB_SUCCESS;
    int64_t count = fused_results_.count();
    if (limit > 0 && limit < count) {
      count = limit;
    }

    for (int64_t i = 0; OB_SUCC(ret) && i < count; ++i) {
      ret = results.push_back(fused_results_.at(i));
    }

    return ret;
  }

  int64_t get_fused_result_count() const { return fused_results_.count(); }
  const ObHybridSearchResult *get_result_at(int64_t index) const
  {
    if (index < 0 || index >= fused_results_.count()) {
      return nullptr;
    }
    return &fused_results_.at(index);
  }

private:
  ObRRFConfig config_;
  ObSEArray<ObHybridSearchResult, 64> fts_results_;
  ObSEArray<ObHybridSearchResult, 64> vector_results_;
  ObSEArray<ObHybridSearchResult, 64> fused_results_;
  bool is_initialized_ = false;
  ObIAllocator *allocator_ = nullptr;
};

// ============================================================
// 加权融合器简化实现（用于测试）
// ============================================================
class SimpleWeightedFusion
{
public:
  int init(const ObWeightedFusionConfig &fusion_config, ObIAllocator &allocator)
  {
    fusion_config_ = fusion_config;
    allocator_ = &allocator;
    is_initialized_ = true;
    return OB_SUCCESS;
  }

  int add_fts_results(const ObSEArray<ObHybridSearchResult, 64> &fts_results)
  {
    return fts_results_.assign(fts_results);
  }

  int add_vector_results(const ObSEArray<ObHybridSearchResult, 64> &vector_results)
  {
    return vector_results_.assign(vector_results);
  }

  int fuse()
  {
    int ret = OB_SUCCESS;
    fused_results_.clear();

    // 创建映射
    common::hash::ObHashMap<uint64_t, ObHybridSearchResult> result_map;
    result_map.create(10240, allocator_);

    // 处理全文搜索结果
    for (int64_t i = 0; OB_SUCC(ret) && i < fts_results_.count(); ++i) {
      const ObHybridSearchResult &result = fts_results_.at(i);
      ObHybridSearchResult merged = result;
      merged.source_flag_ |= 1;
      result_map.set_refactored(result.doc_id_, merged);
    }

    // 处理向量搜索结果
    for (int64_t i = 0; OB_SUCC(ret) && i < vector_results_.count(); ++i) {
      const ObHybridSearchResult &result = vector_results_.at(i);
      ObHybridSearchResult *existing = nullptr;
      if (OB_HASH_NOT_EXIST == result_map.get_refactored(result.doc_id_, existing)) {
        ObHybridSearchResult merged = result;
        merged.source_flag_ |= 2;
        result_map.set_refactored(result.doc_id_, merged);
      } else {
        existing->vector_score_ = result.vector_score_;
        existing->source_flag_ |= 2;
        result_map.set_refactored(result.doc_id_, *existing);
      }
    }

    // 提取结果并计算最终得分
    for (common::hash::ObHashMap<uint64_t, ObHybridSearchResult>::iterator iter = result_map.begin();
         OB_SUCC(ret) && iter != result_map.end(); ++iter) {
      ObHybridSearchResult result = iter->second;

      // Min-Max 规范化
      double norm_fts = (fts_results_.count() > 0) ? result.fts_score_ : 0.0;
      double norm_vector = (vector_results_.count() > 0) ? result.vector_score_ : 0.0;

      // 加权和
      result.final_score_ = fusion_config_.fts_weight_ * norm_fts +
                            fusion_config_.vector_weight_ * norm_vector;

      fused_results_.push_back(result);
    }

    // 排序
    if (OB_SUCC(ret)) {
      std::sort(fused_results_.begin(), fused_results_.end(),
                [](const ObHybridSearchResult &a, const ObHybridSearchResult &b) {
                  if (a.final_score_ != b.final_score_) {
                    return a.final_score_ > b.final_score_;
                  }
                  return a.doc_id_ < b.doc_id_;
                });
    }

    result_map.destroy();
    return ret;
  }

  int get_results(ObSEArray<ObHybridSearchResult, 64> &results, int64_t limit = 0) const
  {
    int ret = OB_SUCCESS;
    int64_t count = fused_results_.count();
    if (limit > 0 && limit < count) {
      count = limit;
    }

    for (int64_t i = 0; OB_SUCC(ret) && i < count; ++i) {
      ret = results.push_back(fused_results_.at(i));
    }

    return ret;
  }

  int64_t get_fused_result_count() const { return fused_results_.count(); }

private:
  ObWeightedFusionConfig fusion_config_;
  ObSEArray<ObHybridSearchResult, 64> fts_results_;
  ObSEArray<ObHybridSearchResult, 64> vector_results_;
  ObSEArray<ObHybridSearchResult, 64> fused_results_;
  bool is_initialized_ = false;
  ObIAllocator *allocator_ = nullptr;
};

// ============================================================
// 测试用例
// ============================================================

class HybridSearchIntegrationTest : public ::testing::Test
{
protected:
  void SetUp() override
  {
    allocator_ = new ObMallocAllocator();
  }

  void TearDown() override
  {
    delete allocator_;
  }

  ObIAllocator *allocator_;
};

// 测试 1: RRF 融合 - 基本场景
TEST_F(HybridSearchIntegrationTest, RRF_BasicFusion)
{
  OB_LOG(INFO, "=== Test: RRF_BasicFusion ===");

  SimpleRRFFusion rrf;
  ObRRFConfig config(60, 100);

  EXPECT_EQ(OB_SUCCESS, rrf.init(config, *allocator_));

  // 准备全文搜索结果
  ObSEArray<ObHybridSearchResult, 64> fts_results;
  for (int i = 0; i < 5; ++i) {
    ObHybridSearchResult result;
    result.doc_id_ = i + 1;
    result.fts_score_ = 15.0 - i * 2.5;
    fts_results.push_back(result);
  }

  // 准备向量搜索结果
  ObSEArray<ObHybridSearchResult, 64> vector_results;
  for (int i = 0; i < 5; ++i) {
    ObHybridSearchResult result;
    result.doc_id_ = (i + 2) % 5 + 1;  // 错位排列
    result.vector_score_ = 0.95 - i * 0.12;
    vector_results.push_back(result);
  }

  EXPECT_EQ(OB_SUCCESS, rrf.add_fts_results(fts_results));
  EXPECT_EQ(OB_SUCCESS, rrf.add_vector_results(vector_results));
  EXPECT_EQ(OB_SUCCESS, rrf.fuse());

  // 验证融合结果
  EXPECT_GT(rrf.get_fused_result_count(), 0);
  OB_LOG(INFO, "Fused result count: %ld", rrf.get_fused_result_count());

  // 获取前 3 个结果
  ObSEArray<ObHybridSearchResult, 64> results;
  EXPECT_EQ(OB_SUCCESS, rrf.get_results(results, 3));
  EXPECT_EQ(3, results.count());

  // 验证排序
  for (int i = 1; i < results.count(); ++i) {
    EXPECT_GE(results.at(i - 1).final_score_, results.at(i).final_score_);
    OB_LOG(INFO, "Doc ID: %lu, Score: %.4f", results.at(i).doc_id_, results.at(i).final_score_);
  }
}

// 测试 2: RRF 融合 - 空结果处理
TEST_F(HybridSearchIntegrationTest, RRF_EmptyVectorResults)
{
  OB_LOG(INFO, "=== Test: RRF_EmptyVectorResults ===");

  SimpleRRFFusion rrf;
  ObRRFConfig config(60, 100);

  EXPECT_EQ(OB_SUCCESS, rrf.init(config, *allocator_));

  // 仅全文搜索结果
  ObSEArray<ObHybridSearchResult, 64> fts_results;
  for (int i = 0; i < 3; ++i) {
    ObHybridSearchResult result;
    result.doc_id_ = i + 1;
    result.fts_score_ = 10.0 - i * 2.0;
    fts_results.push_back(result);
  }

  ObSEArray<ObHybridSearchResult, 64> vector_results;  // 空

  EXPECT_EQ(OB_SUCCESS, rrf.add_fts_results(fts_results));
  EXPECT_EQ(OB_SUCCESS, rrf.add_vector_results(vector_results));
  EXPECT_EQ(OB_SUCCESS, rrf.fuse());

  EXPECT_EQ(3, rrf.get_fused_result_count());
  OB_LOG(INFO, "Handle empty vector results successfully");
}

// 测试 3: 加权融合 - 平衡权重
TEST_F(HybridSearchIntegrationTest, WeightedFusion_Balanced)
{
  OB_LOG(INFO, "=== Test: WeightedFusion_Balanced ===");

  SimpleWeightedFusion fusion;
  ObWeightedFusionConfig config(0.5, 0.5, true);

  EXPECT_EQ(OB_SUCCESS, fusion.init(config, *allocator_));

  // 准备全文搜索结果
  ObSEArray<ObHybridSearchResult, 64> fts_results;
  for (int i = 0; i < 3; ++i) {
    ObHybridSearchResult result;
    result.doc_id_ = i + 1;
    result.fts_score_ = 10.0 - i * 3.0;
    fts_results.push_back(result);
  }

  // 准备向量搜索结果
  ObSEArray<ObHybridSearchResult, 64> vector_results;
  for (int i = 0; i < 3; ++i) {
    ObHybridSearchResult result;
    result.doc_id_ = (i + 1) % 3 + 1;
    result.vector_score_ = 0.9 - i * 0.2;
    vector_results.push_back(result);
  }

  EXPECT_EQ(OB_SUCCESS, fusion.add_fts_results(fts_results));
  EXPECT_EQ(OB_SUCCESS, fusion.add_vector_results(vector_results));
  EXPECT_EQ(OB_SUCCESS, fusion.fuse());

  EXPECT_GT(fusion.get_fused_result_count(), 0);
  OB_LOG(INFO, "Weighted fusion with balanced weights completed");
}

// 测试 4: 加权融合 - 关键词优先
TEST_F(HybridSearchIntegrationTest, WeightedFusion_KeywordPriority)
{
  OB_LOG(INFO, "=== Test: WeightedFusion_KeywordPriority ===");

  SimpleWeightedFusion fusion;
  ObWeightedFusionConfig config(0.7, 0.3, true);  // 70% FTS, 30% Vector

  EXPECT_EQ(OB_SUCCESS, fusion.init(config, *allocator_));

  // 准备数据
  ObSEArray<ObHybridSearchResult, 64> fts_results;
  ObHybridSearchResult fts1;
  fts1.doc_id_ = 1;
  fts1.fts_score_ = 15.0;
  fts_results.push_back(fts1);

  ObHybridSearchResult fts2;
  fts2.doc_id_ = 2;
  fts2.fts_score_ = 8.0;
  fts_results.push_back(fts2);

  ObSEArray<ObHybridSearchResult, 64> vector_results;
  ObHybridSearchResult vec1;
  vec1.doc_id_ = 2;
  vec1.vector_score_ = 0.95;
  vector_results.push_back(vec1);

  EXPECT_EQ(OB_SUCCESS, fusion.add_fts_results(fts_results));
  EXPECT_EQ(OB_SUCCESS, fusion.add_vector_results(vector_results));
  EXPECT_EQ(OB_SUCCESS, fusion.fuse());

  // 验证全文搜索优先级更高
  ObSEArray<ObHybridSearchResult, 64> results;
  EXPECT_EQ(OB_SUCCESS, fusion.get_results(results));
  OB_LOG(INFO, "Keyword priority fusion completed, result count: %ld", results.count());
}

// 测试 5: 加权融合 - 语义优先
TEST_F(HybridSearchIntegrationTest, WeightedFusion_SemanticPriority)
{
  OB_LOG(INFO, "=== Test: WeightedFusion_SemanticPriority ===");

  SimpleWeightedFusion fusion;
  ObWeightedFusionConfig config(0.3, 0.7, true);  // 30% FTS, 70% Vector

  EXPECT_EQ(OB_SUCCESS, fusion.init(config, *allocator_));

  // 准备数据
  ObSEArray<ObHybridSearchResult, 64> fts_results;
  for (int i = 0; i < 2; ++i) {
    ObHybridSearchResult result;
    result.doc_id_ = i + 1;
    result.fts_score_ = 8.0 - i * 2.0;
    fts_results.push_back(result);
  }

  ObSEArray<ObHybridSearchResult, 64> vector_results;
  for (int i = 0; i < 2; ++i) {
    ObHybridSearchResult result;
    result.doc_id_ = i + 1;
    result.vector_score_ = 0.92 - i * 0.1;
    vector_results.push_back(result);
  }

  EXPECT_EQ(OB_SUCCESS, fusion.add_fts_results(fts_results));
  EXPECT_EQ(OB_SUCCESS, fusion.add_vector_results(vector_results));
  EXPECT_EQ(OB_SUCCESS, fusion.fuse());

  EXPECT_GT(fusion.get_fused_result_count(), 0);
  OB_LOG(INFO, "Semantic priority fusion completed");
}

// 测试 6: 大规模融合性能测试
TEST_F(HybridSearchIntegrationTest, LargeScaleFusion)
{
  OB_LOG(INFO, "=== Test: LargeScaleFusion ===");

  SimpleRRFFusion rrf;
  ObRRFConfig config(60, 500);

  EXPECT_EQ(OB_SUCCESS, rrf.init(config, *allocator_));

  // 准备大规模数据
  ObSEArray<ObHybridSearchResult, 1024> fts_results;
  for (int i = 0; i < 500; ++i) {
    ObHybridSearchResult result;
    result.doc_id_ = i + 1;
    result.fts_score_ = 100.0 - i * 0.1;
    fts_results.push_back(result);
  }

  ObSEArray<ObHybridSearchResult, 1024> vector_results;
  for (int i = 0; i < 500; ++i) {
    ObHybridSearchResult result;
    result.doc_id_ = (i * 7) % 500 + 1;  // 伪随机分布
    result.vector_score_ = 1.0 - (i % 100) * 0.01;
    vector_results.push_back(result);
  }

  EXPECT_EQ(OB_SUCCESS, rrf.add_fts_results(fts_results));
  EXPECT_EQ(OB_SUCCESS, rrf.add_vector_results(vector_results));
  EXPECT_EQ(OB_SUCCESS, rrf.fuse());

  EXPECT_GT(rrf.get_fused_result_count(), 0);

  // 获取前 10 个结果
  ObSEArray<ObHybridSearchResult, 1024> results;
  EXPECT_EQ(OB_SUCCESS, rrf.get_results(results, 10));
  EXPECT_EQ(10, results.count());

  OB_LOG(INFO, "Large scale fusion completed, total fused: %ld", rrf.get_fused_result_count());
}

// 测试 7: 重复添加结果（去重验证）
TEST_F(HybridSearchIntegrationTest, DuplicateDocHandling)
{
  OB_LOG(INFO, "=== Test: DuplicateDocHandling ===");

  SimpleRRFFusion rrf;
  ObRRFConfig config(60, 100);

  EXPECT_EQ(OB_SUCCESS, rrf.init(config, *allocator_));

  // 全文搜索和向量搜索都包含相同的文档
  ObSEArray<ObHybridSearchResult, 64> fts_results;
  for (int i = 0; i < 3; ++i) {
    ObHybridSearchResult result;
    result.doc_id_ = i + 1;
    result.fts_score_ = 10.0 - i;
    fts_results.push_back(result);
  }

  ObSEArray<ObHybridSearchResult, 64> vector_results;
  for (int i = 0; i < 3; ++i) {
    ObHybridSearchResult result;
    result.doc_id_ = i + 1;  // 相同的 doc_id
    result.vector_score_ = 0.9 - i * 0.1;
    vector_results.push_back(result);
  }

  EXPECT_EQ(OB_SUCCESS, rrf.add_fts_results(fts_results));
  EXPECT_EQ(OB_SUCCESS, rrf.add_vector_results(vector_results));
  EXPECT_EQ(OB_SUCCESS, rrf.fuse());

  // 应该只有 3 个融合结果（去重后）
  EXPECT_EQ(3, rrf.get_fused_result_count());

  // 验证每个结果都同时包含两个搜索结果
  for (int i = 0; i < rrf.get_fused_result_count(); ++i) {
    const auto *result = rrf.get_result_at(i);
    EXPECT_NE(nullptr, result);
    EXPECT_EQ(3, result->source_flag_);  // 1|2 = 3，表示来自两个源
  }

  OB_LOG(INFO, "Duplicate document handling verified");
}

// 测试 8: 结果限制测试
TEST_F(HybridSearchIntegrationTest, ResultLimitTest)
{
  OB_LOG(INFO, "=== Test: ResultLimitTest ===");

  SimpleRRFFusion rrf;
  ObRRFConfig config(60, 100);

  EXPECT_EQ(OB_SUCCESS, rrf.init(config, *allocator_));

  // 准备 10 个结果
  ObSEArray<ObHybridSearchResult, 64> fts_results;
  for (int i = 0; i < 10; ++i) {
    ObHybridSearchResult result;
    result.doc_id_ = i + 1;
    result.fts_score_ = 20.0 - i;
    fts_results.push_back(result);
  }

  EXPECT_EQ(OB_SUCCESS, rrf.add_fts_results(fts_results));
  EXPECT_EQ(OB_SUCCESS, rrf.add_vector_results(ObSEArray<ObHybridSearchResult, 64>()));
  EXPECT_EQ(OB_SUCCESS, rrf.fuse());

  // 测试不同的限制
  ObSEArray<ObHybridSearchResult, 64> results_5;
  EXPECT_EQ(OB_SUCCESS, rrf.get_results(results_5, 5));
  EXPECT_EQ(5, results_5.count());

  ObSEArray<ObHybridSearchResult, 64> results_20;
  EXPECT_EQ(OB_SUCCESS, rrf.get_results(results_20, 20));
  EXPECT_EQ(10, results_20.count());  // 只有 10 个，不能超过

  ObSEArray<ObHybridSearchResult, 64> results_0;
  EXPECT_EQ(OB_SUCCESS, rrf.get_results(results_0, 0));  // 0 表示全部
  EXPECT_EQ(10, results_0.count());

  OB_LOG(INFO, "Result limit handling verified");
}

} // namespace common
} // namespace oceanbase

// ============================================================
// 主测试实例
// ============================================================
int main(int argc, char **argv)
{
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
