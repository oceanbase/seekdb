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

#define USING_LOG_PREFIX SERVER

#include "observer/virtual_table/ob_virtual_show_trace.h"
#include "sql/session/ob_sql_session_info.h"
#include "sql/monitor/show_trace/ob_show_trace.h"
#include <stdio.h>
#include <string.h>

using namespace oceanbase::common;
using namespace oceanbase::share;

namespace oceanbase
{
namespace observer
{

namespace
{

int append_json_tag_prefix(char *buf, const int64_t buf_len, int64_t &pos, bool &has_tag)
{
  int ret = OB_SUCCESS;
  const char *prefix = has_tag ? "," : "[{";
  const int n = snprintf(buf + pos, buf_len - pos, "%s", prefix);
  if (n <= 0 || n >= buf_len - pos) {
    ret = OB_SIZE_OVERFLOW;
    SERVER_LOG(WARN, "failed to append show trace tag prefix", K(ret), K(pos), K(buf_len));
  } else {
    pos += n;
    has_tag = true;
  }
  return ret;
}

int append_json_int_tag(char *buf, const int64_t buf_len, int64_t &pos,
                        bool &has_tag, const char *key, const int64_t value)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(append_json_tag_prefix(buf, buf_len, pos, has_tag))) {
    // do nothing
  } else {
    const int n = snprintf(buf + pos, buf_len - pos, "\"%s\":%ld", key, value);
    if (n <= 0 || n >= buf_len - pos) {
      ret = OB_SIZE_OVERFLOW;
      SERVER_LOG(WARN, "failed to append show trace int tag", K(ret), K(key), K(pos), K(buf_len));
    } else {
      pos += n;
    }
  }
  return ret;
}

int append_json_bool_tag(char *buf, const int64_t buf_len, int64_t &pos,
                         bool &has_tag, const char *key, const bool value)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(append_json_tag_prefix(buf, buf_len, pos, has_tag))) {
    // do nothing
  } else {
    const int n = snprintf(buf + pos, buf_len - pos, "\"%s\":%s", key, value ? "true" : "false");
    if (n <= 0 || n >= buf_len - pos) {
      ret = OB_SIZE_OVERFLOW;
      SERVER_LOG(WARN, "failed to append show trace bool tag", K(ret), K(key), K(pos), K(buf_len));
    } else {
      pos += n;
    }
  }
  return ret;
}

int close_json_tags(char *buf, const int64_t buf_len, int64_t &pos, const bool has_tag)
{
  int ret = OB_SUCCESS;
  if (!has_tag) {
    buf[0] = '\0';
    pos = 0;
  } else {
    const int n = snprintf(buf + pos, buf_len - pos, "}]");
    if (n <= 0 || n >= buf_len - pos) {
      ret = OB_SIZE_OVERFLOW;
      SERVER_LOG(WARN, "failed to close show trace tags", K(ret), K(pos), K(buf_len));
    } else {
      pos += n;
    }
  }
  return ret;
}

} // namespace

ObVirtualShowTrace::ObVirtualShowTrace()
    : ObVirtualTableScannerIterator(),
      is_first_get_(true),
      show_trace_rec_idx_(-1),
      alloc_(),
      show_trace_arr_(),
      is_row_format_(true)
{
}

ObVirtualShowTrace::~ObVirtualShowTrace()
{
  reset();
}

void ObVirtualShowTrace::reset()
{
  ObVirtualTableScannerIterator::reset();
  is_first_get_ = true;
  show_trace_rec_idx_ = -1;
  show_trace_arr_.reset();
  alloc_.reset();
  is_row_format_ = true;
}

int ObVirtualShowTrace::inner_open()
{
  return OB_SUCCESS;
}

