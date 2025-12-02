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
 * 加权融合方法实现
 * 
 * 基本原理：
 * 通过为全文搜索和向量搜索分配权重，
 * 然后对每个文档的分数进行规范化和加权求和。
 * 
 * 公式：
 * final_score = fts_weight * normalized_fts_score + vector_weight * normalized_vector_score
 * 
 * 优点：
 * 1. 精细控制：可以精确控制全文和向量搜索的影响比例
 * 2. 灵活适应：支持多种规范化策略
 * 3. 业务导向：可根据业务场景动态调整权重
 * 
 * 应用场景：
 * - 需要精细控制全文和向量搜索比例的应用
 * - 对特定业务有明确偏好的场景（如优先关键词匹配或语义相似度）
 * - 可以根据查询类型动态调整权重的应用
 */
class ObWeightedFusion
{
public:
  typedef common::hash::ObHashMap<uint64_t, ObHybridSearchResult> ResultMap;
  
  ObWeightedFusion();
  virtual ~ObWeightedFusion();
  
  /*
   * 初始化加权融合器
   * 
   * @param config 加权融合配置参数
   * @param norm_config 规范化配置参数
   * @param allocator 内存分配器
   * @return 成功返回 OB_SUCCESS，失败返回相应错误码
   */
  int init(const ObWeightedFusionConfig &config,
           const ObNormalizationConfig &norm_config,
           ObIAllocator &allocator);
  
  /*
   * 添加全文搜索结果
   * 
   * @param fts_results 全文搜索结果列表
   * @return 成功返回 OB_SUCCESS，失败返回相应错误码
   */
  int add_fts_results(const common::ObIArray<ObHybridSearchResult> &fts_results);
  
  /*
   * 添加向量搜索结果
   * 
   * @param vector_results 向量搜索结果列表
   * @return 成功返回 OB_SUCCESS，失败返回相应错误码
   */
  int add_vector_results(const common::ObIArray<ObHybridSearchResult> &vector_results);
  
  /*
   * 执行加权融合计算
   * 
   * 该方法会：
   * 1. 收集两个结果列表中的所有得分统计信息
   * 2. 根据规范化策略进行得分规范化
   * 3. 应用权重进行加权求和
   * 4. 按最终得分排序
   * 
   * @return 成功返回 OB_SUCCESS，失败返回相应错误码
   */
  int fuse();
  
  /*
   * 获取融合后的结果
   * 
   * @param results 输出参数，包含融合后的结果列表
   * @param limit 返回结果的最大数量，0 表示返回全部结果
   * @return 成功返回 OB_SUCCESS，失败返回相应错误码
   */
  int get_results(common::ObIArray<ObHybridSearchResult> &results, int64_t limit = 0) const;
  
  /*
   * 重置融合器状态，准备下一次融合
   */
  void reset();
  
  /*
   * 获取融合后的结果数量
   */
  int64_t get_fused_result_count() const { return fused_results_.count(); }
  
  /*
   * 获取单个融合后的结果
   */
  const ObHybridSearchResult *get_result_at(int64_t index) const;
  
private:
  // 计算统计信息
  int calculate_statistics();
  
  // 规范化单个得分
  double normalize_score(double score, bool is_fts);
  
  // 应用规范化策略
  double apply_normalization(double score, bool is_fts);
  
  // Min-Max 规范化
  double min_max_normalize(double score, double min_val, double max_val);
  
  // Z-Score 规范化
  double z_score_normalize(double score, double mean, double stddev);
  
  // Sigmoid 规范化
  double sigmoid_normalize(double score);
  
  // 验证配置参数
  int validate_config() const;
  
private:
  // 加权融合配置
  ObWeightedFusionConfig fusion_config_;
  
  // 规范化配置
  ObNormalizationConfig norm_config_;
  
  // 全文搜索结果
  common::ObSEArray<ObHybridSearchResult, 64> fts_results_;
  
  // 向量搜索结果
  common::ObSEArray<ObHybridSearchResult, 64> vector_results_;
  
  // 融合后的结果
  common::ObSEArray<ObHybridSearchResult, 64> fused_results_;
  
  // 全文搜索分数的统计信息
  struct FTSStats
  {
    double min_score_ = 0.0;
    double max_score_ = 0.0;
    double mean_score_ = 0.0;
    double stddev_ = 0.0;
    int64_t count_ = 0;
  } fts_stats_;
  
  // 向量搜索分数的统计信息
  struct VectorStats
  {
    double min_score_ = 0.0;
    double max_score_ = 0.0;
    double mean_score_ = 0.0;
    double stddev_ = 0.0;
    int64_t count_ = 0;
  } vector_stats_;
  
  // 是否已初始化
  bool is_initialized_;
  
  // 内存分配器（不拥有所有权）
  ObIAllocator *allocator_;
  
  // 用于去重的结果映射表
  ResultMap result_map_;
  
  // 是否已计算统计信息
  bool stats_calculated_;
};

} // namespace common
} // namespace oceanbase

#endif // OB_WEIGHTED_FUSION_H
