#ifndef OB_RRF_FUSION_H
#define OB_RRF_FUSION_H

#include "ob_hybrid_search_common.h"
#include "lib/container/ob_se_array.h"
#include "lib/hash/ob_hashmap.h"

namespace oceanbase
{
namespace common
{

/*
 * RRF (Reciprocal Rank Fusion) Fusion Implementation
 * 
 * Basic Principle:
 * RRF is a parameter-free fusion algorithm that converts multiple ranked lists into scores,
 * and combines these scores to generate a hybrid ranking.
 * 
 * Formula:
 * score = 1/(rank + rank_constant) for each search engine
 * final_score = score_from_fts + score_from_vector
 * 
 * Advantages:
 * 1. Automatic normalization: naturally solves normalization problems between different scoring systems
 * 2. Strong robustness: insensitive to outliers
 * 3. Simple parameters: only requires configuring rank_constant
 * 4. Excellent performance: no extra normalization computation needed
 * 
 * Application Scenarios:
 * - Search applications that need to balance keyword matching and semantic similarity
 * - Applications robust to anomalous score values
 * - Medium-scale datasets (typically rank_window_size = 100-1000)
 */
class ObRRFFusion
{
public:
  typedef common::hash::ObHashMap<uint64_t, ObHybridSearchResult> ResultMap;
  
  ObRRFFusion();
  virtual ~ObRRFFusion();
  
  /*
   * Initialize RRF fusion engine
   * 
   * @param config RRF configuration parameters
   * @param allocator Memory allocator
   * @return Returns OB_SUCCESS on success, corresponding error code on failure
   */
  int init(const ObRRFConfig &config, ObIAllocator &allocator);
  
  /*
   * Add full-text search results
   * 
   * @param fts_results Full-text search result list, sorted by relevance in descending order
   * @return Returns OB_SUCCESS on success, corresponding error code on failure
   */
  int add_fts_results(const common::ObIArray<ObHybridSearchResult> &fts_results);
  
  /*
   * Add vector search results
   * 
   * @param vector_results Vector search result list, sorted by similarity in descending order
   * @return Returns OB_SUCCESS on success, corresponding error code on failure
   */
  int add_vector_results(const common::ObIArray<ObHybridSearchResult> &vector_results);
  
  /*
   * Execute RRF fusion calculation
   * 
   * This method will:
   * 1. Assign ranks to each result in both result lists
   * 2. Calculate normalized scores using RRF formula
   * 3. Merge results from both lists
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
   * Get count of full-text search results
   */
  int64_t get_fts_result_count() const { return fts_results_.count(); }
  
  /*
   * Get count of vector search results
   */
  int64_t get_vector_result_count() const { return vector_results_.count(); }
  
  /*
   * Get count of fused results
   */
  int64_t get_fused_result_count() const { return fused_results_.count(); }
  
  /*
   * Get single fused result
   * 
   * @param index Result index
   * @return Fused result, returns empty result if index is out of bounds
   */
  const ObHybridSearchResult *get_result_at(int64_t index) const;
  
private:
  // Calculate RRF score
  double calculate_rrf_score(int64_t rank) const;
  
  // Validate configuration parameters
  int validate_config() const;
  
private:
  // RRF configuration parameters
  ObRRFConfig config_;
  
  // Full-text search results
  common::ObSEArray<ObHybridSearchResult, 64> fts_results_;
  
  // Vector search results
  common::ObSEArray<ObHybridSearchResult, 64> vector_results_;
  
  // Fused results
  common::ObSEArray<ObHybridSearchResult, 64> fused_results_;
  
  // Record initialization state
  bool is_initialized_;
  
  // Memory allocator (non-owner)
  ObIAllocator *allocator_;
  
  // Result mapping table for deduplication
  ResultMap result_map_;
};

} // namespace common
} // namespace oceanbase

#endif // OB_RRF_FUSION_H
