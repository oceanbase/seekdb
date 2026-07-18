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

#include "storage/fts/ob_fts_struct.h"

#include "lib/charset/ob_charset.h"
#include "share/datum/ob_datum_funcs.h"
#include "storage/ob_storage_util.h"

namespace oceanbase
{
namespace storage
{

void ObFTWord::init_cached_funcs()
{
  if (OB_LIKELY(!meta_.is_invalid())) {
    sql::ObExprBasicFuncs *funcs = ObDatumFuncs::get_basic_func(meta_.get_type(), meta_.get_collation_type());
    if (OB_NOT_NULL(funcs)) {
      hash_func_ = funcs->default_hash_;
      cmp_func_ = funcs->null_first_cmp_;
      if (OB_LIKELY(nullptr != hash_func_)) {
        hash_cached_ = (OB_SUCCESS == hash_func_(word_, 0, hash_val_));
      }
    }
  }
}

int ObFTWord::hash(uint64_t &hash_val) const
{
  int ret = OB_SUCCESS;
  if (OB_LIKELY(hash_cached_)) {
    hash_val = hash_val_;
  } else if (OB_LIKELY(nullptr != hash_func_)) {
    ret = hash_func_(word_, 0, hash_val);
  } else {
    sql::ObExprBasicFuncs *funcs = ObDatumFuncs::get_basic_func(meta_.get_type(), meta_.get_collation_type());
    if (OB_ISNULL(funcs)) {
      ret = OB_ERR_UNEXPECTED;
    } else if (funcs->default_hash_ == nullptr) {
      ret = OB_ERR_UNEXPECTED;
    } else {
      ret = funcs->default_hash_(word_, 0, hash_val);
    }
  }
  return ret;
}
bool ObFTWord::operator==(const ObFTWord &other) const
{
  bool is_equal = false;
  int ret = OB_SUCCESS;
  int cmp_ret = 0;
  // Fast negative path: unequal hash values imply unequal words.  The cached
  // hash is computed with seed 0, exactly what ObHashMap uses for keys.
  if (OB_LIKELY(hash_cached_ && other.hash_cached_ && hash_val_ != other.hash_val_)) {
    is_equal = false;
  } else {
    ObDatumCmpFuncType func = nullptr;
    if (OB_LIKELY(meta_.get_type() == other.meta_.get_type()
                  && meta_.get_collation_type() == other.meta_.get_collation_type()
                  && nullptr != cmp_func_)) {
      func = cmp_func_;
    } else {
      func = get_datum_cmp_func(meta_, other.meta_);
    }
    if (func == nullptr) {
      ob_abort();
    } else if (OB_FAIL(func(word_, other.word_, cmp_ret))) {
      ob_abort();
    } else {
      is_equal = (cmp_ret == 0);
    }
  }
  return is_equal;
}
} // namespace storage
} // namespace oceanbase
