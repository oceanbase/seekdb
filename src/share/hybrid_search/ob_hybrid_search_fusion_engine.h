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

#ifndef OB_HYBRID_SEARCH_FUSION_ENGINE_H
#define OB_HYBRID_SEARCH_FUSION_ENGINE_H

#include "ob_hybrid_search_common.h"
#include "lib/container/ob_se_array.h"
#include "lib/allocator/ob_allocator.h"

namespace oceanbase {
namespace share {

/*
 * Hybrid Search Fusion Engine
 * 
 * Responsibilities:
 * 1. Receive raw search results from FTS and KNN
 * 2. Select appropriate fusion strategy based on configuration
 * 3. Execute fusion computation and return sorted final results
 * 
 * Usage flow:
 * 1. ObHybridSearchFusionEngine engine;
 * 2. engine.init(strategy, config, allocator);
 * 3. engine.feed_fts_results(fts_results);
 * 4. engine.feed_vector_results(vector_results);
 * 5. engine.execute_fusion();
 * 6. engine.get_fused_results(output_results);
 */

class IObHybridSearchFusionStrategy {
public:
  virtual ~IObHybridSearchFusionStrategy() = default;
  
  /*
   * Initialize fusion strategy
   * 
   * @param config Configuration object, specific type is determined by strategy
   * @param allocator Memory allocator
   * @return OB_SUCCESS or error code
   */
  virtual int init(const void *config, common::ObIAllocator &allocator) = 0;
  
  /*
   * Feed full-text search results
   */
  virtual int feed_fts_results(const common::ObIArray<common::ObHybridSearchResult> &results) = 0;
  
  /*
   * Feed vector search results
   */
  virtual int feed_vector_results(const common::ObIArray<common::ObHybridSearchResult> &results) = 0;
  
  /*
   * Execute fusion computation
   */
  virtual int execute_fusion() = 0;
  
  /*
   * Get fused results
   * 
   * @param results Output parameter containing sorted fused results
   * @param limit Limit the number of returned results, 0 means return all results
   * @return OB_SUCCESS or error code
   */
  virtual int get_fused_results(common::ObIArray<common::ObHybridSearchResult> &results, 
                                 int64_t limit = 0) const = 0;
  
  /*
   * Reset state
   */
  virtual void reset() = 0;
};

class ObHybridSearchFusionEngine {
public:
  /*
   * Fusion strategy type
   */
  enum class FusionStrategy {
    UNKNOWN = 0,
    RRF = 1,                    // Reciprocal Rank Fusion
    WEIGHTED_SUM = 2,           // Weighted sum (dynamically select normalization strategy)
    WEIGHTED_SUM_MIN_MAX = 3,   // Min-Max normalization + weighted sum
    WEIGHTED_SUM_Z_SCORE = 4,   // Z-Score normalization + weighted sum
    WEIGHTED_SUM_SIGMOID = 5    // Sigmoid normalization + weighted sum
  };

  ObHybridSearchFusionEngine();
  ~ObHybridSearchFusionEngine();
  
  /*
   * Initialize fusion engine
   * 
   * @param strategy Fusion strategy
   * @param config Configuration object pointer, type depends on strategy
   * @param allocator Memory allocator
   * @return OB_SUCCESS or error code
   * 
   * Configuration object types:
   * - RRF: const ObRRFConfig*
   * - WEIGHTED_SUM*: const ObWeightedFusionConfig*
   */
  int init(FusionStrategy strategy, const void *config, common::ObIAllocator &allocator);
  
  /*
   * Feed full-text search result list
   */
  int feed_fts_results(const common::ObIArray<common::ObHybridSearchResult> &results);
  
  /*
   * Feed vector search result list
   */
  int feed_vector_results(const common::ObIArray<common::ObHybridSearchResult> &results);
  
  /*
   * Execute fusion computation
   */
  int execute_fusion();
  
  /*
   * Get fused results
   * 
   * @param results Output array to receive fused results
   * @param limit Optional, limit the number of returned results, 0 means return all
   * @return OB_SUCCESS or error code
   */
  int get_fused_results(common::ObIArray<common::ObHybridSearchResult> &results, 
                        int64_t limit = 0) const;
  
  /*
   * Get count of fused results
   */
  int64_t get_fused_result_count() const;
  
  /*
   * Reset engine state, prepare for next fusion
   */
  void reset();

private:
  /*
   * Create corresponding strategy object based on strategy type
   */
  int create_strategy(FusionStrategy strategy, common::ObIAllocator &allocator);

private:
  // Fusion strategy implementation
  common::ObSEArray<uint8_t, 256> strategy_buffer_;  // Buffer to store strategy object
  IObHybridSearchFusionStrategy *strategy_;          // Strategy interface pointer
  
  // Initialization flag
  bool is_initialized_;
  
  // Memory allocator (non-owner)
  common::ObIAllocator *allocator_;
};

} // namespace share
} // namespace oceanbase

#endif // OB_HYBRID_SEARCH_FUSION_ENGINE_H
