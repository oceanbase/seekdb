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
#include "lib/utility/ob_printf.h"
#include <cstdarg>

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

int ObWarningBuffer::append_warnings(const ObWarningBuffer &other)
{
  int ret = OB_SUCCESS;
  if (this == &other) {
    ret = OB_INVALID_ARGUMENT;
  } else if (OB_SUCCESS != error_ret_) {
    ret = error_ret_;
  } else {
    const uint32_t readable = other.get_readable_warning_count();
    for (uint32_t i = 0; OB_SUCC(ret) && i < readable; ++i) {
      WarningItem *item = append_idx_ < item_.count()
          ? &item_[append_idx_] : item_.alloc_place_holder();
      if (nullptr == item) {
        ret = error_ret_ = OB_ALLOCATE_MEMORY_FAILED;
      } else {
        *item = *other.get_warning_item(i);
        append_idx_ = (append_idx_ + 1) % MAX_BUFFER_SIZE;
        if (total_warning_count_ < UINT32_MAX) ++total_warning_count_;
      }
    }
    if (OB_SUCC(ret)) {
      const uint64_t total = static_cast<uint64_t>(total_warning_count_)
          + other.total_warning_count_ - readable;
      total_warning_count_ = static_cast<uint32_t>(total > UINT32_MAX ? UINT32_MAX : total);
      // A source allocation failure must not become an apparently complete
      // diagnostic set merely because the retained prefix could be copied.
      ret = other.error_ret_;
    }
  }
  return ret;
}

void log_user_message(UserMsgLevel level, int errcode, const char *fmt, ...)
{
  char buf[ObWarningBuffer::WarningItem::STR_LEN] = {};
  va_list args;
  va_start(args, fmt);
  const int64_t len = ob_vsnprintf(buf, sizeof(buf), fmt, args);
  va_end(args);
  insert_warning_buffer(level, errcode, buf, len);
}

void insert_warning_buffer(UserMsgLevel level, int errcode, const char *data, int64_t len)
{
  if (ObWarningBuffer::is_warn_log_on() && len > 0) {
    ObWarningBuffer *buffer = ob_get_tsi_warning_buffer();
    if (buffer != nullptr) {
      if (level == USER_ERROR) {
        buffer->set_error(data, errcode);
      } else if (level == USER_WARN) {
        buffer->append_warning(data, errcode);
      } else if (level == USER_NOTE) {
        buffer->append_note(data, errcode);
      }
    }
  }
}

void log_user_error_line_column(UserMsgLevel level, int line, int column)
{
  if (ObWarningBuffer::is_warn_log_on() && level == USER_ERROR) {
    ObWarningBuffer *buffer = ob_get_tsi_warning_buffer();
    if (buffer != nullptr) buffer->set_error_line_column(line, column);
  }
}

}
}
