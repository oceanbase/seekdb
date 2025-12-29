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

#define USING_LOG_PREFIX OLOG

#include "ob_hybrid_search_fusion_engine.h"
#include "ob_rrf_fusion.h"
#include "ob_weighted_fusion.h"
#include "lib/oblog/ob_log.h"

namespace oceanbase {
namespace share {
using namespace oceanbase::common;

// ==================== RRF Strategy Adapter ====================
class ObRRFFusionStrategy : public IObHybridSearchFusionStrategy {
public:
  virtual ~ObRRFFusionStrategy() = default;
  
  int init(const void *config, ObIAllocator &allocator) override {
    if (OB_ISNULL(config)) {
      return OB_INVALID_ARGUMENT;
    }
    const ObRRFConfig *rrf_config = static_cast<const ObRRFConfig *>(config);
    return rrf_fusion_.init(*rrf_config, allocator);
  }
  
  int feed_fts_results(const ObIArray<ObHybridSearchResult> &results) override {
    return rrf_fusion_.add_fts_results(results);
  }
  
  int feed_vector_results(const ObIArray<ObHybridSearchResult> &results) override {
    return rrf_fusion_.add_vector_results(results);
  }
  
  int execute_fusion() override {
    return rrf_fusion_.fuse();
  }
  
  int get_fused_results(ObIArray<ObHybridSearchResult> &results, int64_t limit = 0) const override {
    return rrf_fusion_.get_results(results, limit);
  }
  
  void reset() override {
    rrf_fusion_.reset();
  }

private:
  ObRRFFusion rrf_fusion_;
};

// ==================== Weighted Fusion Strategy Adapter ====================
class ObWeightedFusionStrategy : public IObHybridSearchFusionStrategy {
public:
  ObWeightedFusionStrategy(ObNormalizationConfig::NormalizationType norm_type)
    : norm_type_(norm_type) {}
  
  virtual ~ObWeightedFusionStrategy() = default;
  
  int init(const void *config, ObIAllocator &allocator) override {
    if (OB_ISNULL(config)) {
      return OB_INVALID_ARGUMENT;
    }
    const ObWeightedFusionConfig *fusion_config = static_cast<const ObWeightedFusionConfig *>(config);
    ObNormalizationConfig norm_config;
    norm_config.norm_type_ = norm_type_;
    return weighted_fusion_.init(*fusion_config, norm_config, allocator);
  }
  
  int feed_fts_results(const ObIArray<ObHybridSearchResult> &results) override {
    return weighted_fusion_.add_fts_results(results);
  }
  
  int feed_vector_results(const ObIArray<ObHybridSearchResult> &results) override {
    return weighted_fusion_.add_vector_results(results);
  }
  
  int execute_fusion() override {
    return weighted_fusion_.fuse();
  }
  
  int get_fused_results(ObIArray<ObHybridSearchResult> &results, int64_t limit = 0) const override {
    return weighted_fusion_.get_results(results, limit);
  }
  
  void reset() override {
    weighted_fusion_.reset();
  }

private:
  ObWeightedFusion weighted_fusion_;
  ObNormalizationConfig::NormalizationType norm_type_;
};

// ==================== Fusion Engine Main Class ====================
ObHybridSearchFusionEngine::ObHybridSearchFusionEngine()
  : strategy_buffer_(),
    strategy_(nullptr),
    is_initialized_(false),
    allocator_(nullptr)
{
}

ObHybridSearchFusionEngine::~ObHybridSearchFusionEngine()
{
  if (OB_NOT_NULL(strategy_)) {
    strategy_->~IObHybridSearchFusionStrategy();
    strategy_ = nullptr;
  }
}

int ObHybridSearchFusionEngine::create_strategy(FusionStrategy strategy, ObIAllocator &allocator)
{
  int ret = OB_SUCCESS;
  
  switch (strategy) {
    case FusionStrategy::RRF: {
      void *buf = strategy_buffer_.get_data();
      if (OB_ISNULL(buf)) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
      } else {
        strategy_ = new (buf) ObRRFFusionStrategy();
      }
      break;
    }
    
    case FusionStrategy::WEIGHTED_SUM_MIN_MAX: {
      void *buf = strategy_buffer_.get_data();
      if (OB_ISNULL(buf)) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
      } else {
        strategy_ = new (buf) ObWeightedFusionStrategy(
            ObNormalizationConfig::NormalizationType::MIN_MAX);
      }
      break;
    }
    
    case FusionStrategy::WEIGHTED_SUM_Z_SCORE: {
      void *buf = strategy_buffer_.get_data();
      if (OB_ISNULL(buf)) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
      } else {
        strategy_ = new (buf) ObWeightedFusionStrategy(
            ObNormalizationConfig::NormalizationType::Z_SCORE);
      }
      break;
    }
    
