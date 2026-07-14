/*
 * Copyright (c) 2025 OceanBase.
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

#ifndef OCEANBASE_STORAGE_FTS_DICT_OB_FT_USER_DICT_H_
#define OCEANBASE_STORAGE_FTS_DICT_OB_FT_USER_DICT_H_

#include "lib/allocator/page_arena.h"
#include "lib/atomic/ob_atomic.h"
#include "lib/hash/ob_hashmap.h"
#include "lib/lock/ob_bucket_lock.h"
#include "lib/string/ob_sql_string.h"
#include "storage/fts/dict/ob_ft_dict.h"

namespace oceanbase
{
namespace storage
{

template <typename DataType> class ObFTDATReader;

class ObFTUserDict final : public ObIFTDict
{
public:
  ObFTUserDict();
  ~ObFTUserDict() override;

  int build(const common::ObString &database_name,
            const common::ObString &table_name,
            const uint64_t table_id);
  int init() override { return OB_SUCCESS; }
  int match(const common::ObString &single_word, ObDATrieHit &hit) const override;
  int match(const common::ObString &words, bool &is_match) const override;
  int match_with_hit(const common::ObString &single_word,
                     const ObDATrieHit &last_hit,
                     ObDATrieHit &hit) const override;

  int64_t inc_ref() { return ATOMIC_AAF(&ref_count_, 1); }
  int64_t dec_ref() { return ATOMIC_SAF(&ref_count_, 1); }
  uint64_t get_table_id() const { return table_id_; }
  int64_t get_word_count() const { return word_count_; }

private:
  int build_query(const common::ObString &database_name,
                  const common::ObString &table_name,
                  common::ObSqlString &sql) const;

private:
  common::ObArenaAllocator allocator_;
  ObFTDATReader<void> *reader_;
  int64_t ref_count_;
  uint64_t table_id_;
  int64_t word_count_;
  DISALLOW_COPY_AND_ASSIGN(ObFTUserDict);
};

class ObFTUserDictHandle final
{
public:
  ObFTUserDictHandle() : dict_(nullptr) {}
  ObFTUserDictHandle(const ObFTUserDictHandle &other) : dict_(nullptr) { *this = other; }
  ~ObFTUserDictHandle() { reset(); }
  ObFTUserDictHandle &operator=(const ObFTUserDictHandle &other);

  int set_dict(ObFTUserDict *dict);
  void reset();
  bool is_valid() const { return nullptr != dict_; }
  ObIFTDict *get_dict() const { return dict_; }

private:
  ObFTUserDict *dict_;
};

class ObFTUserDictManager final
{
public:
  ObFTUserDictManager() : is_inited_(false), dict_map_(), dict_lock_() {}
  ~ObFTUserDictManager() { destroy(); }

  int init();
  void destroy();
  int get_dict(const common::ObString &full_table_name, ObFTUserDictHandle &handle);
  int refresh(const common::ObString &full_table_name);
  int refresh(const common::ObString &database_name,
              const common::ObString &table_name,
              const uint64_t table_id);

private:
  int resolve_table(const common::ObString &full_table_name,
                    common::ObString &database_name,
                    common::ObString &table_name,
                    uint64_t &table_id) const;
  int build_dict(const common::ObString &database_name,
                 const common::ObString &table_name,
                 const uint64_t table_id,
                 ObFTUserDict *&dict) const;
  static void release_dict(ObFTUserDict *dict);

private:
  bool is_inited_;
  common::hash::ObHashMap<uint64_t, ObFTUserDict *> dict_map_;
  common::ObBucketLock dict_lock_;
  DISALLOW_COPY_AND_ASSIGN(ObFTUserDictManager);
};

} // namespace storage
} // namespace oceanbase

#endif // OCEANBASE_STORAGE_FTS_DICT_OB_FT_USER_DICT_H_
