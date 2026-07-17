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

#ifndef OCEANBASE_SQL_MONITOR_SHOW_TRACE_OB_SHOW_TRACE_H_
#define OCEANBASE_SQL_MONITOR_SHOW_TRACE_OB_SHOW_TRACE_H_

#include "share/ob_define.h"
#include "lib/string/ob_string.h"

namespace oceanbase
{
namespace sql
{

class ObBasicSessionInfo;

enum ObTraceSpanType
{
  TRACE_COM_QUERY_PROCESS = 0,
  TRACE_MPQUERY_SINGLE_STMT,
  TRACE_SQL_COMPILE,
  TRACE_PC_GET_PLAN,
  TRACE_PC_ADD_PLAN,
  TRACE_HARD_PARSE,
  TRACE_PARSE,
  TRACE_RESOLVE,
  TRACE_REWRITE,
  TRACE_OPTIMIZE,
  TRACE_CODE_GENERATE,
  TRACE_SQL_EXECUTE,
  TRACE_OPEN,
  TRACE_RESPONSE_RESULT,
  TRACE_PX_SCHEDULE,
  TRACE_PX_SQC,
  TRACE_CLOSE,
  TRACE_END_TRANSACTION,
  TRACE_SPAN_MAX
};

enum ObTraceTagKey
{
  TRACE_TAG_QC_ID = 0,
  TRACE_TAG_DFO_ID,
  TRACE_TAG_SQC_ID,
  TRACE_TAG_TASK_COUNT,
  TRACE_TAG_RET_CODE,
  TRACE_TAG_MAX
};

const char *get_show_trace_span_name(const ObTraceSpanType type);
const char *get_show_trace_tag_name(const ObTraceTagKey key);

class ObShowTraceSessionBuffer
{
public:
  static const int64_t MAX_SPAN_COUNT = 64;
  static const int64_t MAX_STACK_DEPTH = 16;
  static const int64_t MAX_TAG_COUNT = 96;
  static const int64_t TRACE_ID_BUF_LEN = 64;

  struct Span
  {
    Span();
    void reset();

    int64_t start_ts_;
    int64_t end_ts_;
    int16_t parent_idx_;
    int16_t depth_;
    uint16_t type_;
    uint16_t flags_;
  };

  struct Tag
  {
    Tag();
    void reset();

    int16_t span_idx_;
    uint16_t key_;
    int64_t int_value_;
  };

public:
  ObShowTraceSessionBuffer();
  ~ObShowTraceSessionBuffer() {}

  void reset_for_stmt(const uint64_t sessid);
  void finish_stmt();
  bool is_recording() const { return recording_; }
  bool has_trace() const { return span_count_ > 0; }
  bool is_truncated() const { return span_truncated_ || depth_truncated_ || tag_truncated_; }
  bool is_span_truncated() const { return span_truncated_; }
  bool is_depth_truncated() const { return depth_truncated_; }
  bool is_tag_truncated() const { return tag_truncated_; }
  int64_t get_span_count() const { return span_count_; }
  const Span &get_span(const int64_t idx) const { return spans_[idx]; }
  int64_t get_tag_count() const { return tag_count_; }
  const Tag &get_tag(const int64_t idx) const { return tags_[idx]; }
  const char *get_trace_id_str() const { return trace_id_; }

  int64_t begin_span(const ObTraceSpanType type);
  void end_span(const int64_t span_idx);
  void add_int_tag(const int64_t span_idx, const ObTraceTagKey key, const int64_t value);

  static bool is_show_trace_sql(const common::ObString &sql);

private:
  int16_t current_parent_idx() const;
  void push_span(const int16_t span_idx);
  void pop_span(const int16_t span_idx);

private:
  Span spans_[MAX_SPAN_COUNT];
  Tag tags_[MAX_TAG_COUNT];
  int16_t stack_[MAX_STACK_DEPTH];
  int16_t span_count_;
  int16_t stack_depth_;
  int16_t tag_count_;
  bool recording_;
  bool span_truncated_;
  bool depth_truncated_;
  bool tag_truncated_;
  char trace_id_[TRACE_ID_BUF_LEN];
};

class ObTraceSpanGuard
{
public:
  ObTraceSpanGuard(ObBasicSessionInfo *session, const ObTraceSpanType type);
  ~ObTraceSpanGuard();
  void set_tag(const ObTraceTagKey key, const int64_t value);

private:
  ObShowTraceSessionBuffer *buf_;
  int64_t span_idx_;
};

class OTraceGuard
{
public:
  OTraceGuard(ObBasicSessionInfo &session, const common::ObString &sql);
  ~OTraceGuard();

private:
  ObBasicSessionInfo *session_;
  bool started_;
};

} // namespace sql
} // namespace oceanbase

#endif // OCEANBASE_SQL_MONITOR_SHOW_TRACE_OB_SHOW_TRACE_H_
