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

int ObFTWord::init(const char *ptr, const int64_t length, const ObObjMeta &meta)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(nullptr == ptr || length <= 0)) {
    ret = OB_INVALID_ARGUMENT;
  } else {
    sql::ObExprBasicFuncs *funcs =
        ObDatumFuncs::get_basic_func(meta.get_type(), meta.get_collation_type());
    ObDatumCmpFuncType cmp_func = get_datum_cmp_func(meta, meta);
    if (OB_UNLIKELY(nullptr == funcs || nullptr == funcs->default_hash_ || nullptr == cmp_func)) {
      ret = OB_ERR_UNEXPECTED;
    } else {
      word_.set_string(ptr, length);
      meta_ = meta;
      hash_func_ = funcs->default_hash_;
      cmp_func_ = cmp_func;
      hash_calculated_ = false;
      hash_value_ = 0;
    }
  }
  return ret;
}

int ObFTWord::hash(uint64_t &hash_val) const
{
  int ret = OB_SUCCESS;
  if (OB_LIKELY(hash_calculated_)) {
    hash_val = hash_value_;
  } else if (OB_UNLIKELY(nullptr == hash_func_)) {
    ret = OB_ERR_UNEXPECTED;
  } else {
    if (OB_FAIL(hash_func_(word_, 0, hash_value_))) {
    } else {
      hash_calculated_ = true;
      hash_val = hash_value_;
    }
  }
  return ret;
}

bool ObFTWord::operator==(const ObFTWord &other) const
{
  int ret = OB_SUCCESS;
  bool is_equal = false;
  if (OB_FAIL(compare_(other, is_equal))) {
    ob_abort();
  }
  return is_equal;
}

int ObFTWord::compare_(const ObFTWord &other, bool &is_equal) const
{
  int ret = OB_SUCCESS;
  int cmp_ret = 0;
  is_equal = false;
  if (hash_calculated_ && other.hash_calculated_ && hash_value_ != other.hash_value_) {
  } else if (OB_UNLIKELY(nullptr == cmp_func_)) {
    ret = OB_ERR_UNEXPECTED;
  } else if (OB_FAIL(cmp_func_(word_, other.word_, cmp_ret))) {
  } else {
    is_equal = (cmp_ret == 0);
  }
  return ret;
}
} // namespace storage
} // namespace oceanbase
