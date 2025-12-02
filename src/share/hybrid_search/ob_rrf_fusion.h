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
 * RRF (Reciprocal Rank Fusion) 融合实现
 * 
 * 基本原理：
 * RRF 是一种无参数融合算法，通过将多个排序列表转换为得分，
 * 并将这些得分相加来生成混合排名。
 * 
 * 公式：
 * score = 1/(rank + rank_constant) 对于每个搜索引擎
 * final_score = score_from_fts + score_from_vector
 * 
 * 优点：
 * 1. 自动规范化：通过排名自然解决不同评分系统的规范化问题
 * 2. 鲁棒性强：对异常值不敏感
 * 3. 参数简单：只需要配置 rank_constant 一个参数
 * 4. 性能优异：不需要额外的规范化计算
 * 
 * 应用场景：
 * - 需要平衡关键字匹配和语义相似度的搜索
 * - 对异常得分值鲁棒的应用
 * - 中等规模数据集（通常 rank_window_size = 100-1000）
 */
class ObRRFFusion
{
public:
  typedef common::hash::ObHashMap<uint64_t, ObHybridSearchResult> ResultMap;
  
  ObRRFFusion();
  virtual ~ObRRFFusion();
  
  /*
   * 初始化 RRF 融合器
   * 
   * @param config RRF 配置参数
   * @param allocator 内存分配器
   * @return 成功返回 OB_SUCCESS，失败返回相应错误码
   */
  int init(const ObRRFConfig &config, ObIAllocator &allocator);
  
  /*
   * 添加全文搜索结果
   * 
   * @param fts_results 全文搜索结果列表，按相关性降序排列
   * @return 成功返回 OB_SUCCESS，失败返回相应错误码
   */
  int add_fts_results(const common::ObIArray<ObHybridSearchResult> &fts_results);
  
  /*
   * 添加向量搜索结果
   * 
   * @param vector_results 向量搜索结果列表，按相似度降序排列
   * @return 成功返回 OB_SUCCESS，失败返回相应错误码
   */
  int add_vector_results(const common::ObIArray<ObHybridSearchResult> &vector_results);
  
  /*
   * 执行 RRF 融合计算
   * 
   * 该方法会：
   * 1. 为两个结果列表中的每个结果分配排名
   * 2. 使用 RRF 公式计算规范化分数
   * 3. 合并两个列表的结果
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
   * 获取全文搜索结果数量
   */
  int64_t get_fts_result_count() const { return fts_results_.count(); }
  
  /*
   * 获取向量搜索结果数量
   */
  int64_t get_vector_result_count() const { return vector_results_.count(); }
  
  /*
   * 获取融合后的结果数量
   */
  int64_t get_fused_result_count() const { return fused_results_.count(); }
  
  /*
   * 获取单个融合后的结果
   * 
   * @param index 结果索引
   * @return 融合后的结果，如果索引越界返回空结果
   */
  const ObHybridSearchResult *get_result_at(int64_t index) const;
  
private:
  // 计算 RRF 分数
  double calculate_rrf_score(int64_t rank) const;
  
  // 验证配置参数
  int validate_config() const;
  
private:
  // RRF 配置参数
  ObRRFConfig config_;
  
  // 全文搜索结果
  common::ObSEArray<ObHybridSearchResult, 64> fts_results_;
  
  // 向量搜索结果
  common::ObSEArray<ObHybridSearchResult, 64> vector_results_;
  
  // 融合后的结果
  common::ObSEArray<ObHybridSearchResult, 64> fused_results_;
  
  // 记录已初始化状态
  bool is_initialized_;
  
  // 内存分配器（不拥有所有权）
  ObIAllocator *allocator_;
  
  // 用于去重的结果映射表
  ResultMap result_map_;
};

} // namespace common
} // namespace oceanbase

#endif // OB_RRF_FUSION_H
