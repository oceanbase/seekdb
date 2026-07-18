/*
 * Copyright (c) 2026 OceanBase.
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

#ifndef OCEANBASE_STORAGE_FTS_OB_FT_TOKEN_PROCESSOR_H_
#define OCEANBASE_STORAGE_FTS_OB_FT_TOKEN_PROCESSOR_H_

#include "object/ob_object.h"
#include "storage/fts/ob_fts_stop_token_check.h"
#include "storage/fts/ob_fts_struct.h"

namespace oceanbase
{
namespace storage
{

class ObFTParserProperty;

// 把长度过滤、停止词检查、hash 复用、词频/位置聚合收敛到单条短路流水线。
class ObFTTokenProcessor final
{
public:
  // scratch_allocator 仅服务当前文档的位置列表；外部必须先停止引用旧输出再执行 allocator reuse。
  explicit ObFTTokenProcessor(common::ObIAllocator &scratch_allocator)
      : is_inited_(false),
        token_meta_(),
        token_map_(nullptr),
        min_max_token_cnt_(0),
        non_stop_token_cnt_(0),
        stop_token_cnt_(0),
        min_token_size_(0),
        max_token_size_(0),
        flag_(),
        hash_func_(nullptr),
        cmp_func_(nullptr),
        stop_token_checker_(),
        scratch_allocator_(scratch_allocator)
  {}
  ~ObFTTokenProcessor() = default;

  // token_map 由调用方拥有；初始化时缓存 collation 函数指针和只读停止词 checker。
  int init(const ObFTParserProperty &property,
           const ObObjMeta &meta,
           const ObProcessTokenFlag &flag,
           ObFTTokenMap *token_map);
  void reset();
  // 文档间只重置统计量；调用方负责在复用前清空 token_map 和 scratch allocator。
  void reuse();
  int process_token(const bool need_pos_list,
                    const char *token,
                    const int64_t token_len,
                    const int64_t char_cnt,
                    const int64_t position);
  int64_t get_non_stop_token_count() const { return non_stop_token_cnt_; }

  VIRTUAL_TO_STRING_KV(K_(token_meta), K_(min_max_token_cnt), K_(non_stop_token_cnt),
      K_(stop_token_cnt), K_(min_token_size), K_(max_token_size), KP_(token_map));

private:
  static constexpr int64_t MAX_CHAR_COUNT_PER_TOKEN = 1024;

  class UpdateTokenCallBack final
  {
  public:
    UpdateTokenCallBack(common::ObIAllocator &allocator, const int64_t position)
        : allocator_(allocator), position_(position)
    {}
    int operator()(common::hash::HashMapPair<ObFTToken, ObFTTokenInfo> &pair);
  private:
    common::ObIAllocator &allocator_;
    int64_t position_;
  };

  class UpdateTokenWithoutPosListCallBack final
  {
  public:
    int operator()(common::hash::HashMapPair<ObFTToken, ObFTTokenInfo> &pair);
  };

  bool is_min_max_token_(const int64_t char_cnt) const;
  int groupby_token_(const bool need_pos_list,
                     const ObFTToken &token,
                     const int64_t position);

private:
  bool is_inited_;
  ObObjMeta token_meta_;
  ObFTTokenMap *token_map_;
  int64_t min_max_token_cnt_;
  int64_t non_stop_token_cnt_;
  int64_t stop_token_cnt_;
  int64_t min_token_size_;
  int64_t max_token_size_;
  ObProcessTokenFlag flag_;
  common::ObDatumHashFuncType hash_func_;
  ObDatumCmpFuncType cmp_func_;
  ObStopTokenChecker stop_token_checker_;
  common::ObIAllocator &scratch_allocator_;
};

} // namespace storage
} // namespace oceanbase

#endif // OCEANBASE_STORAGE_FTS_OB_FT_TOKEN_PROCESSOR_H_