int ObVirtualShowTrace::build_show_trace_rows_from_session()
{
  int ret = OB_SUCCESS;
  const sql::ObShowTraceSessionBuffer *trace_buf = NULL;
  char ipstr_buf[common::MAX_IP_ADDR_LENGTH + 2];
  int64_t ipstr_len = 0;
  if (OB_ISNULL(session_)) {
    ret = OB_NOT_INIT;
    SERVER_LOG(WARN, "session is null", K(ret));
  } else if (OB_ISNULL(trace_buf = session_->get_show_trace_buffer()) || !trace_buf->has_trace()) {
    // no lightweight show trace record
  } else {
    is_row_format_ = session_->is_row_traceformat();
    const ObAddr &self_addr = GCTX.self_addr();
    if (!self_addr.ip_to_string(ipstr_buf, sizeof(ipstr_buf))) {
      ipstr_buf[0] = '\0';
      ipstr_len = 0;
    } else {
      ipstr_len = static_cast<int64_t>(strlen(ipstr_buf));
    }

    for (int64_t i = 0; OB_SUCC(ret) && i < trace_buf->get_span_count(); ++i) {
      ObShowTraceRec *rec = NULL;
      const sql::ObShowTraceSessionBuffer::Span &span = trace_buf->get_span(i);
      const char *span_name = sql::get_show_trace_span_name(
          static_cast<sql::ObTraceSpanType>(span.type_));
      const char *trace_id = trace_buf->get_trace_id_str();
      char span_id_buf[128];
      char parent_id_buf[128];
      char tags_buf[512];
      const int64_t trace_id_len = static_cast<int64_t>(strlen(trace_id));
      const int span_id_len = snprintf(span_id_buf, sizeof(span_id_buf), "%s-%ld", trace_id, i);
      int parent_id_len = 0;
      int64_t tag_len = 0;
      bool has_tag = false;
      tags_buf[0] = '\0';

      if (span.parent_idx_ < 0) {
        MEMCPY(parent_id_buf, "00000000-0000-0000-0000-000000000000", 36);
        parent_id_buf[36] = '\0';
        parent_id_len = 36;
      } else {
        parent_id_len = snprintf(parent_id_buf, sizeof(parent_id_buf),
                                 "%s-%d", trace_id, span.parent_idx_);
      }

      for (int64_t tag_idx = 0; OB_SUCC(ret) && tag_idx < trace_buf->get_tag_count(); ++tag_idx) {
        const sql::ObShowTraceSessionBuffer::Tag &tag = trace_buf->get_tag(tag_idx);
        if (tag.span_idx_ == i) {
          const char *key = sql::get_show_trace_tag_name(
              static_cast<sql::ObTraceTagKey>(tag.key_));
          OZ(append_json_int_tag(tags_buf, sizeof(tags_buf), tag_len, has_tag, key, tag.int_value_));
        }
      }
      if (OB_SUCC(ret) && 0 == i && trace_buf->is_truncated()) {
        OZ(append_json_bool_tag(tags_buf, sizeof(tags_buf), tag_len, has_tag,
                                "truncated", true));
        OZ(append_json_bool_tag(tags_buf, sizeof(tags_buf), tag_len, has_tag,
                                "span_truncated", trace_buf->is_span_truncated()));
        OZ(append_json_bool_tag(tags_buf, sizeof(tags_buf), tag_len, has_tag,
                                "depth_truncated", trace_buf->is_depth_truncated()));
        OZ(append_json_bool_tag(tags_buf, sizeof(tags_buf), tag_len, has_tag,
                                "tag_truncated", trace_buf->is_tag_truncated()));
      }
      if (OB_SUCC(ret)) {
        OZ(close_json_tags(tags_buf, sizeof(tags_buf), tag_len, has_tag));
      }

      if (OB_FAIL(ret)) {
        SERVER_LOG(WARN, "failed to format lightweight show trace tags", K(ret));
      } else if (span_id_len <= 0 || span_id_len >= static_cast<int>(sizeof(span_id_buf))
          || parent_id_len <= 0 || parent_id_len >= static_cast<int>(sizeof(parent_id_buf))) {
        ret = OB_SIZE_OVERFLOW;
        SERVER_LOG(WARN, "failed to format lightweight show trace id", K(ret),
                   K(span_id_len), K(parent_id_len));
      } else if (OB_FAIL(alloc_trace_rec(rec))) {
        SERVER_LOG(WARN, "failed to alloc record", K(ret));
      } else if (OB_ISNULL(rec)) {
        ret = OB_ERR_UNEXPECTED;
        SERVER_LOG(WARN, "record ptr is null", K(ret));
      } else {
        rec->data_.req_id_ = i;
        rec->data_.ref_type_ = 0;
        rec->data_.start_ts_ = span.start_ts_;
        rec->data_.end_ts_ = span.end_ts_;
        rec->port_ = static_cast<int64_t>(self_addr.get_port());
        OZ(ob_write_string(alloc_, ObString(trace_id_len, trace_id), rec->data_.trace_id_));
        OZ(ob_write_string(alloc_, ObString(span_id_len, span_id_buf), rec->data_.span_id_));
        OZ(ob_write_string(alloc_, ObString(parent_id_len, parent_id_buf), rec->data_.parent_span_id_));
        OZ(ob_write_string(alloc_, ObString(static_cast<int64_t>(strlen(span_name)), span_name),
                           rec->data_.span_name_));
        OZ(ob_write_string(alloc_, ObString(tag_len, tags_buf), rec->data_.tags_));
        OZ(ob_write_string(alloc_, ObString(0, ""), rec->data_.logs_));
        OZ(ob_write_string(alloc_, ObString(ipstr_len, ipstr_buf), rec->ipstr_));
        if (OB_SUCC(ret) && is_row_format_ && span.depth_ > 0) {
          rec->formatter_.level_ = span.depth_;
          rec->formatter_.tree_line_ =
              static_cast<ObShowTraceRec::TraceFormatter::TreeLine *>(
                  alloc_.alloc(sizeof(ObShowTraceRec::TraceFormatter::TreeLine) * span.depth_));
          if (OB_ISNULL(rec->formatter_.tree_line_)) {
            ret = OB_ALLOCATE_MEMORY_FAILED;
            SERVER_LOG(WARN, "allocate memory failed", K(ret), K(span.depth_));
          } else {
            for (int64_t j = 0; j < span.depth_; ++j) {
              rec->formatter_.tree_line_[j].line_type_ =
                  ObShowTraceRec::TraceFormatter::LineType::LT_SPACE;
            }
            rec->formatter_.tree_line_[span.depth_ - 1].line_type_ =
                ObShowTraceRec::TraceFormatter::LineType::LT_NODE;
            OZ(format_show_trace_record(*rec));
          }
        }
        OZ(show_trace_arr_.push_back(rec));
      }
    }
  }
  return ret;
}

