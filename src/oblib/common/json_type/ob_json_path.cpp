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

#define USING_LOG_PREFIX SQL_RESV
#include "ob_json_path.h"
#include "ob_json_base.h"
#include "rust/json.h"
#include "lib/charset/ob_charset.h"

namespace oceanbase {
namespace common {

ObJsonPath::ObJsonPath(common::ObIAllocator *allocator)
    : heap_expr_(allocator), last_node_(JPN_ROOT), can_match_many_(false)
{}

ObJsonPath::ObJsonPath(const ObString &path, common::ObIAllocator *allocator)
    : heap_expr_(allocator), last_node_(JPN_ROOT), can_match_many_(false)
{
  if (heap_expr_.append(path) == OB_SUCCESS) {
    expression_.assign_ptr(heap_expr_.ptr(), heap_expr_.length());
  } else {
    expression_ = path;
  }
}

int ObJsonPath::parse_path()
{
  const json::PathMetadataResult result = json::inspect_path(rust::Slice<const uint8_t>(
      reinterpret_cast<const uint8_t *>(expression_.ptr()), expression_.length()), true);
  int ret = OB_SUCCESS;
  if (result.status != json::Status::Ok) {
    ret = result.too_many_nodes ? OB_ERR_DATA_TOO_LONG : OB_INVALID_ARGUMENT;
  } else {
    last_node_ = static_cast<ObJsonPathNodeType>(result.last_node);
    can_match_many_ = result.can_match_many;
  }
  return ret;
}

bool ObJsonPath::path_not_str() const
{
  return is_last_func() && last_node_ != JPN_LOWER && last_node_ != JPN_UPPER
      && last_node_ != JPN_STRING && last_node_ != JPN_STR_ONLY && last_node_ != JPN_TYPE;
}

bool ObJsonPathCache::is_match(ObString& path_str, size_t idx)
{
  bool result = false;
  if (idx < size()) {
    ObJsonPath* path = path_at(idx);
    if (OB_NOT_NULL(path)) {
      result = ObJsonPathUtil::string_cmp_skip_charactor(path_str, path->get_path_string(), ' ') == 0;
    }
  }
  return result;
}

int ObJsonPathCache::find_and_add_cache(ObJsonPath*& res_path, ObString& path_str, int arg_idx, bool is_const)
{
  INIT_SUCC(ret);
  if (!((is_const && arg_idx < size()) || is_match(path_str, arg_idx))) {
    void* buf = allocator_->alloc(sizeof(ObJsonPath));
    if (OB_NOT_NULL(buf)) {
      ObJsonPath* path = new (buf) ObJsonPath(path_str, allocator_);
      if (OB_FAIL(path->parse_path())) {
      } else {
        ret = set_path(path, path_str.length() == 0 ? OK_NULL : OK_NOT_NULL, arg_idx, arg_idx);
        res_path = path;
      }
    } else {
      ret = OB_ALLOCATE_MEMORY_FAILED;
    }
  } else {
    res_path = path_at(arg_idx);
  }
  return ret;
}

int ObJsonPathCache::fill_empty(size_t reserve_size)
{
  INIT_SUCC(ret);
  if (reserve_size > path_arr_ptr_.size()) {
    // fill element in vector
    if (OB_FAIL(path_arr_ptr_.reserve(reserve_size))) {
    } else if (OB_FAIL(stat_arr_.reserve(reserve_size))) {
    } else if (path_arr_ptr_.size() != stat_arr_.size()) {
    }
    for (size_t cur = path_arr_ptr_.size(); OB_SUCC(ret) && cur < reserve_size; ++cur) {
      if (OB_FAIL(path_arr_ptr_.push_back(NULL))) {
      } else if (OB_FAIL(stat_arr_.push_back(ObPathCacheStat()))) {
      }
    }
  }
  return ret;
}

ObPathParseStat ObJsonPathCache::path_stat_at(size_t idx)
{
  ObPathParseStat stat = UNINITIALIZED;
  if (idx < stat_arr_.size()) {
    stat = stat_arr_[idx].state_;
  }
  return stat;
}

ObJsonPath* ObJsonPathCache::path_at(size_t idx)
{
  ObJsonPath *ptr = nullptr;
  if (idx < path_arr_ptr_.size()) {
    ptr = path_arr_ptr_[idx];
  }
  return ptr;
}

void ObJsonPathCache::reset()
{
  stat_arr_.clear();
  path_arr_ptr_.clear();
}

size_t ObJsonPathCache::size()
{
  return path_arr_ptr_.size();
}

int ObJsonPathCache::set_path(ObJsonPath* path, ObPathParseStat stat, int arg_idx, int index)
{
  INIT_SUCC(ret);
  if (OB_FAIL(fill_empty(arg_idx + 1))) {
  } else if (index >= path_arr_ptr_.size()) {
    ret = OB_ERROR_OUT_OF_RANGE;
  } else {
    path_arr_ptr_[index] = path;
    stat_arr_[index] = ObPathCacheStat(stat, arg_idx);
  }
  return ret;
}


common::ObIAllocator* ObJsonPathCache::get_allocator()
{
  return allocator_;
}

bool ObJsonPathUtil::is_whitespace(char ch)
{
  return (ch == ' ' || ch == '\t' || ch == '\n');
}

void ObJsonPathUtil::skip_whitespace(const ObString &path, uint64_t& idx)
{
  while (idx < path.length() && ObJsonPathUtil::is_whitespace(path[idx])) {
    idx++;
  }
}

static constexpr unsigned UNICODE_COMBINING_MARK_MIN = 0x300;
static constexpr unsigned UNICODE_COMBINING_MARK_MAX = 0x36F;
static constexpr unsigned UNICODE_EXTEND_MARK_MIN = 0x2E80;
static constexpr unsigned UNICODE_EXTEND_MARK_MAX = 0x9fff;

bool ObJsonPathUtil::unicode_combining_mark(unsigned codepoint) {
  return ((UNICODE_COMBINING_MARK_MIN <= codepoint) && (codepoint <= UNICODE_COMBINING_MARK_MAX));
}

bool ObJsonPathUtil::is_utf8_unicode_charator(const char* ori, uint64_t& start, int64_t len)
{
  bool ret_bool = false;
  int ret = OB_SUCCESS;
  int32_t well_formed_error = 0;
  int64_t well_formed_len = 0;
  if (OB_FAIL(ObCharset::well_formed_len(CS_TYPE_UTF8MB4_BIN, ori + start, len, well_formed_len, well_formed_error))) {
    ret_bool = false;
  } else {
    ret_bool = true;
  }

  return ret_bool;
}

bool ObJsonPathUtil::is_letter(unsigned codepoint, const char* ori, uint64_t start, uint64_t end) {
  bool ret_bool = true;

  ret_bool = isalpha(codepoint);
  if (ret_bool) {
  } else if (!ret_bool && (codepoint >= UNICODE_EXTEND_MARK_MIN
             && codepoint <= UNICODE_EXTEND_MARK_MAX
             && ObJsonPathUtil::is_utf8_unicode_charator(ori, start, end - start))) {
    ret_bool = true;
  }
  return ret_bool;
}

bool ObJsonPathUtil::is_connector_punctuation(unsigned codepoint) {
  bool ret_bool = true;
  switch (codepoint) {
    case 0x5F:    // low line
    case 0x203F:  // undertie
    case 0x2040:  // character tie
    case 0x2054:  // inverted undertie
    case 0xFE33:  // presentation form for vertical low line
    case 0xFE34:  // presentation form for vertical wavy low line
    case 0xFE4D:  // dashed low line
    case 0xFE4E:  // centerline low line
    case 0xFE4F:  // wavy low line
    case 0xFF3F:  // fullwidth low line
    {
      ret_bool = true;
      break;
    }
    default: {
      ret_bool = false;
      break;
    }
  }

  return ret_bool;
}

int ObJsonPathUtil::string_cmp_skip_charactor(ObString& lhs, ObString& rhs, char ch) {
  int res = 0;
  int i = 0, j = 0;
  for (; i < lhs.length() && j < rhs.length(); ) {
    bool skip = false;
    if (lhs[i] == ch) {
      ++i;
      skip = true;
    }

    if (rhs[j] == ch) {
      ++j;
      skip = true;
    }

    if (skip) {
      continue;
    }

    if (lhs[i] != rhs[j]) {
      res = lhs[i] > rhs[j] ? 1 : -1;
      break;
    }
    ++i;
    ++j;
  }

  while (res == 0 && i < lhs.length()) {
    if (lhs[i] != ch) {
      res = 1;
      break;
    }
    ++i;
  }

  while (res == 0 && j < rhs.length()) {
    if (rhs[j] != ch) {
      res = -1;
      break;
    }
    ++j;
  }

  return res;
}

int ObJsonPathUtil::double_quote(ObString &name, ObJsonBuffer* tmp_name)
{
  INIT_SUCC(ret);
  if (OB_ISNULL(name.ptr())) {
    ret = OB_ERR_NULL_VALUE;
  } else if (OB_FAIL(ObJsonBaseUtil::add_double_quote(*tmp_name, name.ptr(), name.length()))) {
  }

  return ret;
}



} // namespace common
} // namespace oceanbase
