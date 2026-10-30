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

#include "data_plane/fts/ob_fts_struct.h"

#include "lib/charset/ob_charset.h"
#include "share/datum/ob_datum_funcs.h"
#include "storage/ob_storage_util.h"

namespace oceanbase
{
namespace storage
{

namespace
{
// ObFTWord is the key type of ObFTWordMap: every token of every document is
// hashed at least once in ObAddWord::groupby_word and compared on hash
// collisions. Both hash() and operator== used to re-resolve the collation
// function pointers through ObDatumFuncs::get_basic_func / get_datum_cmp_func on
// every single call, which walks charset dispatch tables per token. The resolved
// pointers are process-global tables, so caching them per thread is safe.
struct ObFTWordFuncCache
{
  common::ObObjMeta meta_;
  common::ObDatumBasicFuncs *basic_funcs_;
  ObDatumCmpFuncType cmp_func_;
  bool valid_;
  ObFTWordFuncCache()
    : meta_(), basic_funcs_(nullptr), cmp_func_(nullptr), valid_(false)
  {
  }
};

thread_local ObFTWordFuncCache tl_ft_word_func_cache;

inline bool ft_word_func_cache_hit(const common::ObObjMeta &meta)
{
  const ObFTWordFuncCache &cache = tl_ft_word_func_cache;
  return cache.valid_
         && cache.meta_.get_type() == meta.get_type()
         && cache.meta_.get_collation_type() == meta.get_collation_type();
}

inline void refresh_ft_word_func_cache(const common::ObObjMeta &meta)
{
  ObFTWordFuncCache &cache = tl_ft_word_func_cache;
  cache.meta_ = meta;
  cache.basic_funcs_ = common::ObDatumFuncs::get_basic_func(meta.get_type(), meta.get_collation_type());
  cache.cmp_func_ = get_datum_cmp_func(meta, meta);
  cache.valid_ = true;
}

} // namespace

int ObFTWord::hash(uint64_t &hash_val) const
{
  int ret = OB_SUCCESS;
  if (!ft_word_func_cache_hit(meta_)) {
    refresh_ft_word_func_cache(meta_);
  }
  common::ObDatumBasicFuncs *funcs = tl_ft_word_func_cache.basic_funcs_;
  if (OB_ISNULL(funcs)) {
    ret = OB_ERR_UNEXPECTED;
  } else if (funcs->default_hash_ == nullptr) {
    ret = OB_ERR_UNEXPECTED;
  } else {
    ret = funcs->default_hash_(word_, 0, hash_val, nullptr);
  }
  return ret;
}
bool ObFTWord::operator==(const ObFTWord &other) const
{
  bool is_equal = false;
  int ret = OB_SUCCESS;
  int cmp_ret = 0;
  ObDatumCmpFuncType func = nullptr;
  if (meta_.get_type() == other.meta_.get_type()
      && meta_.get_collation_type() == other.meta_.get_collation_type()) {
    if (!ft_word_func_cache_hit(meta_)) {
      refresh_ft_word_func_cache(meta_);
    }
    func = tl_ft_word_func_cache.cmp_func_;
  } else {
    func = get_datum_cmp_func(meta_, other.meta_);
  }
  if (func == nullptr) {
    ob_abort();
  } else if (OB_FAIL(func(word_, other.word_, cmp_ret, nullptr))) {
    ob_abort();
  } else {
    is_equal = (cmp_ret == 0);
  }
  return is_equal;
}
} // namespace storage
} // namespace oceanbase
