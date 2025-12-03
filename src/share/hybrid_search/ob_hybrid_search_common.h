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

#ifndef OB_HYBRID_SEARCH_COMMON_H
#define OB_HYBRID_SEARCH_COMMON_H

#include "lib/ob_define.h"
#include "lib/oblog/ob_log.h"
#include "lib/container/ob_se_array.h"

namespace oceanbase
{
namespace common
{

// Hybrid search fusion method types
enum class ObHybridSearchFusionType
{
  UNKNOWN = 0,
  RRF = 1,           // Reciprocal Rank Fusion
  WEIGHT_SUM = 2,    // Weighted Sum Fusion
  MIN_MAX_NORM = 3,  // Min-Max Normalization Fusion
  Z_SCORE_NORM = 4   // Z-Score Normalization Fusion
};

// Configuration parameters for RRF method
struct ObRRFConfig
{
  // Rank constant for balancing documents with low and high ranks
  // Formula: score = 1 / (rank + rank_constant)
  // Larger values are more favorable for low-ranked documents
  int64_t rank_constant_ = 60;
  
  // Window size for each sub-query, recommended as 10-20 times the number of final results
  int64_t rank_window_size_ = 100;
  
  ObRRFConfig() = default;
  ObRRFConfig(int64_t rank_const, int64_t window_size)
    : rank_constant_(rank_const), rank_window_size_(window_size) {}
};

// Configuration parameters for weighted fusion
struct ObWeightedFusionConfig
{
  // Weight for full-text search, range [0, 1]
  double fts_weight_ = 0.5;
  
  // Weight for vector search, range [0, 1]
  double vector_weight_ = 0.5;
  
  // Normalization strategy: whether to normalize scores
  bool enable_normalization_ = true;
  
  ObWeightedFusionConfig() = default;
  ObWeightedFusionConfig(double fts_w, double vec_w, bool normalize)
    : fts_weight_(fts_w), vector_weight_(vec_w), enable_normalization_(normalize) {}
};

// Normalization strategy configuration
struct ObNormalizationConfig
{
  // Normalization method type
  enum class NormalizationType
  {
    NONE = 0,          // No normalization
    MIN_MAX = 1,       // Min-Max normalization: (x - min) / (max - min)
    Z_SCORE = 2,       // Z-Score normalization: (x - mean) / stddev
    SIGMOID = 3        // Sigmoid normalization: 1 / (1 + exp(-x))
  };
  
  NormalizationType norm_type_ = NormalizationType::MIN_MAX;
  
  // Min and max values for Min-Max normalization
  double min_value_ = 0.0;
  double max_value_ = 1.0;
  
  // Mean value and standard deviation for Z-Score normalization
  double mean_value_ = 0.0;
  double stddev_value_ = 1.0;
  
  ObNormalizationConfig() = default;
};

// Single search result item
struct ObHybridSearchResult
{
  // Document ID
  uint64_t doc_id_ = 0;
  
  // Full-text search score (BM25)
  double fts_score_ = 0.0;
  
  // Vector search score (distance or similarity)
  double vector_score_ = 0.0;
  
  // Full-text search rank
  int64_t fts_rank_ = -1;
  
  // Vector search rank
  int64_t vector_rank_ = -1;
  
  // Final score after fusion
  double final_score_ = 0.0;
  
  // Source flag: 1 for FTS only, 2 for vector only, 3 for both
  int32_t source_flag_ = 0;
  
  bool operator<(const ObHybridSearchResult &other) const
  {
    // Sort by final score in descending order
    if (final_score_ != other.final_score_) {
      return final_score_ > other.final_score_;
    }
    return doc_id_ < other.doc_id_;
  }
  
  TO_STRING_KV(K_(doc_id), K_(fts_score), K_(vector_score),
               K_(fts_rank), K_(vector_rank), K_(final_score), K_(source_flag));
};

// Vector distance measurement type
enum class ObVectorDistanceType
{
  L2_DISTANCE = 0,      // Euclidean distance (L2)
  COSINE_DISTANCE = 1,  // Cosine distance
  INNER_PRODUCT = 2     // Inner product
};

// Helper class for vector similarity conversion
class ObVectorMetricConverter
{
public:
  // Convert vector distance to similarity (between 0 and 1)
  static double distance_to_similarity(double distance, ObVectorDistanceType type)
  {
    if (distance < 0) {
      distance = 0;
    }
    
    switch (type) {
      case ObVectorDistanceType::L2_DISTANCE:
        // L2 distance to similarity: similarity = 1 / (1 + distance)
        return 1.0 / (1.0 + distance);
      
      case ObVectorDistanceType::COSINE_DISTANCE:
        // Cosine distance to similarity: similarity = (1 - distance) / 2
        // Assumes cosine_distance range is [0, 2]
        return (1.0 - distance) / 2.0;
      
      case ObVectorDistanceType::INNER_PRODUCT:
        // Inner product is usually already similarity, but needs mapping to [0, 1] range
        // Assumes already normalized
        return distance > 1.0 ? 1.0 : (distance < 0.0 ? 0.0 : distance);
      
      default:
        return 0.0;
    }
  }
};

} // namespace common
} // namespace oceanbase

#endif // OB_HYBRID_SEARCH_COMMON_H
