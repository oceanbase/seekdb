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

#ifndef OCEANBASE_SQL_ENGINE_IDX_OB_FT_INDEX_OPTIMIZER_H_
#define OCEANBASE_SQL_ENGINE_IDX_OB_FT_INDEX_OPTIMIZER_H_

#include "lib/container/ob_se_array.h"
#include "common/ob_i_task.h"

namespace oceanbase
{
namespace sql
{

struct ObFTOptimizationStats
{
  int64_t total_tokens_;
  int64_t unique_tokens_;
  int64_t index_size_bytes_;
  int64_t query_count_;
  int64_t avg_query_time_us_;
  int64_t tokenization_time_us_;
  int64_t indexing_time_us_;

  ObFTOptimizationStats()
    : total_tokens_(0),
      unique_tokens_(0),
      index_size_bytes_(0),
      query_count_(0),
      avg_query_time_us_(0),
      tokenization_time_us_(0),
      indexing_time_us_(0) {}

  TO_STRING_KV(K_(total_tokens), K_(unique_tokens), K_(index_size_bytes),
               K_(query_count), K_(avg_query_time_us),
               K_(tokenization_time_us), K_(indexing_time_us));
};

class ObFTIndexOptimizer
{
public:
  ObFTIndexOptimizer();
  virtual ~ObFTIndexOptimizer();

  // Initialize optimizer
  int init();

  // Collect statistics on full-text index
  int collect_stats(uint64_t tenant_id,
                    uint64_t table_id,
                    uint64_t index_id,
                    ObFTOptimizationStats& stats);

  // Optimize tokenization (hot path optimization)
  int optimize_tokenization(uint64_t tenant_id,
                            uint64_t table_id);

  // Optimize index construction
  int optimize_index_construction(uint64_t tenant_id,
                                  uint64_t table_id,
                                  uint64_t index_id);

  // Optimize query execution plan
  int optimize_query_execution(uint64_t tenant_id,
                               uint64_t table_id,
                               uint64_t index_id);

  // Apply all optimizations
  int apply_all_optimizations(uint64_t tenant_id,
                              uint64_t table_id,
                              uint64_t index_id);

  // Get optimization recommendations
  int get_recommendations(const ObFTOptimizationStats& stats,
                          common::ObIAllocator& allocator,
                          common::ObSEArray<common::ObString, 8>& recommendations);

private:
  // Analyze token distribution
  int analyze_token_distribution(uint64_t table_id,
                                 uint64_t index_id,
                                 ObFTOptimizationStats& stats);

  // Optimize inverted index structure
  int optimize_inverted_index(uint64_t table_id,
                              uint64_t index_id);

  // Optimize term dictionary compression
  int optimize_term_dictionary(uint64_t table_id,
                               uint64_t index_id);

  // Cache hot terms
  int cache_hot_terms(uint64_t table_id,
                      uint64_t index_id);

private:
  bool is_inited_;

  DISALLOW_COPY_AND_ASSIGN(ObFTIndexOptimizer);
};

// Global optimizer instance
extern ObFTIndexOptimizer* g_ft_index_optimizer;

} // namespace sql
} // namespace oceanbase

#endif // OCEANBASE_SQL_ENGINE_IDX_OB_FT_INDEX_OPTIMIZER_H_
