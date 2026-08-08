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
// Enum/set internal values to strings.
#include "lib/string/ob_string.h"
#include "lib/container/ob_iarray.h"
#include "lib/charset/ob_charset.h"
#define USING_LOG_PREFIX SHARE
#include "share/object/ob_enumset_str_util.h"
#include "share/ob_lob_access_utils.h"
#include "lib/string/ob_sql_string.h"

namespace oceanbase {
namespace common {
namespace enumset_str {
// same value as sql ObExprSetToStr::EFFECTIVE_COUNT(effective bit count for sets stored as uint64 bitmaps)
static const int64_t EFFECTIVE_COUNT = 64;

int enum_to_str(const uint64_t enum_val,
                                  const ObIArray<ObString> &str_values,
                                  common::ObTextStringResult &text_result)
{
  int ret = OB_SUCCESS;
  const int64_t element_num = str_values.count();
  const uint64_t element_idx = enum_val - 1;
  ObString element_str;
  if (OB_UNLIKELY(element_num < 1)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("invalid element num", K(element_num), K(element_num));
  } else if (0 == enum_val) {
    // ObString empty_string;
  } else if (OB_UNLIKELY(element_idx > element_num - 1)) {
    ret = OB_ERR_DATA_TRUNCATED;
    LOG_WARN("enum value out of range", K(element_idx), K(element_num), K(ret));
  } else {
    element_str = str_values.at(element_idx);
  }
  if (OB_SUCC(ret)) {
    if (OB_FAIL(text_result.init(element_str.length()))) {
      LOG_WARN("init lob result failed");
    } else if (OB_FAIL(text_result.append(element_str.ptr(), element_str.length()))) {
      LOG_WARN("failed to append real data", K(ret), K(text_result));
    }
  }
  return ret;
}

int set_to_str(const ObCollationType cs_type,
                                 const uint64_t set_val,
                                 const ObIArray<common::ObString> &str_values,
                                 common::ObTextStringResult &text_result)
{
  int ret = OB_SUCCESS;
  const ObString &sep = ObCharsetUtils::get_const_str(cs_type, ',');
  // When there are duplicate values, element_num will be greater than 64,
  // and values after 64 will be ignored.
  int64_t element_num = str_values.count();
  if (OB_UNLIKELY(element_num < 1)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("invalid element num", K(element_num), K(ret));
  } else if (OB_UNLIKELY(element_num < EFFECTIVE_COUNT && set_val >= (1ULL << element_num))) {
    ret = OB_ERR_DATA_TRUNCATED;
    LOG_WARN("set value out of range", K(set_val), K(element_num));
  }

  int64_t need_size = 0;
  uint64_t index = 1ULL;
  for (int64_t i = 0;
        OB_SUCC(ret) && i < element_num && i < EFFECTIVE_COUNT && set_val >= index;
        ++i, index = index << 1) {
    if (set_val & (index)) {
      need_size += str_values.at(i).length();
      need_size += ((set_val >= (index << 1)) ? sep.length() : 0);
    }
  }

  if (OB_SUCC(ret)) {
    if (OB_FAIL(text_result.init(need_size))) {
      LOG_WARN("init lob result failed", K(ret), K(need_size));
    } else {
      uint64_t index = 1ULL;
      for (int64_t i = 0;
            OB_SUCC(ret) && i < element_num && i < EFFECTIVE_COUNT && set_val >= index;
            ++i, index = index << 1) {
        if (set_val & (index)) {
          const ObString &element_val = str_values.at(i);
          if (OB_UNLIKELY(element_val.empty())) {
            // skip empty string and its separator
          } else if (OB_FAIL(text_result.append(element_val))) {
            LOG_WARN("fail to append str to lob result", K(ret), K(element_val));
          } else if ((i + 1) < element_num && (i + 1) < EFFECTIVE_COUNT &&
              ((index << 1) <= set_val)) {
            // skip setting last seperator
            if (OB_FAIL(text_result.append(sep))) {
              LOG_WARN("fail to append str to lob result", K(ret), K(sep));
            }
          }
        }
      }
    }
  }
  return ret;
}

}  // namespace enumset_str
}  // namespace common
}  // namespace oceanbase
