/*
 * 混合搜索演示示例和测试用例
 * 
 * 本文件展示如何使用混合搜索 API 实现向量和全文搜索的融合。
 * 包含多个场景的演示代码和单元测试。
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
 * 演示 1: 使用 RRF 融合方法
 * =============================================
 * 场景：平衡的混合搜索，需要自动规范化
 * 适用场景：关键词匹配和语义相似度需要平衡的应用
 */
void demo_rrf_fusion()
{
  std::cout << "\n========== RRF Fusion Demo ==========" << std::endl;
  
  // 1. 准备内存分配器
  ObMallocAllocator allocator;
  
  // 2. 创建 RRF 融合器
  ObRRFFusion rrf_fusion;
  
  // 3. 配置参数
  // rank_constant 为 60 表示排名差异影响相对较小
  // rank_window_size 为 100 表示从 100 个搜索结果中融合
  ObRRFConfig rrf_config(60, 100);
  
  if (rrf_fusion.init(rrf_config, allocator) != OB_SUCCESS) {
    std::cout << "Failed to initialize RRF fusion" << std::endl;
    return;
  }
  
  // 4. 准备全文搜索结果（BM25 得分，通常在 0-几十之间）
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
  
  // 5. 准备向量搜索结果（向量相似度，通常在 0-1 之间）
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
  
  // 6. 添加搜索结果并执行融合
  if (rrf_fusion.add_fts_results(fts_results) != OB_SUCCESS ||
      rrf_fusion.add_vector_results(vector_results) != OB_SUCCESS ||
      rrf_fusion.fuse() != OB_SUCCESS) {
    std::cout << "Failed to perform RRF fusion" << std::endl;
    return;
  }
  
  // 7. 获取融合结果（取前 5 个）
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
 * 演示 2: 使用加权融合方法 - 平衡方案
 * =============================================
 * 场景：全文搜索和向量搜索权重相等（各 50%）
 * 适用场景：关键词匹配和语义相似度同等重要
 */
void demo_weighted_fusion_balanced()
{
  std::cout << "\n========== Weighted Fusion Demo (Balanced 50:50) ==========" << std::endl;
  
  // 1. 准备内存分配器
  ObMallocAllocator allocator;
  
  // 2. 创建加权融合器
  ObWeightedFusion weighted_fusion;
  
  // 3. 配置参数 - 平衡方案
  ObWeightedFusionConfig fusion_config(0.5, 0.5, true);  // 50% FTS, 50% Vector, enable normalization
  
  // 4. 规范化配置 - 使用 Min-Max 规范化
  ObNormalizationConfig norm_config;
  norm_config.norm_type_ = ObNormalizationConfig::NormalizationType::MIN_MAX;
  
  if (weighted_fusion.init(fusion_config, norm_config, allocator) != OB_SUCCESS) {
    std::cout << "Failed to initialize weighted fusion" << std::endl;
    return;
  }
  
  // 5. 准备测试数据（同 RRF 演示）
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
  
  // 6. 执行融合
  if (weighted_fusion.add_fts_results(fts_results) != OB_SUCCESS ||
      weighted_fusion.add_vector_results(vector_results) != OB_SUCCESS ||
      weighted_fusion.fuse() != OB_SUCCESS) {
    std::cout << "Failed to perform weighted fusion" << std::endl;
    return;
  }
  
  // 7. 获取结果
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
 * 演示 3: 使用加权融合方法 - 精确匹配优先
 * =============================================
 * 场景：优先考虑关键词精确匹配（70% FTS, 30% Vector）
 * 适用场景：用户搜索关键词通常准确，不需要太多语义理解
 */
void demo_weighted_fusion_keyword_priority()
{
  std::cout << "\n========== Weighted Fusion Demo (Keyword Priority 70:30) ==========" << std::endl;
  
  ObMallocAllocator allocator;
  ObWeightedFusion weighted_fusion;
  
  // 70% 全文搜索，30% 向量搜索
  ObWeightedFusionConfig fusion_config(0.7, 0.3, true);
  
  // 使用 Z-Score 规范化
  ObNormalizationConfig norm_config;
  norm_config.norm_type_ = ObNormalizationConfig::NormalizationType::Z_SCORE;
  
  if (weighted_fusion.init(fusion_config, norm_config, allocator) != OB_SUCCESS) {
    std::cout << "Failed to initialize weighted fusion" << std::endl;
    return;
  }
  
  // 准备数据
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
 * 演示 4: 使用加权融合方法 - 语义相似度优先
 * =============================================
 * 场景：优先考虑语义相似度（30% FTS, 70% Vector）
 * 适用场景：用户搜索意图复杂，需要通过向量搜索理解语义
 */
void demo_weighted_fusion_semantic_priority()
{
  std::cout << "\n========== Weighted Fusion Demo (Semantic Priority 30:70) ==========" << std::endl;
  
  ObMallocAllocator allocator;
  ObWeightedFusion weighted_fusion;
  
  // 30% 全文搜索，70% 向量搜索
  ObWeightedFusionConfig fusion_config(0.3, 0.7, true);
  
  // 使用 Min-Max 规范化
  ObNormalizationConfig norm_config;
  norm_config.norm_type_ = ObNormalizationConfig::NormalizationType::MIN_MAX;
  
  if (weighted_fusion.init(fusion_config, norm_config, allocator) != OB_SUCCESS) {
    std::cout << "Failed to initialize weighted fusion" << std::endl;
    return;
  }
  
  // 准备数据
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
  
  // 运行所有演示
  oceanbase::common::demo_rrf_fusion();
  oceanbase::common::demo_weighted_fusion_balanced();
  oceanbase::common::demo_weighted_fusion_keyword_priority();
  oceanbase::common::demo_weighted_fusion_semantic_priority();
  
  std::cout << "\n========================================" << std::endl;
  std::cout << "All demonstrations completed successfully!" << std::endl;
  std::cout << "========================================" << std::endl;
  
  return 0;
}
