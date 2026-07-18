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

int ObFTWord::hash(uint64_t &hash_val) const
{
  int ret = OB_SUCCESS;
  // FTS next-stage optimization (Op5): a token is commonly probed more than
  // once (stop-word set, get/update). Reuse its first successful hash.
  if (OB_LIKELY(is_hash_cached_)) {
    hash_val = hash_val_;
  } else {
    sql::ObExprBasicFuncs *funcs =
        ObDatumFuncs::get_basic_func(meta_.get_type(), meta_.get_collation_type());
    if (OB_ISNULL(funcs) || OB_ISNULL(funcs->default_hash_)) {
      ret = OB_ERR_UNEXPECTED;
    } else if (OB_FAIL(funcs->default_hash_(word_, 0, hash_val_))) {
      // Do not cache a failed calculation.
    } else {
      is_hash_cached_ = true;
      hash_val = hash_val_;
    }
  }
  return ret;
}
bool ObFTWord::operator==(const ObFTWord &other) const
{
  bool is_equal = false;
  int ret = OB_SUCCESS;
  int cmp_ret = 0;
  // FTS next-stage optimization (Op5): unequal full hashes cannot represent
  // equal keys, so avoid an expensive collation comparison in that case.
  if (OB_LIKELY(is_hash_cached_ && other.is_hash_cached_) && hash_val_ != other.hash_val_) {
    // Keep is_equal false.
  } else {
    ObDatumCmpFuncType func = get_datum_cmp_func(meta_, other.meta_);
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
