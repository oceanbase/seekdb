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

#define USING_LOG_PREFIX STORAGE_FTS

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
  sql::ObExprBasicFuncs *funcs = ObDatumFuncs::get_basic_func(meta_.get_type(), meta_.get_collation_type());
  if (OB_ISNULL(funcs)) {
    ret = OB_ERR_UNEXPECTED;
  } else if (funcs->default_hash_ == nullptr) {
    ret = OB_ERR_UNEXPECTED;
  } else {
    ret = funcs->default_hash_(word_, 0, hash_val);
  }
  return ret;
}
bool ObFTWord::operator==(const ObFTWord &other) const
{
  bool is_equal = false;
  int ret = OB_SUCCESS;
  int cmp_ret = 0;
  ObDatumCmpFuncType func = get_datum_cmp_func(meta_, other.meta_);
  if (func == nullptr) {
    ob_abort();
  } else if (OB_FAIL(func(word_, other.word_, cmp_ret))) {
    ob_abort();
  } else {
    is_equal = (cmp_ret == 0);
  }
  return is_equal;
}

int ObFTToken::init(
    const char *ptr,
    const int64_t length,
    const ObObjMeta &meta,
    const ObDatumHashFuncType hash_func,
    const ObDatumCmpFuncType cmp_func)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(ptr) || OB_UNLIKELY(length <= 0)
      || OB_ISNULL(hash_func) || OB_ISNULL(cmp_func)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid fulltext token arguments", K(ret), KP(ptr), K(length),
        K(meta), KP(hash_func), KP(cmp_func));
  } else {
    token_.set_string(ptr, length);
    meta_ = meta;
    hash_func_ = hash_func;
    cmp_func_ = cmp_func;
    is_calc_hash_val_ = false;
    hash_val_ = 0;
  }
  return ret;
}

int ObFTToken::hash(uint64_t &hash_val) const
{
  int ret = OB_SUCCESS;
  if (is_calc_hash_val_) {
    hash_val = hash_val_;
  } else if (OB_ISNULL(hash_func_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("fulltext token hash function is null", K(ret), KPC(this));
  } else if (OB_FAIL(hash_func_(token_, 0, hash_val_))) {
    LOG_WARN("failed to calculate fulltext token hash", K(ret), KPC(this));
  } else {
    is_calc_hash_val_ = true;
    hash_val = hash_val_;
  }
  return ret;
}

bool ObFTToken::operator==(const ObFTToken &other) const
{
  bool is_equal = false;
  int ret = do_compare(other, is_equal);
  if (OB_FAIL(ret)) {
    LOG_WARN("failed to compare fulltext tokens", K(ret), KPC(this), K(other));
    ob_abort();
  }
  return is_equal;
}

int ObFTToken::do_compare(const ObFTToken &other, bool &is_equal) const
{
  int ret = OB_SUCCESS;
  int cmp_ret = 0;
  is_equal = false;
  if (is_calc_hash_val_ && other.is_calc_hash_val_ && hash_val_ != other.hash_val_) {
    // Different cached hashes cannot represent equal values.
  } else if (OB_ISNULL(cmp_func_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("fulltext token compare function is null", K(ret), KPC(this));
  } else if (OB_FAIL(cmp_func_(token_, other.token_, cmp_ret))) {
    LOG_WARN("failed to compare fulltext token datum", K(ret), KPC(this), K(other));
  } else {
    is_equal = (0 == cmp_ret);
  }
  return ret;
}
} // namespace storage
} // namespace oceanbase
