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

#ifndef OB_WEIGHTED_FUSION_H
#define OB_WEIGHTED_FUSION_H

#include "ob_hybrid_search_common.h"
#include "lib/container/ob_se_array.h"
#include "lib/hash/ob_hashmap.h"
#include <cmath>

namespace oceanbase
{
namespace common
{

/*
 * Weighted Fusion Method Implementation
 * 
 * Basic Principle:
 * By assigning weights to full-text and vector search,
 * then normalizing and computing weighted sum of scores for each document.
 * 
 * Formula:
 * final_score = fts_weight * normalized_fts_score + vector_weight * normalized_vector_score
 * 
 * Advantages:
 * 1. Fine-grained control: can precisely control the impact ratio of FTS and vector search
 * 2. Flexible adaptation: supports multiple normalization strategies
 * 3. Business-oriented: weights can be adjusted dynamically based on business scenarios
 * 
 * Application Scenarios:
 * - Applications requiring fine-grained control over FTS and vector search ratio
 * - Scenarios with clear business preferences (e.g., prioritizing keyword matching or semantic similarity)
 * - Applications that can dynamically adjust weights based on query types
 */
class ObWeightedFusion
{
public:
  typedef common::hash::ObHashMap<uint64_t, ObHybridSearchResult> ResultMap;
  
  ObWeightedFusion();
  virtual ~ObWeightedFusion();
  
  /*
   * Initialize weighted fusion engine
   * 
   * @param config Weighted fusion configuration parameters
   * @param norm_config Normalization configuration parameters
   * @param allocator Memory allocator
   * @return Returns OB_SUCCESS on success, corresponding error code on failure
   */
  int init(const ObWeightedFusionConfig &config,
           const ObNormalizationConfig &norm_config,
           ObIAllocator &allocator);
  
  /*
   * Add full-text search results
   * 
   * @param fts_results Full-text search result list
   * @return Returns OB_SUCCESS on success, corresponding error code on failure
   */
  int add_fts_results(const common::ObIArray<ObHybridSearchResult> &fts_results);
  
  /*
   * Add vector search results
   * 
   * @param vector_results Vector search result list
   * @return Returns OB_SUCCESS on success, corresponding error code on failure
   */
  int add_vector_results(const common::ObIArray<ObHybridSearchResult> &vector_results);
  
  /*
   * Execute weighted fusion calculation
   * 
   * This method will:
   * 1. Collect score statistics from both result lists
   * 2. Normalize scores according to normalization strategy
   * 3. Apply weights to compute weighted sum
   * 4. Sort by final score
   * 
   * @return Returns OB_SUCCESS on success, corresponding error code on failure
   */
  int fuse();
  
  /*
   * Get fusion results
   * 
   * @param results Output parameter containing the fused result list
   * @param limit Maximum number of results to return, 0 means return all results
   * @return Returns OB_SUCCESS on success, corresponding error code on failure
   */
  int get_results(common::ObIArray<ObHybridSearchResult> &results, int64_t limit = 0) const;
  
  /*
   * Reset fusion engine state, prepare for next fusion
   */
  void reset();
  
  /*
   * Get count of fused results
   */
  int64_t get_fused_result_count() const { return fused_results_.count(); }
  
  /*
   * Get single fused result
   */
  const ObHybridSearchResult *get_result_at(int64_t index) const;
  
private:
  // Calculate statistics
  int calculate_statistics();
  
  // Normalize single score
  double normalize_score(double score, bool is_fts);
  
  // Apply normalization strategy
  double apply_normalization(double score, bool is_fts);
  
  // Min-Max normalization
  double min_max_normalize(double score, double min_val, double max_val);
  
  // Z-Score normalization
  double z_score_normalize(double score, double mean, double stddev);
  
  // Sigmoid normalization
  double sigmoid_normalize(double score);
  
  // Validate configuration parameters
  int validate_config(const ObWeightedFusionConfig &config) const;
  
private:
  // Weighted fusion configuration
  ObWeightedFusionConfig fusion_config_;
  
  // Normalization configuration
  ObNormalizationConfig norm_config_;
  
  // Full-text search results
  common::ObSEArray<ObHybridSearchResult, 64> fts_results_;
  
  // Vector search results
  common::ObSEArray<ObHybridSearchResult, 64> vector_results_;
  
  // Fused results
  common::ObSEArray<ObHybridSearchResult, 64> fused_results_;
  
  // Statistics for full-text search scores
  struct FTSStats
  {
    double min_score_ = 0.0;
    double max_score_ = 0.0;
    double mean_score_ = 0.0;
    double stddev_ = 0.0;
    int64_t count_ = 0;
  } fts_stats_;
  
  // Statistics for vector search scores
  struct VectorStats
  {
    double min_score_ = 0.0;
    double max_score_ = 0.0;
    double mean_score_ = 0.0;
    double stddev_ = 0.0;
    int64_t count_ = 0;
  } vector_stats_;
  
  // Whether initialization is complete
  bool is_initialized_;
  
  // Memory allocator (non-owner)
  ObIAllocator *allocator_;
  
  // Result mapping table for deduplication
  ResultMap result_map_;
  
  // Whether statistics have been calculated
  bool stats_calculated_;
};

} // namespace common
} // namespace oceanbase

#endif // OB_WEIGHTED_FUSION_H
