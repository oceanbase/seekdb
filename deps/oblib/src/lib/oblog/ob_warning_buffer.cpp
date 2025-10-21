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

#include "lib/oblog/ob_warning_buffer.h"

namespace oceanbase
{
namespace common
{
bool ObWarningBuffer::is_log_on_ = false;
_RLOCAL(ObWarningBuffer *, g_warning_buffer);

OB_SERIALIZE_MEMBER(ObWarningBuffer::WarningItem,
                    msg_,
                    code_,
                    log_level_,
                    line_no_,
                    column_no_);

ObWarningBuffer &ObWarningBuffer::operator= (const ObWarningBuffer &other)
{
  if (this != &other) {
    reset();
    int ret = item_.assign(other.item_);
    if (OB_FAIL(ret)) {
      error_ret_ = ret;
    } else {
      err_ = other.err_;
      append_idx_ = other.append_idx_;
      total_warning_count_ = other.total_warning_count_;
    }
  }
  return *this;
}

ObWarningBuffer::WarningItem &ObWarningBuffer::WarningItem::operator= (const WarningItem &other)
{
  if (this != &other) {
    STRCPY(msg_, other.msg_);
    timestamp_ = other.timestamp_;
    log_level_ = other.log_level_;
    line_no_ = other.line_no_;
    column_no_ = other.column_no_;
    code_ = other.code_;
    STRCPY(sql_state_, other.sql_state_);
  }
  return *this;
}

}
}
