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

#include "ob_weighted_fusion.h"
#include "lib/ob_errno.h"

namespace oceanbase
{
namespace common
{

USING_LOG_PREFIX(OLOG);

ObWeightedFusion::ObWeightedFusion()
  : is_initialized_(false), allocator_(nullptr), stats_calculated_(false)
{
}

ObWeightedFusion::~ObWeightedFusion()
{
  reset();
}

int ObWeightedFusion::init(const ObWeightedFusionConfig &config,
                           const ObNormalizationConfig &norm_config,
                           ObIAllocator &allocator)
{
  int ret = OB_SUCCESS;
  
  if (is_initialized_) {
    ret = OB_INIT_TWICE;
    OB_LOG(WARN, "weighted fusion is already initialized", K(ret));
  } else if (OB_FAIL(validate_config(config))) {
    OB_LOG(WARN, "failed to validate config", K(ret));
  } else {
    fusion_config_ = config;
    norm_config_ = norm_config;
    allocator_ = &allocator;
    
    // Initialize result mapping table
    if (OB_FAIL(result_map_.create(10240, common::ObMemAttr(common::OB_SERVER_TENANT_ID)))) {
      OB_LOG(WARN, "failed to create result map", K(ret));
    } else {
      is_initialized_ = true;
      OB_LOG(DEBUG, "weighted fusion initialized successfully",
             K(config.fts_weight_), K(config.vector_weight_));
    }
  }
  
  return ret;
}

int ObWeightedFusion::add_fts_results(const common::ObIArray<ObHybridSearchResult> &fts_results)
{
  int ret = OB_SUCCESS;
  
  if (!is_initialized_) {
    ret = OB_NOT_INIT;
    OB_LOG(WARN, "weighted fusion is not initialized", K(ret));
  } else if (OB_FAIL(fts_results_.assign(fts_results))) {
    OB_LOG(WARN, "failed to assign fts results", K(ret));
  } else {
    OB_LOG(DEBUG, "add fts results successfully", "count", fts_results_.count());
  }
  
  return ret;
}

int ObWeightedFusion::add_vector_results(const common::ObIArray<ObHybridSearchResult> &vector_results)
{
  int ret = OB_SUCCESS;
  
  if (!is_initialized_) {
    ret = OB_NOT_INIT;
    OB_LOG(WARN, "weighted fusion is not initialized", K(ret));
  } else if (OB_FAIL(vector_results_.assign(vector_results))) {
    OB_LOG(WARN, "failed to assign vector results", K(ret));
  } else {
    OB_LOG(DEBUG, "add vector results successfully", "count", vector_results_.count());
  }
  
  return ret;
}

int ObWeightedFusion::calculate_statistics()
{
  int ret = OB_SUCCESS;
  
  // Calculate statistics for full-text search scores
  if (fts_results_.count() > 0) {
    double sum = 0.0;
    fts_stats_.min_score_ = fts_results_.at(0).fts_score_;
    fts_stats_.max_score_ = fts_results_.at(0).fts_score_;
    fts_stats_.count_ = fts_results_.count();
    
    for (int64_t i = 0; i < fts_results_.count(); ++i) {
      double score = fts_results_.at(i).fts_score_;
      sum += score;
      if (score < fts_stats_.min_score_) {
        fts_stats_.min_score_ = score;
      }
      if (score > fts_stats_.max_score_) {
        fts_stats_.max_score_ = score;
      }
    }
    
    fts_stats_.mean_score_ = sum / fts_stats_.count_;
    
    // Calculate standard deviation
    double variance = 0.0;
    for (int64_t i = 0; i < fts_results_.count(); ++i) {
      double diff = fts_results_.at(i).fts_score_ - fts_stats_.mean_score_;
      variance += diff * diff;
    }
    fts_stats_.stddev_ = std::sqrt(variance / fts_stats_.count_);
  }
  
  // Calculate statistics for vector search scores
  if (vector_results_.count() > 0) {
    double sum = 0.0;
    vector_stats_.min_score_ = vector_results_.at(0).vector_score_;
    vector_stats_.max_score_ = vector_results_.at(0).vector_score_;
    vector_stats_.count_ = vector_results_.count();
    
    for (int64_t i = 0; i < vector_results_.count(); ++i) {
      double score = vector_results_.at(i).vector_score_;
      sum += score;
      if (score < vector_stats_.min_score_) {
        vector_stats_.min_score_ = score;
      }
      if (score > vector_stats_.max_score_) {
        vector_stats_.max_score_ = score;
      }
    }
    
    vector_stats_.mean_score_ = sum / vector_stats_.count_;
    
    // Calculate standard deviation
    double variance = 0.0;
    for (int64_t i = 0; i < vector_results_.count(); ++i) {
      double diff = vector_results_.at(i).vector_score_ - vector_stats_.mean_score_;
      variance += diff * diff;
    }
    vector_stats_.stddev_ = std::sqrt(variance / vector_stats_.count_);
  }
  
  stats_calculated_ = true;
  return ret;
}

double ObWeightedFusion::min_max_normalize(double score, double min_val, double max_val)
{
  if (max_val - min_val < 1e-10) {
    return 0.5;  // Avoid division by zero
  }
  return (score - min_val) / (max_val - min_val);
}

double ObWeightedFusion::z_score_normalize(double score, double mean, double stddev)
{
  if (stddev < 1e-10) {
    return 0.0;  // Avoid division by zero
  }
  // Use Sigmoid function to map Z-Score to [0, 1]
  double z = (score - mean) / stddev;
  return 1.0 / (1.0 + std::exp(-z));
}

double ObWeightedFusion::sigmoid_normalize(double score)
{
  return 1.0 / (1.0 + std::exp(-score));
}

double ObWeightedFusion::apply_normalization(double score, bool is_fts)
{
  if (!fusion_config_.enable_normalization_) {
    return score;
  }
  
  const ScoreStats &stats = is_fts ? fts_stats_ : vector_stats_;
  
  switch (norm_config_.norm_type_) {
    case ObNormalizationConfig::NormalizationType::NONE:
      return score;
    
    case ObNormalizationConfig::NormalizationType::MIN_MAX:
      return min_max_normalize(score, stats.min_score_, stats.max_score_);
    
    case ObNormalizationConfig::NormalizationType::Z_SCORE:
      return z_score_normalize(score, stats.mean_score_, stats.stddev_);
    
    case ObNormalizationConfig::NormalizationType::SIGMOID:
      return sigmoid_normalize(score);
    
    default:
      return score;
  }
}

int ObWeightedFusion::fuse()
{
  int ret = OB_SUCCESS;
  
  if (!is_initialized_) {
    ret = OB_NOT_INIT;
    OB_LOG(WARN, "weighted fusion is not initialized", K(ret));
    return ret;
  }
  
  // 清空融合结果和映射表
  fused_results_.reuse();
  result_map_.clear();
  
  // 计算统计信息
  if (OB_FAIL(calculate_statistics())) {
    OB_LOG(WARN, "failed to calculate statistics", K(ret));
    return ret;
  }
  
  // 处理全文搜索结果
  for (int64_t i = 0; OB_SUCC(ret) && i < fts_results_.count(); ++i) {
    const ObHybridSearchResult &result = fts_results_.at(i);
    
    ObHybridSearchResult merged_result = result;
    merged_result.source_flag_ |= 1;  // 标记来自全文搜索
    
    if (OB_FAIL(result_map_.set_refactored(result.doc_id_, merged_result))) {
      OB_LOG(WARN, "failed to insert fts result into map", K(ret), K(result));
    }
  }
  
  // 处理向量搜索结果
  for (int64_t i = 0; OB_SUCC(ret) && i < vector_results_.count(); ++i) {
    const ObHybridSearchResult &result = vector_results_.at(i);
    
    ObHybridSearchResult merged_result = result;
    merged_result.source_flag_ |= 2;  // 标记来自向量搜索
    
    ObHybridSearchResult existing;
    if (OB_FAIL(result_map_.get_refactored(result.doc_id_, existing))) {
      if (OB_HASH_NOT_EXIST == ret) {
        // 新文档，直接插入
        ret = result_map_.set_refactored(result.doc_id_, merged_result);
        if (OB_FAIL(ret)) {
          OB_LOG(WARN, "failed to insert vector result into map", K(ret), K(result));
        }
      } else {
        OB_LOG(WARN, "failed to get result from map", K(ret));
      }
    } else {
      // 文档已存在，更新向量分数
      existing.vector_score_ = result.vector_score_;
      existing.source_flag_ |= 2;  // 添加向量搜索标记
      
      if (OB_FAIL(result_map_.set_refactored(result.doc_id_, existing))) {
        OB_LOG(WARN, "failed to update result in map", K(ret));
      }
    }
  }
  
  // 从映射表中提取结果并计算最终得分
  for (ResultMap::iterator iter = result_map_.begin(); OB_SUCC(ret) && iter != result_map_.end(); ++iter) {
    ObHybridSearchResult result = iter->second;
    
    // 规范化分数
    double normalized_fts = apply_normalization(result.fts_score_, true);
    double normalized_vector = apply_normalization(result.vector_score_, false);
    
    // 计算加权和
    result.final_score_ = fusion_config_.fts_weight_ * normalized_fts +
                          fusion_config_.vector_weight_ * normalized_vector;
    
    if (OB_FAIL(fused_results_.push_back(result))) {
      OB_LOG(WARN, "failed to push back fused result", K(ret));
    }
  }
  
  // 按最终得分降序排序
  if (OB_SUCC(ret)) {
    std::sort(fused_results_.begin(), fused_results_.end(),
              [](const ObHybridSearchResult &a, const ObHybridSearchResult &b) {
                if (a.final_score_ != b.final_score_) {
                  return a.final_score_ > b.final_score_;
                }
                return a.doc_id_ < b.doc_id_;
              });
    
    OB_LOG(DEBUG, "weighted fusion completed successfully",
           "fused_count", fused_results_.count(),
           "fts_count", fts_results_.count(),
           "vector_count", vector_results_.count());
  }
  
  return ret;
}

int ObWeightedFusion::get_results(common::ObIArray<ObHybridSearchResult> &results, int64_t limit) const
{
  int ret = OB_SUCCESS;
  
  if (!is_initialized_) {
    ret = OB_NOT_INIT;
    OB_LOG(WARN, "weighted fusion is not initialized", K(ret));
    return ret;
  }
  
  int64_t count = fused_results_.count();
  if (limit > 0 && limit < count) {
    count = limit;
  }
  
  for (int64_t i = 0; OB_SUCC(ret) && i < count; ++i) {
    if (OB_FAIL(results.push_back(fused_results_.at(i)))) {
      OB_LOG(WARN, "failed to push back result", K(ret));
    }
  }
  
  return ret;
}

const ObHybridSearchResult *ObWeightedFusion::get_result_at(int64_t index) const
{
  if (index < 0 || index >= fused_results_.count()) {
    return nullptr;
  }
  return &fused_results_.at(index);
}

void ObWeightedFusion::reset()
{
  fts_results_.reuse();
  vector_results_.reuse();
  fused_results_.reuse();
  if (nullptr != allocator_) {
    result_map_.clear();
    result_map_.destroy();
  }
  is_initialized_ = false;
  allocator_ = nullptr;
  stats_calculated_ = false;
}

int ObWeightedFusion::validate_config(const ObWeightedFusionConfig &config) const
{
  int ret = OB_SUCCESS;
  
  // Check weights
  if (config.fts_weight_ < 0.0 || config.fts_weight_ > 1.0) {
    ret = OB_INVALID_ARGUMENT;
    OB_LOG(WARN, "fts weight must be in [0, 1]", K(ret), K(config.fts_weight_));
  } else if (config.vector_weight_ < 0.0 || config.vector_weight_ > 1.0) {
    ret = OB_INVALID_ARGUMENT;
    OB_LOG(WARN, "vector weight must be in [0, 1]", K(ret), K(config.vector_weight_));
  } else if (config.fts_weight_ + config.vector_weight_ < 1e-10) {
    ret = OB_INVALID_ARGUMENT;
    OB_LOG(WARN, "sum of weights should be positive", K(ret));
  }
  
  return ret;
}

} // namespace common
} // namespace oceanbase
