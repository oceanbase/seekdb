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

#ifndef OCEANBASE_STORAGE_FTS_OB_FTS_STOP_TOKEN_CHECK_H_
#define OCEANBASE_STORAGE_FTS_OB_FTS_STOP_TOKEN_CHECK_H_

#include "lib/hash/ob_hashmap.h"
#include "lib/hash/ob_hashset.h"
#include "lib/lock/ob_tc_rwlock.h"
#include "object/ob_object.h"
#include "storage/fts/ob_fts_struct.h"

namespace oceanbase
{
namespace storage
{

static constexpr int64_t FTS_STOP_TOKEN_MAX_LENGTH = 10;
static const char OB_STOP_TOKEN_TABLE_UTF8[][FTS_STOP_TOKEN_MAX_LENGTH] = {
  u8"a", u8"about", u8"an", u8"are", u8"as", u8"at", u8"be", u8"by", u8"com",
  u8"de", u8"en", u8"for", u8"from", u8"how", u8"i", u8"in", u8"is", u8"it",
  u8"la", u8"of", u8"on", u8"or", u8"that", u8"the", u8"this", u8"to", u8"was",
  u8"what", u8"when", u8"where", u8"who", u8"will", u8"with", u8"und", u8"www"
};

// 每张表完成构建后只读，NoPthreadDefendMode 让逐 token 查询不进入容器锁。
typedef common::hash::ObHashSet<
    ObFTToken,
    common::hash::NoPthreadDefendMode,
    common::hash::hash_func<ObFTToken>,
    common::hash::equal_to<ObFTToken>,
    common::hash::SimpleAllocer<
        typename common::hash::HashSetTypes<ObFTToken>::AllocType,
        common::hash::NodeNumTraits<
            typename common::hash::HashSetTypes<ObFTToken>::AllocType>::NODE_NUM,
        common::hash::NoPthreadDefendMode>> ObStopTokenTable;

// checker 是进程级只读表的非拥有视图；其生命周期不能超过 ObFTParsePluginData。
class ObStopTokenChecker final
{
public:
  ObStopTokenChecker()
      : is_inited_(false), collation_type_(CS_TYPE_INVALID), stop_token_hash_table_(nullptr)
  {}
  ~ObStopTokenChecker() { reset(); }

  int init(const ObCollationType coll, ObStopTokenTable *stop_token_hash_table);
  void reset()
  {
    is_inited_ = false;
    collation_type_ = CS_TYPE_INVALID;
    stop_token_hash_table_ = nullptr;
  }

  // 表发布后不再修改，因此该读取路径无锁；token 已缓存 hash 时不会重新计算。
  int check_is_stop_token(const ObFTToken &token, bool &is_stop_token) const;

private:
  bool is_inited_;
  ObCollationType collation_type_;
  ObStopTokenTable *stop_token_hash_table_;
};

// 按 collation 延迟构建并发布停止词表；写锁只覆盖首次生成，checker 查询本身不持锁。
class ObStopTokenCheckerGen final
{
public:
  ObStopTokenCheckerGen()
      : is_inited_(false),
        allocator_(),
        // seekdb 单机版尚未引入上游 Task 7 的专用 latch id；默认自旋读写锁保持相同的发布同步语义。
        lock_(),
        stop_token_hash_tables_()
  {}
  ~ObStopTokenCheckerGen() { reset(); }

  int init();
  void reset();
  int get_stop_token_checker_by_coll(const ObCollationType collation_type,
                                     ObStopTokenChecker &stop_token_checker);

private:
  int generate_stop_token_hash_table_by_coll(const ObCollationType coll);
  int convert_charset(const ObString &src_string,
                      const ObCollationType from_collation,
                      const ObCollationType to_collation,
                      ObString &converted_string);

private:
  static constexpr int64_t DEFAULT_STOP_TOKEN_NUMBERS = 36;
  static constexpr int64_t DEFAULT_STOP_TOKEN_TABLE_CAPACITY =
      static_cast<int64_t>(1) << (64 - __builtin_clzll(DEFAULT_STOP_TOKEN_NUMBERS));

  typedef common::hash::ObHashMap<
      ObCollationType,
      ObStopTokenTable *,
      common::hash::NoPthreadDefendMode,
      common::hash::hash_func<int64_t>,
      common::hash::equal_to<ObCollationType>,
      common::hash::SimpleAllocer<
          typename common::hash::HashMapTypes<ObCollationType, ObStopTokenTable *>::AllocType,
          common::hash::NodeNumTraits<
              typename common::hash::HashMapTypes<ObCollationType, ObStopTokenTable *>::AllocType>::NODE_NUM,
          common::hash::NoPthreadDefendMode>> StopTokenHashMap;

private:
  bool is_inited_;
  common::ObArenaAllocator allocator_;
  common::TCRWLock lock_;
  StopTokenHashMap stop_token_hash_tables_;

  static_assert(sizeof(OB_STOP_TOKEN_TABLE_UTF8) / sizeof(OB_STOP_TOKEN_TABLE_UTF8[0])
                    <= DEFAULT_STOP_TOKEN_NUMBERS,
                "too many builtin stop tokens");
};

} // namespace storage
} // namespace oceanbase

#endif // OCEANBASE_STORAGE_FTS_OB_FTS_STOP_TOKEN_CHECK_H_
