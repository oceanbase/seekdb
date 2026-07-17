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

int ObFTToken::init(const char *ptr, const int64_t length, const ObObjMeta &meta,
                    const common::ObDatumHashFuncType hash_func, const ObDatumCmpFuncType cmp_func)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(ptr) || length <= 0) {
    ret = OB_INVALID_ARGUMENT;
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
  if (!is_calc_hash_val_) {
    common::ObDatumHashFuncType hash_func = hash_func_;
    if (OB_ISNULL(hash_func)) {
      sql::ObExprBasicFuncs *funcs = ObDatumFuncs::get_basic_func(meta_.get_type(), meta_.get_collation_type());
      if (OB_ISNULL(funcs) || OB_ISNULL(funcs->default_hash_)) {
        ret = OB_ERR_UNEXPECTED;
      } else {
        hash_func = funcs->default_hash_;
      }
    }
    if (OB_SUCC(ret) && OB_FAIL(hash_func(token_, 0, hash_val_))) {
    } else if (OB_SUCC(ret)) {
      is_calc_hash_val_ = true;
    }
  }
  if (OB_SUCC(ret)) {
    hash_val = hash_val_;
  }
  return ret;
}

bool ObFTToken::operator==(const ObFTToken &other) const
{
  bool equal = false;
  int cmp_ret = 0;
  ObDatumCmpFuncType cmp_func = OB_NOT_NULL(cmp_func_)
      ? cmp_func_ : get_datum_cmp_func(meta_, other.meta_);
  if (OB_NOT_NULL(cmp_func) && OB_SUCCESS == cmp_func(token_, other.token_, cmp_ret)) {
    equal = (0 == cmp_ret);
  }
  return equal;
}

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
} // namespace storage
} // namespace oceanbase
