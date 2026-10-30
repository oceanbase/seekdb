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

#include <atomic>

#include "lib/charset/ob_charset.h"
#include "share/datum/ob_datum_funcs.h"
#include "storage/ob_storage_util.h"

namespace oceanbase
{
namespace storage
{

namespace {

// ObCharset::get_charset() and the argument validation in ObCharset::hash() were
// paid on every word hash lookup.  The collation handler is a process lifetime
// constant, so cache it once per collation.
inline const ObCharsetInfo *get_cached_collation(const ObCollationType cs_type)
{
  static std::atomic<const ObCharsetInfo *> cached[common::CS_TYPE_MAX];
  const ObCharsetInfo *cs = nullptr;
  const bool cacheable = cs_type > common::CS_TYPE_INVALID && cs_type < common::CS_TYPE_MAX;
  if (cacheable) {
    cs = cached[cs_type].load(std::memory_order_relaxed);
  }
  if (OB_ISNULL(cs)) {
    cs = ObCharset::get_charset(cs_type);
    if (cacheable && OB_NOT_NULL(cs)) {
      cached[cs_type].store(cs, std::memory_order_relaxed);
    }
  }
  return cs;
}

} // anonymous namespace

int ObFTWord::hash(uint64_t &hash_val) const
{
  int ret = OB_SUCCESS;
  const ObCollationType cs_type = meta_.get_collation_type();
  const ObString str = word_.get_string();
  const ObCharsetInfo *cs = get_cached_collation(cs_type);
  if (OB_ISNULL(cs) || OB_ISNULL(cs->coll) || OB_ISNULL(cs->coll->hash_sort)) {
    // unknown collation: keep the previous behaviour
    common::ObDatumBasicFuncs *funcs = ObDatumFuncs::get_basic_func(meta_.get_type(), cs_type);
    if (OB_ISNULL(funcs) || OB_ISNULL(funcs->default_hash_)) {
      ret = OB_ERR_UNEXPECTED;
    } else {
      ret = funcs->default_hash_(word_, 0, hash_val, nullptr);
    }
  } else if (str.empty()) {
    // matches ObCharset::hash(cs, str, seed=0, ...) for an empty string
    hash_val = 0;
  } else {
    // same seeds as ObCharset::hash(): input seed 0, random seed 0xc6a4a7935bd1e995
    ulong nr1 = 0;
    ulong nr2 = 0xc6a4a7935bd1e995;
    cs->coll->hash_sort(cs, reinterpret_cast<const unsigned char *>(str.ptr()),
                        str.length(), &nr1, &nr2, false, nullptr);
    hash_val = static_cast<uint64_t>(nr1);
  }
  return ret;
}

bool ObFTWord::operator==(const ObFTWord &other) const
{
  if (meta_.get_collation_type() == other.meta_.get_collation_type()) {
    const uint32_t l1 = word_.len_;
    const uint32_t l2 = other.word_.len_;
    if (l1 == l2 && (0 == l1 || 0 == MEMCMP(word_.ptr_, other.word_.ptr_, l1))) {
      return true;
    }
    const ObCollationType cs = meta_.get_collation_type();
    if ((CS_TYPE_UTF8MB4_GENERAL_CI == cs || CS_TYPE_UTF8MB4_BIN == cs || CS_TYPE_BINARY == cs)
        && (0 == l1 || ' ' != word_.ptr_[l1 - 1])
        && (0 == l2 || ' ' != other.word_.ptr_[l2 - 1])) {
      if (l1 != l2 || CS_TYPE_UTF8MB4_BIN == cs || CS_TYPE_BINARY == cs) {
        return false;
      }
    }
  }
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
