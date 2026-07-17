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

#ifndef OCEANBASE_STORAGE_FTS_OB_FTS_STRUCT_H_
#define OCEANBASE_STORAGE_FTS_OB_FTS_STRUCT_H_

#include "lib/charset/ob_charset.h"
#include "lib/container/ob_se_array.h"
#include "lib/hash/ob_hashmap.h"
#include "object/ob_object.h"
#include "share/datum/ob_datum_funcs.h"

namespace oceanbase
{
namespace storage
{

// 全文 token 的轻量视图，不拥有字符内存；调用方必须保证底层文档或 scratch 缓冲区有效。
// 初始化时缓存 collation 对应的 hash/compare 函数，首次 hash 后再缓存结果，避免热路径重复查表和计算。
class ObFTToken final
{
public:
  ObFTToken()
      : is_calc_hash_val_(false),
        hash_val_(0),
        hash_func_(nullptr),
        cmp_func_(nullptr),
        meta_(),
        token_()
  {}

  // 兼容 Task 2/Task 3 既有调用；新热路径应优先显式传入已缓存的函数指针。
  ObFTToken(const int64_t length, const char *ptr, const ObObjMeta &meta) : ObFTToken()
  {
    (void)init(ptr, length, meta, nullptr, nullptr);
  }

  ~ObFTToken() = default;

  // ptr 由调用方持有；hash_func/cmp_func 可为空，此时仅在首次使用时回退查询 datum 函数表。
  int init(const char *ptr,
           const int64_t length,
           const ObObjMeta &meta,
           const common::ObDatumHashFuncType hash_func,
           const ObDatumCmpFuncType cmp_func);

  const ObDatum &get_token() const { return token_; }
  // 兼容尚由后续 Task 迁移的旧调用路径，语义与 get_token 完全一致且不产生拷贝。
  const ObDatum &get_word() const { return token_; }
  ObCollationType get_collation_type() const { return meta_.get_collation_type(); }
  bool empty() const { return token_.get_string().empty(); }
  int hash(uint64_t &hash_val) const;
  bool operator==(const ObFTToken &other) const;
  bool operator!=(const ObFTToken &other) const { return !(other == *this); }

  TO_STRING_KV(K_(is_calc_hash_val), K_(hash_val), KP_(hash_func), KP_(cmp_func), K_(meta), K_(token));

private:
  int do_compare(const ObFTToken &other, bool &is_equal) const;

private:
  mutable bool is_calc_hash_val_;
  mutable uint64_t hash_val_;
  common::ObDatumHashFuncType hash_func_;
  ObDatumCmpFuncType cmp_func_;
  ObObjMeta meta_;
  ObDatum token_;
};

// 词频及可选位置列表；位置 holder 用单线程引用计数适配无锁 token map 的按值复制。
class ObFTTokenInfo final
{
public:
  ObFTTokenInfo()
      : allocator_(nullptr), count_(0), position_list_(nullptr), position_list_holder_(nullptr)
  {}
  ObFTTokenInfo(const ObFTTokenInfo &other);
  ObFTTokenInfo &operator=(const ObFTTokenInfo &other);
  ~ObFTTokenInfo();

  // 追加严格递增的位置；allocator 必须覆盖 token map 当前文档的生命周期。
  int update_one_position(common::ObIAllocator &allocator, const int64_t position);
  // 普通 MATCH 索引只累加词频，不分配位置数组，为 Task 4 的 phrase 分支保留兼容承接点。
  int update_without_pos_list();

  TO_STRING_KV(K_(count), KPC_(position_list), KP_(position_list_holder));

private:
  static constexpr int64_t INITIAL_POSITION_LIST_COUNT = 1;
  static constexpr int64_t MAX_POSITION_LIST_COUNT = 512;
  struct ObFTPositionListHolder
  {
    ObFTPositionListHolder() : ref_cnt_(1), position_list_() {}
    int32_t ref_cnt_;
    common::ObSEArray<int64_t, INITIAL_POSITION_LIST_COUNT> position_list_;
  };