int ObVirtualShowTrace::format_show_trace_record(ObShowTraceRec &rec)
{
  int ret = OB_SUCCESS;
  const int64_t level = rec.formatter_.level_;
  ObSqlString buff;
  const ObShowTraceRec::TraceFormatter::NameLeftPadding &pad = rec.formatter_;
  for (int64_t i = 0; OB_SUCC(ret) && i < level; ++i) {
    if (OB_UNLIKELY(NULL == pad.tree_line_)) {
      ret = OB_ERR_UNEXPECTED;
      SERVER_LOG(WARN, "tree line ptr is null", K(ret), K(i));
    } else {
      const ObShowTraceRec::TraceFormatter::TreeLine &tl = pad.tree_line_[i];
      switch(tl.line_type_) {
        case ObShowTraceRec::TraceFormatter::LineType::LT_SPACE: {
          OZ(buff.append("    "));
          break;
        }
        case ObShowTraceRec::TraceFormatter::LineType::LT_LINE: {
          OZ(buff.append("│   "));
          break;
        }
        case ObShowTraceRec::TraceFormatter::LineType::LT_NODE: {
          OZ(buff.append("├── "));
          break;
        }
        case ObShowTraceRec::TraceFormatter::LineType::LT_LAST_NODE: {
          OZ(buff.append("└── "));
          break;
        }
        default: {
          ret = OB_ERR_UNEXPECTED;
          SERVER_LOG(WARN, "invalid tree line type", K(ret), K(tl.line_type_));
          break;
        }
      }
    }
  }
  if (OB_SUCC(ret)) {
    OZ(buff.append(rec.data_.span_name_));
  }
  if (OB_SUCC(ret)) {
    OZ(ob_write_string(alloc_, buff.string(), rec.data_.span_name_));
  }
  return ret;
}

int ObVirtualShowTrace::inner_get_next_row(common::ObNewRow *&row)
{
  int ret = OB_SUCCESS;
  if (is_first_get_) {
    show_trace_arr_.reset();
    if (OB_FAIL(build_show_trace_rows_from_session())) {
      SERVER_LOG(WARN, "failed to build show trace rows from session", K(ret));
    } else {
      is_first_get_ = false;
      show_trace_rec_idx_ = 0;
    }
    LOG_TRACE("after pre processed", K(show_trace_arr_.count()), K(ret));
  }

  if (show_trace_arr_.empty()) {
    ret = OB_ITER_END;
  } else if (OB_SUCC(ret)) {
    if (show_trace_rec_idx_ < 0) {
      ret = OB_ERR_UNEXPECTED;
      SERVER_LOG(WARN, "invalid show trace array index", K(show_trace_rec_idx_));
    } else if (show_trace_rec_idx_ >= show_trace_arr_.count()) {
      ret = OB_ITER_END;
      show_trace_rec_idx_ = OB_INVALID_ID;
      show_trace_arr_.reset();
    } else if (OB_ISNULL(show_trace_arr_.at(show_trace_rec_idx_))) {
      ret = OB_ERR_UNEXPECTED;
      SERVER_LOG(WARN, "record ptr is null", K(show_trace_rec_idx_));
    } else {
      ObShowTraceRec rec = *show_trace_arr_.at(show_trace_rec_idx_);
      ++show_trace_rec_idx_;
      if (OB_FAIL(fill_cells(rec))) {
        SERVER_LOG(WARN, "fail to fill cells", K(ret), K(rec));
      } else {
        row = &cur_row_;
      }
    }
  }
  return ret;
}