    case FusionStrategy::WEIGHTED_SUM_SIGMOID: {
      void *buf = strategy_buffer_.get_data();
      if (OB_ISNULL(buf)) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
      } else {
        strategy_ = new (buf) ObWeightedFusionStrategy(
            ObNormalizationConfig::NormalizationType::SIGMOID);
      }
      break;
    }
    
    case FusionStrategy::WEIGHTED_SUM:
    case FusionStrategy::UNKNOWN:
    default:
      ret = OB_NOT_SUPPORTED;
      OB_LOG(WARN, "unsupported fusion strategy", K(ret), K(strategy));
  }
  
  return ret;
}

int ObHybridSearchFusionEngine::init(FusionStrategy strategy, const void *config, 
                                      ObIAllocator &allocator)
{
  int ret = OB_SUCCESS;
  
  if (is_initialized_) {
    ret = OB_INIT_TWICE;
    OB_LOG(WARN, "fusion engine already initialized", K(ret));
    return ret;
  }
  
  if (OB_FAIL(create_strategy(strategy, allocator))) {
    OB_LOG(WARN, "failed to create fusion strategy", K(ret));
    return ret;
  }
  
  if (OB_ISNULL(strategy_)) {
    ret = OB_ERR_UNEXPECTED;
    OB_LOG(WARN, "strategy is null after creation", K(ret));
    return ret;
  }
  
  if (OB_FAIL(strategy_->init(config, allocator))) {
    OB_LOG(WARN, "failed to initialize fusion strategy", K(ret));
    return ret;
  }
  
  allocator_ = &allocator;
  is_initialized_ = true;
  
  return ret;
}

int ObHybridSearchFusionEngine::feed_fts_results(const ObIArray<ObHybridSearchResult> &results)
{
  int ret = OB_SUCCESS;
  
  if (!is_initialized_) {
    ret = OB_NOT_INIT;
    OB_LOG(WARN, "fusion engine not initialized", K(ret));
    return ret;
  }
  
  if (OB_ISNULL(strategy_)) {
    ret = OB_ERR_UNEXPECTED;
    OB_LOG(WARN, "strategy is null", K(ret));
    return ret;
  }
  
  return strategy_->feed_fts_results(results);
}

int ObHybridSearchFusionEngine::feed_vector_results(const ObIArray<ObHybridSearchResult> &results)
{
  int ret = OB_SUCCESS;
  
  if (!is_initialized_) {
    ret = OB_NOT_INIT;
    OB_LOG(WARN, "fusion engine not initialized", K(ret));
    return ret;
  }
  
  if (OB_ISNULL(strategy_)) {
    ret = OB_ERR_UNEXPECTED;
    OB_LOG(WARN, "strategy is null", K(ret));
    return ret;
  }
  
  return strategy_->feed_vector_results(results);
}

int ObHybridSearchFusionEngine::execute_fusion()
{
  int ret = OB_SUCCESS;
  
  if (!is_initialized_) {
    ret = OB_NOT_INIT;
    OB_LOG(WARN, "fusion engine not initialized", K(ret));
    return ret;
  }
  
  if (OB_ISNULL(strategy_)) {
    ret = OB_ERR_UNEXPECTED;
    OB_LOG(WARN, "strategy is null", K(ret));
    return ret;
  }
  
  return strategy_->execute_fusion();
}

int ObHybridSearchFusionEngine::get_fused_results(ObIArray<ObHybridSearchResult> &results, 
                                                   int64_t limit) const
{
  int ret = OB_SUCCESS;
  
  if (!is_initialized_) {
    ret = OB_NOT_INIT;
    OB_LOG(WARN, "fusion engine not initialized", K(ret));
    return ret;
  }
  
  if (OB_ISNULL(strategy_)) {
    ret = OB_ERR_UNEXPECTED;
    OB_LOG(WARN, "strategy is null", K(ret));
    return ret;
  }
  
  return strategy_->get_fused_results(results, limit);
}

int64_t ObHybridSearchFusionEngine::get_fused_result_count() const
{
  int ret = OB_SUCCESS;
  if (!is_initialized_ || OB_ISNULL(strategy_)) {
    return 0;
  }
  
  common::ObSEArray<ObHybridSearchResult, 64> temp_results;
  if (OB_SUCC(strategy_->get_fused_results(temp_results))) {
    return temp_results.count();
  }
  
  return 0;
}

void ObHybridSearchFusionEngine::reset()
{
  if (OB_NOT_NULL(strategy_)) {
    strategy_->reset();
  }
  
  is_initialized_ = false;
  allocator_ = nullptr;
}

} // namespace share
} // namespace oceanbase
