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

int ObFTWord::hash(uint64_t &hash_val) const
{
  int ret = OB_SUCCESS;
  // get_basic_func() is a pure function of (obj type, collation type): it
  // only indexes the static EXPR_BASIC_* function tables. Every ObFTWord
  // inside one tokenize call carries the same (type, collation) (the map
  // key meta comes from a single column), so a thread-local last-lookup
  // memo skips the repeated table walk without changing which function
  // gets called. Thread-local storage keeps concurrent word maps on
  // distinct worker threads from sharing mutable state.
  struct ObFTWordHashFuncsMemo
  {
    ObObjType type_;
    ObCollationType coll_;
    common::ObDatumBasicFuncs *funcs_;
  };
  static thread_local ObFTWordHashFuncsMemo memo = {
      ObMaxType, CS_TYPE_INVALID, nullptr};
  common::ObDatumBasicFuncs *funcs = nullptr;
  if (memo.type_ == meta_.get_type() && memo.coll_ == meta_.get_collation_type()
      && OB_NOT_NULL(memo.funcs_)) {
    funcs = memo.funcs_;
  } else {
    funcs = ObDatumFuncs::get_basic_func(meta_.get_type(), meta_.get_collation_type());
    if (OB_NOT_NULL(funcs)) {
      memo.type_ = meta_.get_type();
      memo.coll_ = meta_.get_collation_type();
      memo.funcs_ = funcs;
    }
  }
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
  ObDatumCmpFuncType func = get_datum_cmp_func(meta_, other.meta_);
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
