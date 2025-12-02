#ifndef OB_HYBRID_SEARCH_COMMON_H
#define OB_HYBRID_SEARCH_COMMON_H

#include "lib/ob_define.h"
#include "lib/oblog/ob_log.h"
#include "lib/container/ob_se_array.h"

namespace oceanbase
{
namespace common
{

// 混合搜索融合方法类型
enum class ObHybridSearchFusionType
{
  UNKNOWN = 0,
  RRF = 1,           // Reciprocal Rank Fusion - 倒数排名融合
  WEIGHT_SUM = 2,    // Weighted Sum - 加权和融合
  MIN_MAX_NORM = 3,  // Min-Max 规范化融合
  Z_SCORE_NORM = 4   // Z-Score 规范化融合
};

// RRF 方法的配置参数
struct ObRRFConfig
{
  // 排名常数，用于平衡低排名和高排名的文档
  // 公式: score = 1 / (rank + rank_constant)
  // 较大的值对低排名文档更友好
  int64_t rank_constant_ = 60;
  
  // 每个子查询的窗口大小，建议为最终返回结果数的 10-20 倍
  int64_t rank_window_size_ = 100;
  
  ObRRFConfig() = default;
  ObRRFConfig(int64_t rank_const, int64_t window_size)
    : rank_constant_(rank_const), rank_window_size_(window_size) {}
};

// 加权融合的配置参数
struct ObWeightedFusionConfig
{
  // 全文搜索的权重，范围 [0, 1]
  double fts_weight_ = 0.5;
  
  // 向量搜索的权重，范围 [0, 1]
  double vector_weight_ = 0.5;
  
  // 规范化策略：是否需要对分数进行规范化
  bool enable_normalization_ = true;
  
  ObWeightedFusionConfig() = default;
  ObWeightedFusionConfig(double fts_w, double vec_w, bool normalize)
    : fts_weight_(fts_w), vector_weight_(vec_w), enable_normalization_(normalize) {}
};

// 规范化策略配置
struct ObNormalizationConfig
{
  // 规范化方法类型
  enum class NormalizationType
  {
    NONE = 0,          // 不进行规范化
    MIN_MAX = 1,       // Min-Max 规范化：(x - min) / (max - min)
    Z_SCORE = 2,       // Z-Score 规范化：(x - mean) / stddev
    SIGMOID = 3        // Sigmoid 规范化：1 / (1 + exp(-x))
  };
  
  NormalizationType norm_type_ = NormalizationType::MIN_MAX;
  
  // Min-Max 规范化的最小值和最大值
  double min_value_ = 0.0;
  double max_value_ = 1.0;
  
  // Z-Score 规范化的平均值和标准差
  double mean_value_ = 0.0;
  double stddev_value_ = 1.0;
  
  ObNormalizationConfig() = default;
};

// 单个搜索结果项
struct ObHybridSearchResult
{
  // 文档 ID
  uint64_t doc_id_ = 0;
  
  // 全文搜索分数（BM25）
  double fts_score_ = 0.0;
  
  // 向量搜索分数（距离或相似度）
  double vector_score_ = 0.0;
  
  // 全文搜索排名
  int64_t fts_rank_ = -1;
  
  // 向量搜索排名
  int64_t vector_rank_ = -1;
  
  // 融合后的最终分数
  double final_score_ = 0.0;
  
  // 来源标记：1 表示仅来自全文搜索，2 表示仅来自向量搜索，3 表示两者都有
  int32_t source_flag_ = 0;
  
  bool operator<(const ObHybridSearchResult &other) const
  {
    // 按最终分数降序排序
    if (final_score_ != other.final_score_) {
      return final_score_ > other.final_score_;
    }
    return doc_id_ < other.doc_id_;
  }
  
  TO_STRING_KV(K_(doc_id), K_(fts_score), K_(vector_score),
               K_(fts_rank), K_(vector_rank), K_(final_score), K_(source_flag));
};

// 向量距离度量类型
enum class ObVectorDistanceType
{
  L2_DISTANCE = 0,      // 欧几里得距离 (L2)
  COSINE_DISTANCE = 1,  // 余弦距离
  INNER_PRODUCT = 2     // 内积
};

// 向量相似度转换辅助函数
class ObVectorMetricConverter
{
public:
  // 将向量距离转换为相似度（0 到 1 之间）
  static double distance_to_similarity(double distance, ObVectorDistanceType type)
  {
    if (distance < 0) {
      distance = 0;
    }
    
    switch (type) {
      case ObVectorDistanceType::L2_DISTANCE:
        // L2 距离转换为相似度：similarity = 1 / (1 + distance)
        return 1.0 / (1.0 + distance);
      
      case ObVectorDistanceType::COSINE_DISTANCE:
        // 余弦距离转换为相似度：similarity = (1 - distance) / 2
        // 假设 cosine_distance 范围为 [0, 2]
        return (1.0 - distance) / 2.0;
      
      case ObVectorDistanceType::INNER_PRODUCT:
        // 内积通常已经是相似度，但需要映射到 [0, 1] 范围
        // 这里假设已经标准化
        return distance > 1.0 ? 1.0 : (distance < 0.0 ? 0.0 : distance);
      
      default:
        return 0.0;
    }
  }
};

} // namespace common
} // namespace oceanbase

#endif // OB_HYBRID_SEARCH_COMMON_H