  void retain_position_list_();
  void release_position_list_();

public:
  common::ObIAllocator *allocator_;
  int64_t count_;
  common::ObSEArray<int64_t, INITIAL_POSITION_LIST_COUNT> *position_list_;
  ObFTPositionListHolder *position_list_holder_;
};

typedef common::hash::HashMapPair<ObFTToken, ObFTTokenInfo> ObFTTokenPair;

// 单个 token processor 独占该 map；禁用内部锁，查询直接使用 token 预计算 hash。
typedef common::hash::ObHashMap<
    ObFTToken,
    ObFTTokenInfo,
    common::hash::NoPthreadDefendMode,
    common::hash::hash_func<ObFTToken>,
    common::hash::equal_to<ObFTToken>,
    common::hash::SimpleAllocer<
        typename common::hash::HashMapTypes<ObFTToken, ObFTTokenInfo>::AllocType,
        common::hash::NodeNumTraits<
            typename common::hash::HashMapTypes<ObFTToken, ObFTTokenInfo>::AllocType>::NODE_NUM,
        common::hash::NoPthreadDefendMode>> ObFTTokenMap;

// 处理开关统一描述 token 过滤、规范化与聚合步骤。
class ObProcessTokenFlag final
{
private:
  static constexpr uint64_t PTF_NONE = 0;
  static constexpr uint64_t PTF_MIN_MAX_TOKEN = 1ULL << 0;
  static constexpr uint64_t PTF_STOP_TOKEN = 1ULL << 1;
  static constexpr uint64_t PTF_CASEDOWN = 1ULL << 2;
  static constexpr uint64_t PTF_GROUPBY_TOKEN = 1ULL << 3;

public:
  ObProcessTokenFlag() : flag_(PTF_NONE) {}
  ~ObProcessTokenFlag() = default;
  void reset() { flag_ = PTF_NONE; }
  void set_flag(const uint64_t flag) { flag_ |= flag; }
  void set_min_max_token() { set_flag(PTF_MIN_MAX_TOKEN); }
  void set_stop_token() { set_flag(PTF_STOP_TOKEN); }
  void set_casedown_token() { set_flag(PTF_CASEDOWN); }
  void set_groupby_token() { set_flag(PTF_GROUPBY_TOKEN); }
  void clear() { reset(); }
  void clear_min_max_token() { clear_flag_(PTF_MIN_MAX_TOKEN); }
  void clear_stop_token() { clear_flag_(PTF_STOP_TOKEN); }
  void clear_casedown_token() { clear_flag_(PTF_CASEDOWN); }
  void clear_groupby_token() { clear_flag_(PTF_GROUPBY_TOKEN); }
  bool min_max_token() const { return has_flag_(PTF_MIN_MAX_TOKEN); }
  bool stop_token() const { return has_flag_(PTF_STOP_TOKEN); }
  bool casedown_token() const { return has_flag_(PTF_CASEDOWN); }
  bool groupby_token() const { return has_flag_(PTF_GROUPBY_TOKEN); }

  // 以下旧命名仅供尚属后续 Task 的调用点平滑编译，最终都落到同一位图，不复制状态。
  void set_min_max_word() { set_min_max_token(); }
  void set_stop_word() { set_stop_token(); }
  void set_casedown() { set_casedown_token(); }
  void set_groupby_word() { set_groupby_token(); }
  void clear_min_max_word() { clear_min_max_token(); }
  void clear_stop_word() { clear_stop_token(); }
  void clear_casedown() { clear_casedown_token(); }
  void clear_groupby_word() { clear_groupby_token(); }
  bool min_max_word() const { return min_max_token(); }
  bool stopword() const { return stop_token(); }
  bool casedown() const { return casedown_token(); }
  bool groupby_word() const { return groupby_token(); }

  TO_STRING_KV(K_(flag));

private:
  void clear_flag_(const uint64_t flag) { flag_ &= ~flag; }
  bool has_flag_(const uint64_t flag) const { return (flag_ & flag) == flag; }

private:
  uint64_t flag_;
};

// Task 2/Task 3 兼容别名：旧 token 视图仍受益于 hash 缓存；旧 map 暂保留 int64_t 词频布局。
typedef ObFTToken ObFTWord;
typedef common::hash::ObHashMap<ObFTWord, int64_t> ObFTWordMap;
typedef ObProcessTokenFlag ObAddWordFlag;

} // namespace storage
} // namespace oceanbase

#endif // OCEANBASE_STORAGE_FTS_OB_FTS_STRUCT_H_
