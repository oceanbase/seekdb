/*
 * Hybrid Search Demo Examples and Test Cases
 * 
 * This file demonstrates how to use the hybrid search API to implement fusion
 * of vector and full-text search. Includes demo code and unit tests for multiple scenarios.
 */

#include "ob_rrf_fusion.h"
#include "ob_weighted_fusion.h"
#include "lib/allocator/ob_malloc.h"
#include <iostream>
#include <iomanip>

namespace oceanbase
{
namespace common
{

/*
 * =============================================
 * Demo 1: Using RRF Fusion Method
 * =============================================
 * Scenario: Balanced hybrid search with automatic normalization
 * Use Case: Applications that need to balance keyword matching and semantic similarity
 */
void demo_rrf_fusion()
{
  std::cout << "\n========== RRF Fusion Demo ==========" << std::endl;
  
  // 1. Prepare memory allocator
  ObMallocAllocator allocator;
  
  // 2. Create RRF fusion engine
  ObRRFFusion rrf_fusion;
  
  // 3. Configure parameters
  // rank_constant 60 means ranking differences have relatively small impact
  // rank_window_size 100 means fusing from 100 search results
  ObRRFConfig rrf_config(60, 100);
  
  if (rrf_fusion.init(rrf_config, allocator) != OB_SUCCESS) {
    std::cout << "Failed to initialize RRF fusion" << std::endl;
    return;
  }
  
  // 4. Prepare full-text search results (BM25 scores, typically between 0 and tens)
  common::ObSEArray<ObHybridSearchResult, 16> fts_results;
  ObHybridSearchResult fts_result1;
  fts_result1.doc_id_ = 1;
  fts_result1.fts_score_ = 15.5;
  fts_results.push_back(fts_result1);
  
  ObHybridSearchResult fts_result2;
  fts_result2.doc_id_ = 2;
  fts_result2.fts_score_ = 12.3;
  fts_results.push_back(fts_result2);
  
  ObHybridSearchResult fts_result3;
  fts_result3.doc_id_ = 3;
  fts_result3.fts_score_ = 8.7;
  fts_results.push_back(fts_result3);
  
  ObHybridSearchResult fts_result4;
  fts_result4.doc_id_ = 4;
  fts_result4.fts_score_ = 5.2;
  fts_results.push_back(fts_result4);
  
  std::cout << "FTS Results:" << std::endl;
  for (const auto &r : fts_results) {
    std::cout << "  Doc ID: " << r.doc_id_ << ", Score: " << std::fixed << std::setprecision(2) << r.fts_score_ << std::endl;
  }
  
  // 5. Prepare vector search results (vector similarity, typically between 0-1)
  common::ObSEArray<ObHybridSearchResult, 16> vector_results;
  ObHybridSearchResult vec_result1;
  vec_result1.doc_id_ = 2;
  vec_result1.vector_score_ = 0.95;
  vector_results.push_back(vec_result1);
  
  ObHybridSearchResult vec_result2;
  vec_result2.doc_id_ = 1;
  vec_result2.vector_score_ = 0.88;
  vector_results.push_back(vec_result2);
  
  ObHybridSearchResult vec_result3;
  vec_result3.doc_id_ = 5;
  vec_result3.vector_score_ = 0.82;
  vector_results.push_back(vec_result3);
  
  ObHybridSearchResult vec_result4;
  vec_result4.doc_id_ = 3;
  vec_result4.vector_score_ = 0.75;
  vector_results.push_back(vec_result4);
  
  std::cout << "\nVector Results:" << std::endl;
  for (const auto &r : vector_results) {
    std::cout << "  Doc ID: " << r.doc_id_ << ", Score: " << std::fixed << std::setprecision(2) << r.vector_score_ << std::endl;
  }
  
  // 6. Add search results and perform fusion
  if (rrf_fusion.add_fts_results(fts_results) != OB_SUCCESS ||
      rrf_fusion.add_vector_results(vector_results) != OB_SUCCESS ||
      rrf_fusion.fuse() != OB_SUCCESS) {
    std::cout << "Failed to perform RRF fusion" << std::endl;
    return;
  }
  
  // 7. Get fusion results (top 5)
  common::ObSEArray<ObHybridSearchResult, 16> fused_results;
  if (rrf_fusion.get_results(fused_results, 5) != OB_SUCCESS) {
    std::cout << "Failed to get results" << std::endl;
    return;
  }
  
  std::cout << "\nFused Results (Top 5):" << std::endl;
  std::cout << std::left << std::setw(10) << "Doc ID" 
            << std::setw(15) << "FTS Score" 
            << std::setw(15) << "Vector Score"
            << std::setw(15) << "Final Score"
            << std::setw(10) << "Source" << std::endl;
  std::cout << std::string(65, '-') << std::endl;
  
  for (const auto &result : fused_results) {
    std::string source = "None";
    if (result.source_flag_ == 1) source = "FTS Only";
    else if (result.source_flag_ == 2) source = "Vec Only";
    else if (result.source_flag_ == 3) source = "Both";
    
    std::cout << std::left 
              << std::setw(10) << result.doc_id_
              << std::setw(15) << std::fixed << std::setprecision(4) << result.fts_score_
              << std::setw(15) << std::fixed << std::setprecision(4) << result.vector_score_
              << std::setw(15) << std::fixed << std::setprecision(4) << result.final_score_
              << std::setw(10) << source << std::endl;
  }
}

/*
 * =============================================
 * Demo 2: Using Weighted Fusion Method - Balanced Approach
 * =============================================
 * Scenario: Equal weights for full-text and vector search (50% each)
 * Use Case: Both keyword matching and semantic similarity are equally important
 */
void demo_weighted_fusion_balanced()
{
  std::cout << "\n========== Weighted Fusion Demo (Balanced 50:50) ==========" << std::endl;
  
  // 1. Prepare memory allocator
  ObMallocAllocator allocator;
  
  // 2. Create weighted fusion engine
  ObWeightedFusion weighted_fusion;
  
  // 3. Configure parameters - Balanced approach
  ObWeightedFusionConfig fusion_config(0.5, 0.5, true);  // 50% FTS, 50% Vector, enable normalization
  
  // 4. Normalization configuration - Use Min-Max normalization
  ObNormalizationConfig norm_config;
  norm_config.norm_type_ = ObNormalizationConfig::NormalizationType::MIN_MAX;
  
  if (weighted_fusion.init(fusion_config, norm_config, allocator) != OB_SUCCESS) {
    std::cout << "Failed to initialize weighted fusion" << std::endl;
    return;
  }
  
  // 5. Prepare test data (same as RRF demo)
  common::ObSEArray<ObHybridSearchResult, 16> fts_results;
  ObHybridSearchResult fts_r1 = {1, 15.5, 0.0, -1, -1, 0.0, 0};
  ObHybridSearchResult fts_r2 = {2, 12.3, 0.0, -1, -1, 0.0, 0};
  ObHybridSearchResult fts_r3 = {3, 8.7, 0.0, -1, -1, 0.0, 0};
  fts_results.push_back(fts_r1);
  fts_results.push_back(fts_r2);
  fts_results.push_back(fts_r3);
  
  common::ObSEArray<ObHybridSearchResult, 16> vector_results;
  ObHybridSearchResult vec_r1 = {2, 0.0, 0.95, -1, -1, 0.0, 0};
  ObHybridSearchResult vec_r2 = {1, 0.0, 0.88, -1, -1, 0.0, 0};
  ObHybridSearchResult vec_r3 = {5, 0.0, 0.82, -1, -1, 0.0, 0};
  vector_results.push_back(vec_r1);
  vector_results.push_back(vec_r2);
  vector_results.push_back(vec_r3);
  
  // 6. Perform fusion
  if (weighted_fusion.add_fts_results(fts_results) != OB_SUCCESS ||
      weighted_fusion.add_vector_results(vector_results) != OB_SUCCESS ||
      weighted_fusion.fuse() != OB_SUCCESS) {
    std::cout << "Failed to perform weighted fusion" << std::endl;
    return;
  }
  
  // 7. Get results
  common::ObSEArray<ObHybridSearchResult, 16> fused_results;
  if (weighted_fusion.get_results(fused_results, 5) != OB_SUCCESS) {
    std::cout << "Failed to get results" << std::endl;
    return;
  }
  
  std::cout << "Fused Results (Balanced 50:50):" << std::endl;
  std::cout << std::left << std::setw(10) << "Doc ID" 
            << std::setw(15) << "Norm FTS" 
            << std::setw(15) << "Norm Vector"
            << std::setw(15) << "Final Score" << std::endl;
  std::cout << std::string(55, '-') << std::endl;
  
  for (const auto &result : fused_results) {
    std::cout << std::left 
              << std::setw(10) << result.doc_id_
              << std::setw(15) << std::fixed << std::setprecision(4) << result.fts_score_
              << std::setw(15) << std::fixed << std::setprecision(4) << result.vector_score_
              << std::setw(15) << std::fixed << std::setprecision(4) << result.final_score_ << std::endl;
  }
}

/*
 * =============================================
 * Demo 3: Using Weighted Fusion Method - Exact Match Priority
 * =============================================
 * Scenario: Prioritize keyword exact matching (70% FTS, 30% Vector)
 * Use Case: Users' search keywords are usually accurate, minimal semantic understanding needed
 */
void demo_weighted_fusion_keyword_priority()
{
  std::cout << "\n========== Weighted Fusion Demo (Keyword Priority 70:30) ==========" << std::endl;
  
  ObMallocAllocator allocator;
  ObWeightedFusion weighted_fusion;
  
  // 70% full-text search, 30% vector search
  ObWeightedFusionConfig fusion_config(0.7, 0.3, true);
  
  // Use Z-Score normalization
  ObNormalizationConfig norm_config;
  norm_config.norm_type_ = ObNormalizationConfig::NormalizationType::Z_SCORE;
  
  if (weighted_fusion.init(fusion_config, norm_config, allocator) != OB_SUCCESS) {
    std::cout << "Failed to initialize weighted fusion" << std::endl;
    return;
  }
  
  // Prepare data
  common::ObSEArray<ObHybridSearchResult, 16> fts_results;
  for (int i = 0; i < 3; ++i) {
    ObHybridSearchResult result = {static_cast<uint64_t>(i+1), 10.0 - i*3, 0.0, -1, -1, 0.0, 0};
    fts_results.push_back(result);
  }
  
  common::ObSEArray<ObHybridSearchResult, 16> vector_results;
  for (int i = 0; i < 3; ++i) {
    ObHybridSearchResult result = {static_cast<uint64_t>((i+1)%3+1), 0.9 - i*0.05, 0.0, -1, -1, 0.0, 0};
    vector_results.push_back(result);
  }
  
  if (weighted_fusion.add_fts_results(fts_results) != OB_SUCCESS ||
      weighted_fusion.add_vector_results(vector_results) != OB_SUCCESS ||
      weighted_fusion.fuse() != OB_SUCCESS) {
    std::cout << "Failed to perform fusion" << std::endl;
    return;
  }
  
  common::ObSEArray<ObHybridSearchResult, 16> fused_results;
  if (weighted_fusion.get_results(fused_results, 5) != OB_SUCCESS) {
    std::cout << "Failed to get results" << std::endl;
    return;
  }
  
  std::cout << "Fused Results (Keyword Priority 70:30):" << std::endl;
  for (int i = 0; i < fused_results.count(); ++i) {
    const auto &result = fused_results.at(i);
    std::cout << "  Rank " << (i+1) << ": Doc ID " << result.doc_id_ 
              << ", Score: " << std::fixed << std::setprecision(4) << result.final_score_ << std::endl;
  }
}

/*
 * =============================================
 * Demo 4: Using Weighted Fusion Method - Semantic Similarity Priority
 * =============================================
 * Scenario: Prioritize semantic similarity (30% FTS, 70% Vector)
 * Use Case: Complex user search intent, need to understand semantics through vector search
 */
void demo_weighted_fusion_semantic_priority()
{
  std::cout << "\n========== Weighted Fusion Demo (Semantic Priority 30:70) ==========" << std::endl;
  
  ObMallocAllocator allocator;
  ObWeightedFusion weighted_fusion;
  
  // 30% full-text search, 70% vector search
  ObWeightedFusionConfig fusion_config(0.3, 0.7, true);
  
  // Use Min-Max normalization
  ObNormalizationConfig norm_config;
  norm_config.norm_type_ = ObNormalizationConfig::NormalizationType::MIN_MAX;
  
  if (weighted_fusion.init(fusion_config, norm_config, allocator) != OB_SUCCESS) {
    std::cout << "Failed to initialize weighted fusion" << std::endl;
    return;
  }
  
  // Prepare data
  common::ObSEArray<ObHybridSearchResult, 16> fts_results;
  ObHybridSearchResult fts1 = {1, 8.0, 0.0, -1, -1, 0.0, 0};
  ObHybridSearchResult fts2 = {2, 6.5, 0.0, -1, -1, 0.0, 0};
  fts_results.push_back(fts1);
  fts_results.push_back(fts2);
  
  common::ObSEArray<ObHybridSearchResult, 16> vector_results;
  ObHybridSearchResult vec1 = {2, 0.0, 0.92, -1, -1, 0.0, 0};
  ObHybridSearchResult vec2 = {1, 0.0, 0.85, -1, -1, 0.0, 0};
  vector_results.push_back(vec1);
  vector_results.push_back(vec2);
  
  if (weighted_fusion.add_fts_results(fts_results) != OB_SUCCESS ||
      weighted_fusion.add_vector_results(vector_results) != OB_SUCCESS ||
      weighted_fusion.fuse() != OB_SUCCESS) {
    std::cout << "Failed to perform fusion" << std::endl;
    return;
  }
  
  common::ObSEArray<ObHybridSearchResult, 16> fused_results;
  if (weighted_fusion.get_results(fused_results) != OB_SUCCESS) {
    std::cout << "Failed to get results" << std::endl;
    return;
  }
  
  std::cout << "Fused Results (Semantic Priority 30:70):" << std::endl;
  for (int i = 0; i < fused_results.count(); ++i) {
    const auto &result = fused_results.at(i);
    std::cout << "  Rank " << (i+1) << ": Doc ID " << result.doc_id_ 
              << ", Score: " << std::fixed << std::setprecision(4) << result.final_score_ << std::endl;
  }
}

} // namespace common
} // namespace oceanbase

int main()
{
  std::cout << "========================================" << std::endl;
  std::cout << "Hybrid Search Fusion Demonstrations" << std::endl;
  std::cout << "========================================" << std::endl;
  
  // Run all demos
  oceanbase::common::demo_rrf_fusion();
  oceanbase::common::demo_weighted_fusion_balanced();
  oceanbase::common::demo_weighted_fusion_keyword_priority();
  oceanbase::common::demo_weighted_fusion_semantic_priority();
  
  std::cout << "\n========================================" << std::endl;
  std::cout << "All demonstrations completed successfully!" << std::endl;
  std::cout << "========================================" << std::endl;
  
  return 0;
}
