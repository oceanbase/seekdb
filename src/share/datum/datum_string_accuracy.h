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
#pragma once

#include "common/datum/datum_core.h"
#include "share/datum/datum_cast_context.h"

namespace oceanbase {
namespace common {

// Apply the varchar length semantics used by SQL datum casts without requiring
// an ObExpr or ObEvalCtx. The input is never modified; the output borrows its
// payload, so callers must consume/copy it before the input storage expires.
inline int datum_varchar_accuracy_check(const ObCastMode cast_mode,
                                        const ObLength max_char_length,
                                        const ObCollationType collation,
                                        const ObDatum &input,
                                        ObDatum &output,
                                        int &warning)
{
  int ret = OB_SUCCESS;
  output.set_datum(input);
  if (!input.is_null()) {
    const int32_t byte_length = input.len_;
    const char *str = input.ptr_;
    if (byte_length > 0 && str == nullptr) {
      ret = OB_INVALID_ARGUMENT;
    } else if (max_char_length != -1 &&
               (max_char_length <= 0 || byte_length > max_char_length)) {
      int &cast_ret = CM_IS_ERROR_ON_FAIL(cast_mode) ? ret : warning;
      if (max_char_length <= 0) {
        output.set_string(nullptr, 0);
        if (max_char_length == 0 && byte_length > 0) {
          cast_ret = OB_ERR_DATA_TOO_LONG;
        }
      } else {
        const int32_t non_space_bytes = static_cast<int32_t>(
            ObCharset::strlen_byte_no_sp(collation, str, byte_length));
        const int32_t non_space_chars = static_cast<int32_t>(
            ObCharset::strlen_char(collation, str, non_space_bytes));
        if (non_space_chars > max_char_length) {
          cast_ret = OB_ERR_DATA_TOO_LONG;
        } else {
          const int32_t char_length = static_cast<int32_t>(
              ObCharset::strlen_char(collation, str, byte_length));
          // Excess trailing spaces are truncation warnings even in strict mode.
          if (char_length > max_char_length) {
            warning = OB_ERR_DATA_TOO_LONG;
          }
        }
        if (OB_SUCC(ret) && warning == OB_ERR_DATA_TOO_LONG) {
          const int32_t truncated_bytes = static_cast<int32_t>(
              ObCharset::charpos(collation, str, byte_length, max_char_length));
          output.set_string(str, static_cast<uint32_t>(truncated_bytes));
        }
      }
    }
  }
  return ret;
}

} // namespace common
} // namespace oceanbase