int ObVirtualShowTrace::fill_cells(ObShowTraceRec &record)
{
  int ret = OB_SUCCESS;
  const int64_t col_count = output_column_ids_.count();
  ObObj *cells = cur_row_.cells_;

  if (OB_ISNULL(cells)) {
    ret = OB_INVALID_ARGUMENT;
    SERVER_LOG(WARN, "invalid argument", K(cells));
  } else {
    for (int64_t cell_idx = 0; OB_SUCC(ret) && cell_idx < col_count; cell_idx++) {
      uint64_t col_id = output_column_ids_.at(cell_idx);
      switch(col_id) {
      case TRACE_ID: {
        cells[cell_idx].set_varchar(record.data_.trace_id_);
        cells[cell_idx].set_collation_type(ObCharset::get_default_collation(
                                             ObCharset::get_default_charset()));
      } break;
      case REQUEST_ID: {
        cells[cell_idx].set_int(record.data_.req_id_);
      } break;
      case SPAN_ID: {
        cells[cell_idx].set_varchar(record.data_.span_id_);
        cells[cell_idx].set_collation_type(ObCharset::get_default_collation(
                                             ObCharset::get_default_charset()));
      } break;
      case PARENT_SPAN_ID: {
        cells[cell_idx].set_varchar(record.data_.parent_span_id_);
        cells[cell_idx].set_collation_type(ObCharset::get_default_collation(
                                             ObCharset::get_default_charset()));
      } break;
      case SPAN_NAME: {
        cells[cell_idx].set_varchar(record.data_.span_name_);
        cells[cell_idx].set_collation_type(ObCharset::get_default_collation(
                                             ObCharset::get_default_charset()));
      } break;
      case REF_TYPE: {
        if (record.data_.ref_type_ == 0) {
          cells[cell_idx].set_varchar("CHILD");
          cells[cell_idx].set_collation_type(ObCharset::get_default_collation(
                                               ObCharset::get_default_charset()));
        } else if (record.data_.ref_type_ == 1) {
          cells[cell_idx].set_varchar("FOLLOW");
          cells[cell_idx].set_collation_type(ObCharset::get_default_collation(
                                               ObCharset::get_default_charset()));
        }
      } break;
      case START_TS: {
        cells[cell_idx].set_timestamp(record.data_.start_ts_);
      } break;
      case END_TS: {
        cells[cell_idx].set_timestamp(record.data_.end_ts_);
      } break;
      case ELAPSE: {
        cells[cell_idx].set_int(record.data_.end_ts_ - record.data_.start_ts_);
      } break;
      case TAGS: {
        cells[cell_idx].set_lob_value(ObLongTextType,
                                      record.data_.tags_.ptr(),
                                      record.data_.tags_.length());
        cells[cell_idx].set_collation_type(ObCharset::get_default_collation(
                                             ObCharset::get_default_charset()));
      } break;
      case LOGS: {
        cells[cell_idx].set_lob_value(ObLongTextType,
                                      record.data_.logs_.ptr(),
                                      record.data_.logs_.length());
        cells[cell_idx].set_collation_type(ObCharset::get_default_collation(
                                             ObCharset::get_default_charset()));
      } break;
      default: {
        ret = OB_ERR_UNEXPECTED;
        SERVER_LOG(WARN, "invalid column id", K(ret), K(cell_idx), K(col_id));
      } break;
      }
    }
  }
  return ret;
}

int ObVirtualShowTrace::alloc_trace_rec(ObShowTraceRec *&rec)
{
  int ret = OB_SUCCESS;
  char *buf = NULL;
  if (NULL == (buf = static_cast<char *>(alloc_.alloc(sizeof(ObShowTraceRec))))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    if (REACH_TIME_INTERVAL(100 * 1000)) {
      SERVER_LOG(WARN, "alloc mem failed", K(sizeof(ObShowTraceRec)), K(ret));
    }
  } else {
    rec = new(buf) ObShowTraceRec();
  }
  return ret;
}

} // namespace observer
} // namespace oceanbase
