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

#ifndef OB_CSV_TABLE_ROW_ITER_H
#define OB_CSV_TABLE_ROW_ITER_H

#include "share/ob_i_tablet_scan.h"
#include "lib/file/ob_file.h"
#include "common/row/ob_row_iterator.h"
#include "storage/access/ob_dml_param.h"
#include "common/storage/ob_io_device.h"
#include "share/backup/ob_backup_struct.h"
#include "sql/engine/table/ob_external_table_access_service.h"
#include "sql/engine/ob_exec_context.h"

namespace oceanbase {
namespace sql {


class ObCSVIteratorState : public ObExternalIteratorState
{
public:
  static const int64_t MIN_EXTERNAL_TABLE_FILE_ID = 1;
  static const int64_t MIN_EXTERNAL_TABLE_LINE_NUMBER = 1;
  ObCSVIteratorState() :
    buf_(nullptr),
    buf_len_(OB_MALLOC_NORMAL_BLOCK_SIZE),
    pos_(nullptr),
    data_end_(nullptr),
    escape_buf_(nullptr),
    escape_buf_end_(nullptr),
    skip_lines_(0),
    cur_file_name_(),
    line_count_limit_(INT64_MAX),
    ip_port_buf_(NULL),
    ip_port_len_(0),
    need_expand_buf_(false),
    duration_(0),
    cur_file_size_(0),
    has_escape_(true) {}

  virtual void reuse() override
  {
    ObExternalIteratorState::reuse();
    // cur_file_id_ = MIN_EXTERNAL_TABLE_FILE_ID;
    // cur_line_number_ = MIN_EXTERNAL_TABLE_LINE_NUMBER;
    pos_ = buf_;
    data_end_ = buf_;
    skip_lines_ = 0;
    cur_file_name_.reset();
    line_count_limit_ = INT64_MAX;
    ip_port_len_ = 0;
    need_expand_buf_ = false;
    duration_ = 0;
    cur_file_size_ = 0;
    has_escape_ = true;
  }
  DECLARE_VIRTUAL_TO_STRING;
  char *buf_;
  int64_t buf_len_;
  const char *pos_;
  const char *data_end_;
  char *escape_buf_;
  char *escape_buf_end_;
  int64_t skip_lines_;
  ObString cur_file_name_;
  int64_t line_count_limit_;
  char *ip_port_buf_;
  int ip_port_len_;
  bool need_expand_buf_;
  int64_t duration_;
  int64_t cur_file_size_;
  bool has_escape_;
};

class ObCSVTableRowIterator : public ObExternalTableRowIterator {
public:
  static const int64_t MIN_EXTERNAL_TABLE_FILE_ID = 1;
  static const int64_t MIN_EXTERNAL_TABLE_LINE_NUMBER = 1;
  static const int max_ipv6_port_length = 100;
  ObCSVTableRowIterator() : bit_vector_cache_(NULL) {}
  virtual ~ObCSVTableRowIterator();
  virtual int init(const storage::ObTableScanParam *scan_param) override;
  int get_next_row() override;
  int get_next_rows(int64_t &count, int64_t capacity) override;

  virtual int get_next_row(ObNewRow *&row) override {
    UNUSED(row);
    return common::OB_ERR_UNEXPECTED;
  }

  virtual void reset() override;

  virtual int get_diagnosis_info(ObDiagnosisManager* diagnosis_manager) override 
  { 
    int ret = OB_SUCCESS;
    diagnosis_manager->set_cur_file_url(state_.cur_file_name_);
    diagnosis_manager->set_cur_line_number(state_.batch_first_row_line_num_);
    return ret;
  }

private:
  int expand_buf();
  int load_next_buf();
  int open_next_file();
  int get_next_file_and_line_number(const int64_t task_idx,
                                    common::ObString &file_url,
                                    int64_t &file_id,
                                    int64_t &part_id,
                                    int64_t &start_line,
                                    int64_t &end_line);
  int skip_lines();
  void release_buf();
  void dump_error_log(common::ObIArray<ObCSVGeneralParser::LineErrRec> &error_msgs);
  int record_err_for_select_data(int err_ret, const char *message, int64_t limit_num);
  int handle_error_msgs(common::ObIArray<ObCSVGeneralParser::LineErrRec> &error_msgs);
private:
  ObCSVIteratorState state_;
  ObBitVector *bit_vector_cache_;
  common::ObMalloc malloc_alloc_; //for internal data buffers
  common::ObArenaAllocator arena_alloc_;
  ObCSVGeneralParser parser_;
  ObExternalStreamFileReader file_reader_;
  ObSqlString url_;
  ObExpr *file_name_expr_;
  bool use_handle_batch_lines_ = false;
};


}
}

#endif // OB_CSV_TABLE_ROW_ITER_H
