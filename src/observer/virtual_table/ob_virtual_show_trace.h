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
#ifndef OB_VIRTUAL_SHOW_TRACE_H_
#define OB_VIRTUAL_SHOW_TRACE_H_

#include "observer/virtual_table/ob_virtual_table_scanner_iterator.h"

namespace oceanbase
{
namespace observer
{

class ObShowTraceRec
{
public:
  struct Data
  {
    Data() { reset(); }
    void reset()
    {
      req_id_ = common::OB_INVALID_ID;
      ref_type_ = -1;
      start_ts_ = -1;
      end_ts_ = -1;
      trace_id_.reset();
      span_id_.reset();
      parent_span_id_.reset();
      span_name_.reset();
      tags_.reset();
      logs_.reset();
    }
    common::ObString trace_id_;
    int64_t req_id_;
    common::ObString span_id_;
    common::ObString parent_span_id_;
    common::ObString span_name_;
    int64_t ref_type_;
    int64_t start_ts_;
    int64_t end_ts_;
    common::ObString tags_;
    common::ObString logs_;
    TO_STRING_KV(K_(trace_id), K_(req_id), K_(span_id), K_(parent_span_id),
                 K_(span_name), K_(start_ts), K_(end_ts), K_(tags), K_(logs));
  };

  struct TraceFormatter
  {
    enum class LineType
    {
      LT_SPACE,
      LT_LINE,
      LT_NODE,
      LT_LAST_NODE,
    };

    struct TreeLine
    {
      TreeLine() : color_idx_(0), line_type_(LineType::LT_SPACE) {}
      int32_t color_idx_;
      LineType line_type_;
    };

    struct NameLeftPadding
    {
      NameLeftPadding() : level_(0), tree_line_(NULL) {}
      int64_t level_;
      TreeLine *tree_line_;
      TO_STRING_KV(K_(level), KP_(tree_line));
    };
  };

public:
  ObShowTraceRec() : formatter_(), data_(), ipstr_(), port_(-1) {}
  ~ObShowTraceRec() {}

  TraceFormatter::NameLeftPadding formatter_;
  Data data_;
  common::ObString ipstr_;
  int64_t port_;
  TO_STRING_KV(K_(formatter), K_(data), K_(port), K_(ipstr));
};

class ObVirtualShowTrace : public common::ObVirtualTableScannerIterator
{
public:
  ObVirtualShowTrace();
  virtual ~ObVirtualShowTrace();
  int inner_open();
  virtual int inner_get_next_row(common::ObNewRow *&row);
  virtual void reset();

private:
  int build_show_trace_rows_from_session();
  int format_show_trace_record(ObShowTraceRec &rec);
  int fill_cells(ObShowTraceRec &record);
  int alloc_trace_rec(ObShowTraceRec *&rec);

private:
  enum SYS_COLUMN
  {
    TRACE_ID = common::OB_APP_MIN_COLUMN_ID,
    REQUEST_ID,
    SPAN_ID,
    PARENT_SPAN_ID,
    SPAN_NAME,
    REF_TYPE,
    START_TS,
    END_TS,
    ELAPSE,
    TAGS,
    LOGS,
  };

  DISALLOW_COPY_AND_ASSIGN(ObVirtualShowTrace);

  bool is_first_get_;
  int64_t show_trace_rec_idx_;
  common::ObArenaAllocator alloc_;
  common::ObSEArray<ObShowTraceRec*, 16> show_trace_arr_;
  bool is_row_format_;
};

} /* namespace observer */
} /* namespace oceanbase */
#endif /* OB_VIRTUAL_SHOW_TRACE_H_ */
