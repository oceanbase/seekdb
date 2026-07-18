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

int ObFTWord::init_funcs_() const
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(hash_func_) || OB_ISNULL(cmp_func_)) {
    sql::ObExprBasicFuncs *funcs = ObDatumFuncs::get_basic_func(meta_.get_type(),
                                                                meta_.get_collation_type());
    if (OB_ISNULL(funcs) || OB_ISNULL(funcs->default_hash_)) {
      ret = OB_ERR_UNEXPECTED;
    } else if (OB_ISNULL(cmp_func_ = get_datum_cmp_func(meta_, meta_))) {
      ret = OB_ERR_UNEXPECTED;
    } else {
      hash_func_ = funcs->default_hash_;
    }
  }
  return ret;
}

int ObFTWord::hash(uint64_t &hash_val) const
{
  int ret = OB_SUCCESS;
  if (hash_valid_) {
    hash_val = hash_value_;
  } else if (OB_FAIL(init_funcs_())) {
  } else if (OB_FAIL(hash_func_(word_, 0, hash_value_))) {
  } else {
    hash_valid_ = true;
    hash_val = hash_value_;
  }
  return ret;
}
bool ObFTWord::operator==(const ObFTWord &other) const
{
  bool is_equal = false;
  int ret = OB_SUCCESS;
  int cmp_ret = 0;
  ObDatumCmpFuncType func = nullptr;
  if (this == &other) {
    is_equal = true;
  } else if (meta_ == other.meta_) {
    if (OB_FAIL(init_funcs_())) {
      ob_abort();
    } else {
      func = cmp_func_;
    }
  } else {
    func = get_datum_cmp_func(meta_, other.meta_);
  }
  if (is_equal || OB_FAIL(ret)) {
  } else if (func == nullptr) {
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
