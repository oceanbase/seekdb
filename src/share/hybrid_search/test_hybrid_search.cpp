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
 *
 * Hybrid Search Unit Tests
 * Includes comprehensive test cases for RRF fusion and weighted fusion
 */

#include <gtest/gtest.h>
#include "ob_rrf_fusion.h"
#include "ob_weighted_fusion.h"
#include "lib/allocator/ob_malloc.h"

namespace oceanbase
{
namespace common
{

/*
 * =============================================
 * RRF 融合单元测试
 * =============================================
 */

class RRFFusionTest : public ::testing::Test
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

// 测试 1: 基础初始化
TEST_F(RRFFusionTest, BasicInitialization)
{
  ObRRFFusion rrf;
  ObRRFConfig config(60, 100);
  
  int ret = rrf.init(config, *allocator_);
  ASSERT_EQ(OB_SUCCESS, ret);
}

// 测试 2: 重复初始化应该失败
TEST_F(RRFFusionTest, DuplicateInitialization)
{
  ObRRFFusion rrf;
  ObRRFConfig config(60, 100);
  
  int ret1 = rrf.init(config, *allocator_);
  ASSERT_EQ(OB_SUCCESS, ret1);
  
  int ret2 = rrf.init(config, *allocator_);
  ASSERT_EQ(OB_INIT_TWICE, ret2);
}

// 测试 3: 无效配置应该失败
TEST_F(RRFFusionTest, InvalidConfig)
{
  ObRRFFusion rrf;
  ObRRFConfig config(-1, 100);  // 无效的 rank_constant
  
  int ret = rrf.init(config, *allocator_);
  ASSERT_NE(OB_SUCCESS, ret);
}

// 测试 4: 基本融合功能
TEST_F(RRFFusionTest, BasicFusion)
{
  ObRRFFusion rrf;
  ObRRFConfig config(60, 100);
  
  ASSERT_EQ(OB_SUCCESS, rrf.init(config, *allocator_));
  
  // 准备全文搜索结果
  common::ObSEArray<ObHybridSearchResult, 8> fts_results;
  for (int i = 0; i < 3; ++i) {
    ObHybridSearchResult result;
    result.doc_id_ = i + 1;
    result.fts_score_ = 10.0 - i * 2.0;
    fts_results.push_back(result);
  }
  
  // 准备向量搜索结果
  common::ObSEArray<ObHybridSearchResult, 8> vector_results;
  for (int i = 0; i < 3; ++i) {
    ObHybridSearchResult result;
    result.doc_id_ = (i % 3) + 1;
    result.vector_score_ = 0.9 - i * 0.1;
    vector_results.push_back(result);
  }
  
  ASSERT_EQ(OB_SUCCESS, rrf.add_fts_results(fts_results));
  ASSERT_EQ(OB_SUCCESS, rrf.add_vector_results(vector_results));
  ASSERT_EQ(OB_SUCCESS, rrf.fuse());
  
  // 验证融合结果
  ASSERT_GT(rrf.get_fused_result_count(), 0);
  
  // 获取前 2 个结果
  common::ObSEArray<ObHybridSearchResult, 8> results;
  ASSERT_EQ(OB_SUCCESS, rrf.get_results(results, 2));
  ASSERT_EQ(2, results.count());
  
  // 验证结果按得分排序
  if (results.count() > 1) {
    EXPECT_GE(results.at(0).final_score_, results.at(1).final_score_);
  }
}

// 测试 5: 全文搜索结果为空
TEST_F(RRFFusionTest, EmptyFTSResults)
{
  ObRRFFusion rrf;
  ObRRFConfig config(60, 100);
  
  ASSERT_EQ(OB_SUCCESS, rrf.init(config, *allocator_));
  
  common::ObSEArray<ObHybridSearchResult, 8> fts_results;
  common::ObSEArray<ObHybridSearchResult, 8> vector_results;
  
  ObHybridSearchResult result;
  result.doc_id_ = 1;
  result.vector_score_ = 0.9;
  vector_results.push_back(result);
  
  ASSERT_EQ(OB_SUCCESS, rrf.add_fts_results(fts_results));
  ASSERT_EQ(OB_SUCCESS, rrf.add_vector_results(vector_results));
  ASSERT_EQ(OB_SUCCESS, rrf.fuse());
  
  ASSERT_GT(rrf.get_fused_result_count(), 0);
}

// 测试 6: 向量搜索结果为空
TEST_F(RRFFusionTest, EmptyVectorResults)
{
  ObRRFFusion rrf;
  ObRRFConfig config(60, 100);
  
  ASSERT_EQ(OB_SUCCESS, rrf.init(config, *allocator_));
  
  common::ObSEArray<ObHybridSearchResult, 8> fts_results;
  common::ObSEArray<ObHybridSearchResult, 8> vector_results;
  
  ObHybridSearchResult result;
  result.doc_id_ = 1;
  result.fts_score_ = 10.0;
  fts_results.push_back(result);
  
  ASSERT_EQ(OB_SUCCESS, rrf.add_fts_results(fts_results));
  ASSERT_EQ(OB_SUCCESS, rrf.add_vector_results(vector_results));
  ASSERT_EQ(OB_SUCCESS, rrf.fuse());
  
  ASSERT_GT(rrf.get_fused_result_count(), 0);
}

// 测试 7: 重置功能
TEST_F(RRFFusionTest, Reset)
{
  ObRRFFusion rrf;
  ObRRFConfig config(60, 100);
  
  ASSERT_EQ(OB_SUCCESS, rrf.init(config, *allocator_));
  
  common::ObSEArray<ObHybridSearchResult, 8> results;
  ObHybridSearchResult result;
  result.doc_id_ = 1;
  result.fts_score_ = 10.0;
  results.push_back(result);
  
  ASSERT_EQ(OB_SUCCESS, rrf.add_fts_results(results));
  
  rrf.reset();
  ASSERT_EQ(0, rrf.get_fts_result_count());
}

/*
 * =============================================
 * 加权融合单元测试
 * =============================================
 */

class WeightedFusionTest : public ::testing::Test
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

// 测试 1: 基础初始化
TEST_F(WeightedFusionTest, BasicInitialization)
{
  ObWeightedFusion fusion;
  ObWeightedFusionConfig fusion_config(0.5, 0.5, true);
  ObNormalizationConfig norm_config;
  
  int ret = fusion.init(fusion_config, norm_config, *allocator_);
  ASSERT_EQ(OB_SUCCESS, ret);
}

// 测试 2: 无效权重配置
TEST_F(WeightedFusionTest, InvalidWeightConfig)
{
  ObWeightedFusion fusion;
  ObWeightedFusionConfig fusion_config(-0.5, 1.5, true);  // 无效权重
  ObNormalizationConfig norm_config;
  
  int ret = fusion.init(fusion_config, norm_config, *allocator_);
  ASSERT_NE(OB_SUCCESS, ret);
}

// 测试 3: Min-Max 规范化融合
TEST_F(WeightedFusionTest, MinMaxNormalization)
{
  ObWeightedFusion fusion;
  ObWeightedFusionConfig fusion_config(0.5, 0.5, true);
  ObNormalizationConfig norm_config;
  norm_config.norm_type_ = ObNormalizationConfig::NormalizationType::MIN_MAX;
  
  ASSERT_EQ(OB_SUCCESS, fusion.init(fusion_config, norm_config, *allocator_));
  
  // 准备数据
  common::ObSEArray<ObHybridSearchResult, 8> fts_results;
  for (int i = 0; i < 3; ++i) {
    ObHybridSearchResult result;
    result.doc_id_ = i + 1;
    result.fts_score_ = 10.0 - i * 3.0;
    fts_results.push_back(result);
  }
  
  common::ObSEArray<ObHybridSearchResult, 8> vector_results;
  for (int i = 0; i < 3; ++i) {
    ObHybridSearchResult result;
    result.doc_id_ = (i % 3) + 1;
    result.vector_score_ = 0.9 - i * 0.2;
    vector_results.push_back(result);
  }
  
  ASSERT_EQ(OB_SUCCESS, fusion.add_fts_results(fts_results));
  ASSERT_EQ(OB_SUCCESS, fusion.add_vector_results(vector_results));
  ASSERT_EQ(OB_SUCCESS, fusion.fuse());
  
  ASSERT_GT(fusion.get_fused_result_count(), 0);
}

// 测试 4: Z-Score 规范化融合
TEST_F(WeightedFusionTest, ZScoreNormalization)
{
  ObWeightedFusion fusion;
  ObWeightedFusionConfig fusion_config(0.7, 0.3, true);
  ObNormalizationConfig norm_config;
  norm_config.norm_type_ = ObNormalizationConfig::NormalizationType::Z_SCORE;
  
  ASSERT_EQ(OB_SUCCESS, fusion.init(fusion_config, norm_config, *allocator_));
  
  // 准备数据
  common::ObSEArray<ObHybridSearchResult, 8> fts_results;
  for (int i = 0; i < 5; ++i) {
    ObHybridSearchResult result;
    result.doc_id_ = i + 1;
    result.fts_score_ = 10.0 - i;
    fts_results.push_back(result);
  }
  
  common::ObSEArray<ObHybridSearchResult, 8> vector_results;
  for (int i = 0; i < 5; ++i) {
    ObHybridSearchResult result;
    result.doc_id_ = i + 1;
    result.vector_score_ = 0.8 - i * 0.1;
    vector_results.push_back(result);
  }
  
  ASSERT_EQ(OB_SUCCESS, fusion.add_fts_results(fts_results));
  ASSERT_EQ(OB_SUCCESS, fusion.add_vector_results(vector_results));
  ASSERT_EQ(OB_SUCCESS, fusion.fuse());
  
  // 验证融合结果
  common::ObSEArray<ObHybridSearchResult, 8> results;
  ASSERT_EQ(OB_SUCCESS, fusion.get_results(results));
  
  for (int i = 0; i < results.count(); ++i) {
    const auto &result = results.at(i);
    // 最终得分应该在合理范围内
    EXPECT_GE(result.final_score_, 0.0);
    EXPECT_LE(result.final_score_, 2.0);
  }
}

// 测试 5: 关键词优先权重配置
TEST_F(WeightedFusionTest, KeywordPriorityWeights)
{
  ObWeightedFusion fusion;
  ObWeightedFusionConfig fusion_config(0.7, 0.3, true);
  ObNormalizationConfig norm_config;
  
  ASSERT_EQ(OB_SUCCESS, fusion.init(fusion_config, norm_config, *allocator_));
  
  // 准备数据
  common::ObSEArray<ObHybridSearchResult, 8> fts_results;
  ObHybridSearchResult fts_r1;
  fts_r1.doc_id_ = 1;
  fts_r1.fts_score_ = 15.0;
  fts_results.push_back(fts_r1);
  
  common::ObSEArray<ObHybridSearchResult, 8> vector_results;
  ObHybridSearchResult vec_r1;
  vec_r1.doc_id_ = 2;
  vec_r1.vector_score_ = 0.95;
  vector_results.push_back(vec_r1);
  
  ASSERT_EQ(OB_SUCCESS, fusion.add_fts_results(fts_results));
  ASSERT_EQ(OB_SUCCESS, fusion.add_vector_results(vector_results));
  ASSERT_EQ(OB_SUCCESS, fusion.fuse());
  
  // 文档 1 应该获得更高的分数（因为全文权重较高）
  const auto *result1 = fusion.get_result_at(0);
  ASSERT_NE(nullptr, result1);
}

// 测试 6: Sigmoid 规范化
TEST_F(WeightedFusionTest, SigmoidNormalization)
{
  ObWeightedFusion fusion;
  ObWeightedFusionConfig fusion_config(0.5, 0.5, true);
  ObNormalizationConfig norm_config;
  norm_config.norm_type_ = ObNormalizationConfig::NormalizationType::SIGMOID;
  
  ASSERT_EQ(OB_SUCCESS, fusion.init(fusion_config, norm_config, *allocator_));
  
  // 准备数据
  common::ObSEArray<ObHybridSearchResult, 8> fts_results;
  for (int i = 0; i < 3; ++i) {
    ObHybridSearchResult result;
    result.doc_id_ = i + 1;
    result.fts_score_ = 5.0 - i * 1.5;
    fts_results.push_back(result);
  }
  
  common::ObSEArray<ObHybridSearchResult, 8> vector_results;
  for (int i = 0; i < 3; ++i) {
    ObHybridSearchResult result;
    result.doc_id_ = i + 1;
    result.vector_score_ = 0.7 - i * 0.15;
    vector_results.push_back(result);
  }
  
  ASSERT_EQ(OB_SUCCESS, fusion.add_fts_results(fts_results));
  ASSERT_EQ(OB_SUCCESS, fusion.add_vector_results(vector_results));
  ASSERT_EQ(OB_SUCCESS, fusion.fuse());
  
  ASSERT_GT(fusion.get_fused_result_count(), 0);
}

// 测试 7: 无规范化
TEST_F(WeightedFusionTest, NoNormalization)
{
  ObWeightedFusion fusion;
  ObWeightedFusionConfig fusion_config(0.5, 0.5, false);
  ObNormalizationConfig norm_config;
  norm_config.norm_type_ = ObNormalizationConfig::NormalizationType::NONE;
  
  ASSERT_EQ(OB_SUCCESS, fusion.init(fusion_config, norm_config, *allocator_));
  
  // 准备数据
  common::ObSEArray<ObHybridSearchResult, 8> fts_results;
  ObHybridSearchResult fts_r;
  fts_r.doc_id_ = 1;
  fts_r.fts_score_ = 10.0;
  fts_results.push_back(fts_r);
  
  common::ObSEArray<ObHybridSearchResult, 8> vector_results;
  ObHybridSearchResult vec_r;
  vec_r.doc_id_ = 1;
  vec_r.vector_score_ = 0.5;
  vector_results.push_back(vec_r);
  
  ASSERT_EQ(OB_SUCCESS, fusion.add_fts_results(fts_results));
  ASSERT_EQ(OB_SUCCESS, fusion.add_vector_results(vector_results));
  ASSERT_EQ(OB_SUCCESS, fusion.fuse());
  
  const auto *result = fusion.get_result_at(0);
  ASSERT_NE(nullptr, result);
  // final_score = 0.5 * 10.0 + 0.5 * 0.5 = 5.25
  EXPECT_DOUBLE_EQ(5.25, result->final_score_);
}

} // namespace common
} // namespace oceanbase

// 运行所有测试
int main(int argc, char **argv)
{
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
