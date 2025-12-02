#include "ob_rrf_fusion.h"
#include "lib/ob_errno.h"

namespace oceanbase
{
namespace common
{

ObRRFFusion::ObRRFFusion()
  : is_initialized_(false), allocator_(nullptr)
{
}

ObRRFFusion::~ObRRFFusion()
{
  reset();
}

int ObRRFFusion::init(const ObRRFConfig &config, ObIAllocator &allocator)
{
  int ret = OB_SUCCESS;
  
  if (is_initialized_) {
    ret = OB_INIT_TWICE;
    OB_LOG(WARN, "RRF fusion is already initialized", K(ret));
  } else if (OB_FAIL(validate_config())) {
    OB_LOG(WARN, "failed to validate rrf config", K(ret));
  } else {
    config_ = config;
    allocator_ = &allocator;
    
    // 初始化结果映射表
    if (OB_FAIL(result_map_.create(10240, &allocator))) {
      OB_LOG(WARN, "failed to create result map", K(ret));
    } else {
      is_initialized_ = true;
      OB_LOG(DEBUG, "RRF fusion initialized successfully", K(config_));
    }
  }
  
  return ret;
}

int ObRRFFusion::add_fts_results(const common::ObIArray<ObHybridSearchResult> &fts_results)
{
  int ret = OB_SUCCESS;
  
  if (!is_initialized_) {
    ret = OB_NOT_INIT;
    OB_LOG(WARN, "RRF fusion is not initialized", K(ret));
  } else if (OB_FAIL(fts_results_.assign(fts_results))) {
    OB_LOG(WARN, "failed to assign fts results", K(ret));
  } else {
    OB_LOG(DEBUG, "add fts results successfully", "count", fts_results_.count());
  }
  
  return ret;
}

int ObRRFFusion::add_vector_results(const common::ObIArray<ObHybridSearchResult> &vector_results)
{
  int ret = OB_SUCCESS;
  
  if (!is_initialized_) {
    ret = OB_NOT_INIT;
    OB_LOG(WARN, "RRF fusion is not initialized", K(ret));
  } else if (OB_FAIL(vector_results_.assign(vector_results))) {
    OB_LOG(WARN, "failed to assign vector results", K(ret));
  } else {
    OB_LOG(DEBUG, "add vector results successfully", "count", vector_results_.count());
  }
  
  return ret;
}

double ObRRFFusion::calculate_rrf_score(int64_t rank) const
{
  // RRF 公式：score = 1 / (rank + rank_constant)
  // rank 从 1 开始计数
  if (rank <= 0) {
    return 0.0;
  }
  return 1.0 / (rank + config_.rank_constant_);
}

int ObRRFFusion::fuse()
{
  int ret = OB_SUCCESS;
  
  if (!is_initialized_) {
    ret = OB_NOT_INIT;
    OB_LOG(WARN, "RRF fusion is not initialized", K(ret));
    return ret;
  }
  
  // 清空融合结果和映射表
  fused_results_.clear();
  result_map_.clear();
  
  // 处理全文搜索结果
  for (int64_t i = 0; OB_SUCC(ret) && i < fts_results_.count(); ++i) {
    const ObHybridSearchResult &result = fts_results_.at(i);
    int64_t rank = i + 1;  // 排名从 1 开始
    
    ObHybridSearchResult merged_result = result;
    merged_result.fts_rank_ = rank;
    merged_result.fts_score_ = calculate_rrf_score(rank);
    merged_result.source_flag_ |= 1;  // 标记来自全文搜索
    
    if (OB_FAIL(result_map_.set_refactored(result.doc_id_, merged_result))) {
      OB_LOG(WARN, "failed to insert fts result into map", K(ret), K(result));
    }
  }
  
  // 处理向量搜索结果
  for (int64_t i = 0; OB_SUCC(ret) && i < vector_results_.count(); ++i) {
    const ObHybridSearchResult &result = vector_results_.at(i);
    int64_t rank = i + 1;  // 排名从 1 开始
    
    ObHybridSearchResult merged_result = result;
    merged_result.vector_rank_ = rank;
    merged_result.vector_score_ = calculate_rrf_score(rank);
    merged_result.source_flag_ |= 2;  // 标记来自向量搜索
    
    ObHybridSearchResult *existing = nullptr;
    if (OB_FAIL(result_map_.get_refactored(result.doc_id_, existing))) {
      if (OB_HASH_NOT_EXIST == ret) {
        // 新文档，直接插入
        ret = result_map_.set_refactored(result.doc_id_, merged_result);
        if (OB_FAIL(ret)) {
          OB_LOG(WARN, "failed to insert vector result into map", K(ret), K(result));
        }
      } else {
        OB_LOG(WARN, "failed to get result from map", K(ret));
      }
    } else {
      // 文档已存在，更新分数和排名
      existing->vector_rank_ = rank;
      existing->vector_score_ = calculate_rrf_score(rank);
      existing->source_flag_ |= 2;  // 添加向量搜索标记
      
      if (OB_FAIL(result_map_.set_refactored(result.doc_id_, *existing))) {
        OB_LOG(WARN, "failed to update result in map", K(ret));
      }
    }
  }
  
  // 从映射表中提取结果并计算最终得分
  for (ResultMap::iterator iter = result_map_.begin(); OB_SUCC(ret) && iter != result_map_.end(); ++iter) {
    ObHybridSearchResult result = iter->second;
    
    // 计算最终得分（两个分数的和）
    result.final_score_ = result.fts_score_ + result.vector_score_;
    
    if (OB_FAIL(fused_results_.push_back(result))) {
      OB_LOG(WARN, "failed to push back fused result", K(ret));
    }
  }
  
  // 按最终得分降序排序
  if (OB_SUCC(ret)) {
    std::sort(fused_results_.begin(), fused_results_.end(),
              [](const ObHybridSearchResult &a, const ObHybridSearchResult &b) {
                if (a.final_score_ != b.final_score_) {
                  return a.final_score_ > b.final_score_;
                }
                return a.doc_id_ < b.doc_id_;
              });
    
    OB_LOG(DEBUG, "RRF fusion completed successfully",
           "fused_count", fused_results_.count(),
           "fts_count", fts_results_.count(),
           "vector_count", vector_results_.count());
  }
  
  return ret;
}

int ObRRFFusion::get_results(common::ObIArray<ObHybridSearchResult> &results, int64_t limit) const
{
  int ret = OB_SUCCESS;
  
  if (!is_initialized_) {
    ret = OB_NOT_INIT;
    OB_LOG(WARN, "RRF fusion is not initialized", K(ret));
    return ret;
  }
  
  int64_t count = fused_results_.count();
  if (limit > 0 && limit < count) {
    count = limit;
  }
  
  for (int64_t i = 0; OB_SUCC(ret) && i < count; ++i) {
    if (OB_FAIL(results.push_back(fused_results_.at(i)))) {
      OB_LOG(WARN, "failed to push back result", K(ret));
    }
  }
  
  return ret;
}

const ObHybridSearchResult *ObRRFFusion::get_result_at(int64_t index) const
{
  if (index < 0 || index >= fused_results_.count()) {
    return nullptr;
  }
  return &fused_results_.at(index);
}

void ObRRFFusion::reset()
{
  fts_results_.clear();
  vector_results_.clear();
  fused_results_.clear();
  if (nullptr != allocator_) {
    result_map_.clear();
    result_map_.destroy();
  }
  is_initialized_ = false;
  allocator_ = nullptr;
}

int ObRRFFusion::validate_config() const
{
  int ret = OB_SUCCESS;
  
  if (config_.rank_constant_ < 0) {
    ret = OB_INVALID_ARGUMENT;
    OB_LOG(WARN, "rank constant must be non-negative", K(ret), K(config_.rank_constant_));
  } else if (config_.rank_window_size_ <= 0) {
    ret = OB_INVALID_ARGUMENT;
    OB_LOG(WARN, "rank window size must be positive", K(ret), K(config_.rank_window_size_));
  }
  
  return ret;
}

} // namespace common
} // namespace oceanbase
