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

#define USING_LOG_PREFIX SQL

#include "sql/monitor/show_trace/ob_show_trace.h"
#include "lib/time/ob_time_utility.h"
#include "sql/session/ob_basic_session_info.h"
#include <stdio.h>

namespace oceanbase
{
namespace sql
{

namespace
{

bool is_ascii_space(const char c)
{
  return ' ' == c || '\t' == c || '\n' == c || '\r' == c || '\f' == c;
}

bool is_ident_char(const char c)
{
  return (c >= 'a' && c <= 'z')
      || (c >= 'A' && c <= 'Z')
      || (c >= '0' && c <= '9')
      || '_' == c
      || '$' == c;
}

char lower_ascii(const char c)
{
  return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
}

void skip_space_and_comments(const char *str, const int64_t len, int64_t &pos)
{
  bool advanced = true;
  while (pos < len && advanced) {
    advanced = false;
    while (pos < len && is_ascii_space(str[pos])) {
      ++pos;
      advanced = true;
    }
    if (pos + 1 < len && '/' == str[pos] && '*' == str[pos + 1]) {
      pos += 2;
      while (pos + 1 < len && !('*' == str[pos] && '/' == str[pos + 1])) {
        ++pos;
      }
      if (pos + 1 < len) {
        pos += 2;
      }
      advanced = true;
    } else if (pos + 1 < len && '-' == str[pos] && '-' == str[pos + 1]) {
      pos += 2;
      while (pos < len && '\n' != str[pos]) {
        ++pos;
      }
      advanced = true;
    } else if (pos < len && '#' == str[pos]) {
      ++pos;
      while (pos < len && '\n' != str[pos]) {
        ++pos;
      }
      advanced = true;
    }
  }
}

bool consume_keyword(const char *str, const int64_t len, int64_t &pos, const char *kw)
{
  bool bret = true;
  int64_t kw_pos = 0;
  const int64_t start = pos;
  while ('\0' != kw[kw_pos]) {
    if (pos + kw_pos >= len || lower_ascii(str[pos + kw_pos]) != kw[kw_pos]) {
      bret = false;
      break;
    }
    ++kw_pos;
  }
  if (bret && pos + kw_pos < len && is_ident_char(str[pos + kw_pos])) {
    bret = false;
  }
  if (bret) {
    pos += kw_pos;
  } else {
    pos = start;
  }
  return bret;
}

} // namespace

const char *get_show_trace_span_name(const ObTraceSpanType type)
{
  static const char *SPAN_NAMES[] = {
    "com_query_process",
    "mpquery_single_stmt",
    "sql_compile",
    "pc_get_plan",
    "pc_add_plan",
    "hard_parse",
    "parse",
    "resolve",
    "rewrite",
    "optimize",
    "code_generate",
    "sql_execute",
    "open",
    "response_result",
    "px_schedule",
    "px_sqc",
    "close",
    "end_transaction",
  };
  const int64_t idx = static_cast<int64_t>(type);
  return (idx >= 0 && idx < TRACE_SPAN_MAX)
      ? SPAN_NAMES[idx]
      : "unknown";
}

const char *get_show_trace_tag_name(const ObTraceTagKey key)
{
  static const char *TAG_NAMES[] = {
    "qc_id",
    "dfo_id",
    "sqc_id",
    "task_count",
    "ret_code",
  };
  const int64_t idx = static_cast<int64_t>(key);
  return (idx >= 0 && idx < TRACE_TAG_MAX)
      ? TAG_NAMES[idx]
      : "unknown";
}

ObShowTraceSessionBuffer::Span::Span()
{
  reset();
}

void ObShowTraceSessionBuffer::Span::reset()
{
  start_ts_ = 0;
  end_ts_ = 0;
  parent_idx_ = -1;
  depth_ = 0;
  type_ = 0;
  flags_ = 0;
}

ObShowTraceSessionBuffer::Tag::Tag()
{
  reset();
}

void ObShowTraceSessionBuffer::Tag::reset()
{
  span_idx_ = -1;
  key_ = 0;
  int_value_ = 0;
}

ObShowTraceSessionBuffer::ObShowTraceSessionBuffer()
  : spans_(),
    tags_(),
    stack_(),
    span_count_(0),
    stack_depth_(0),
    tag_count_(0),
    recording_(false),
    span_truncated_(false),
    depth_truncated_(false),
    tag_truncated_(false),
    trace_id_()
{
  trace_id_[0] = '\0';
}

void ObShowTraceSessionBuffer::reset_for_stmt(const uint64_t sessid)
{
  span_count_ = 0;
  stack_depth_ = 0;
  tag_count_ = 0;
  span_truncated_ = false;
  depth_truncated_ = false;
  tag_truncated_ = false;
  const int64_t now = common::ObTimeUtility::current_time();
  const int n = snprintf(trace_id_, sizeof(trace_id_), "show-trace-%llu-%ld",
                         static_cast<unsigned long long>(sessid), now);
  if (n <= 0 || n >= static_cast<int>(sizeof(trace_id_))) {
    trace_id_[0] = '\0';
  }
  recording_ = true;
}

void ObShowTraceSessionBuffer::finish_stmt()
{
  recording_ = false;
  stack_depth_ = 0;
}

int16_t ObShowTraceSessionBuffer::current_parent_idx() const
{
  return stack_depth_ > 0 ? stack_[stack_depth_ - 1] : -1;
}

void ObShowTraceSessionBuffer::push_span(const int16_t span_idx)
{
  if (stack_depth_ < MAX_STACK_DEPTH) {
    stack_[stack_depth_++] = span_idx;
  } else {
    depth_truncated_ = true;
  }
}

void ObShowTraceSessionBuffer::pop_span(const int16_t span_idx)
{
  if (stack_depth_ > 0 && stack_[stack_depth_ - 1] == span_idx) {
    --stack_depth_;
  }
}

int64_t ObShowTraceSessionBuffer::begin_span(const ObTraceSpanType type)
{
  int64_t span_idx = -1;
  if (!recording_) {
    // do nothing
  } else if (span_count_ >= MAX_SPAN_COUNT) {
    span_truncated_ = true;
  } else {
    Span &span = spans_[span_count_];
    span.reset();
    span.start_ts_ = common::ObTimeUtility::current_time();
    span.end_ts_ = span.start_ts_;
    span.parent_idx_ = current_parent_idx();
    span.depth_ = stack_depth_;
    span.type_ = static_cast<uint16_t>(type);
    span_idx = span_count_++;
    push_span(static_cast<int16_t>(span_idx));
  }
  return span_idx;
}

void ObShowTraceSessionBuffer::end_span(const int64_t span_idx)
{
  if (recording_ && span_idx >= 0 && span_idx < span_count_) {
    spans_[span_idx].end_ts_ = common::ObTimeUtility::current_time();
    pop_span(static_cast<int16_t>(span_idx));
  }
}

void ObShowTraceSessionBuffer::add_int_tag(const int64_t span_idx,
                                           const ObTraceTagKey key,
                                           const int64_t value)
{
  if (!recording_) {
    // do nothing
  } else if (span_idx < 0 || span_idx >= span_count_
             || key < 0 || key >= TRACE_TAG_MAX) {
    // do nothing
  } else if (tag_count_ >= MAX_TAG_COUNT) {
    tag_truncated_ = true;
  } else {
    Tag &tag = tags_[tag_count_++];
    tag.span_idx_ = static_cast<int16_t>(span_idx);
    tag.key_ = static_cast<uint16_t>(key);
    tag.int_value_ = value;
  }
}

bool ObShowTraceSessionBuffer::is_show_trace_sql(const common::ObString &sql)
{
  bool bret = false;
  const char *str = sql.ptr();
  const int64_t len = sql.length();
  int64_t pos = 0;
  if (OB_ISNULL(str) || len <= 0) {
    // do nothing
  } else {
    skip_space_and_comments(str, len, pos);
    if (consume_keyword(str, len, pos, "show")) {
      skip_space_and_comments(str, len, pos);
      bret = consume_keyword(str, len, pos, "trace");
    }
  }
  return bret;
}

ObTraceSpanGuard::ObTraceSpanGuard(ObBasicSessionInfo *session, const ObTraceSpanType type)
  : buf_(NULL),
    span_idx_(-1)
{
  if (OB_NOT_NULL(session)) {
    buf_ = session->get_show_trace_buffer();
    if (OB_NOT_NULL(buf_) && buf_->is_recording()) {
      span_idx_ = buf_->begin_span(type);
    }
  }
}

ObTraceSpanGuard::~ObTraceSpanGuard()
{
  if (OB_NOT_NULL(buf_) && span_idx_ >= 0) {
    buf_->end_span(span_idx_);
  }
}

void ObTraceSpanGuard::set_tag(const ObTraceTagKey key, const int64_t value)
{
  if (OB_NOT_NULL(buf_) && span_idx_ >= 0) {
    buf_->add_int_tag(span_idx_, key, value);
  }
}

OTraceGuard::OTraceGuard(ObBasicSessionInfo &session, const common::ObString &sql)
  : session_(&session),
    started_(false)
{
  if (!session.is_use_trace_log()) {
    session.destroy_show_trace_buffer();
  } else if (ObShowTraceSessionBuffer::is_show_trace_sql(sql)) {
    // Keep the previous statement trace for SHOW TRACE itself.
  } else {
    int ret = common::OB_SUCCESS;
    if (OB_FAIL(session.start_show_trace_recording())) {
      LOG_WARN("failed to start show trace recording", K(ret));
    } else {
      started_ = true;
    }
  }
}

OTraceGuard::~OTraceGuard()
{
  if (started_ && OB_NOT_NULL(session_)) {
    session_->finish_show_trace_recording();
  }
}

} // namespace sql
} // namespace oceanbase
