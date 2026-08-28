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

#define USING_LOG_PREFIX SQL_ENG

#include <cmath>
#include <memory>

#include "ob_select_into_op.h"
#include "sql/engine/cmd/ob_variable_set_executor.h"
#include "lib/charset/ob_charset_string_helper.h"
#include "sql/engine/px/ob_px_sqc_handler.h"
#include "sql/engine/expr/ob_expr_json_func_helper.h"
#include "common/udt/ob_collection_type.h"
#include "share/config/ob_server_config.h"
#include "share/ob_lob_access_utils.h"

namespace oceanbase
{
using namespace common;
namespace sql
{

OB_SERIALIZE_MEMBER(ObSelectIntoOpInput, task_id_, sqc_id_);
OB_SERIALIZE_MEMBER((ObSelectIntoSpec, ObOpSpec), into_type_, user_vars_, outfile_name_,
    field_str_, line_str_, closed_cht_, is_optional_, select_exprs_, is_single_, max_file_size_,
    escaped_cht_, cs_type_, parallel_, buffer_size_, is_overwrite_,
    external_properties_);


int ObSelectIntoOp::inner_open()
{
  int ret = OB_SUCCESS;
  ObSQLSessionInfo *session = NULL;
  if (OB_ISNULL(session = ctx_.get_my_session())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("get session failed", K(ret));
  } else {
    // since we call get_next_row in inner_open, we have to set opened_ first in avoid to a infinite loop.
    opened_ = true;
    if (OB_FAIL(session->get_sql_select_limit(top_limit_cnt_))) {
    }
  }
  if (OB_SUCC(ret) && !MY_SPEC.external_properties_.str_.empty()) {
    if (OB_FAIL(external_properties_.load_from_string(MY_SPEC.external_properties_.str_,
                                                      ctx_.get_allocator()))) {
    } else {
      format_type_ = external_properties_.format_type_;
    }
  }
  if (OB_SUCC(ret)) {
    switch (format_type_)
    {
      case ObExternalFileFormat::FormatType::CSV_FORMAT:
      {
        if (OB_FAIL(init_csv_env())) {
        }
        break;
      }
      default:
      {
        ret = OB_NOT_SUPPORTED;
        LOG_WARN("not support select into type", K(format_type_));
      }
    }
  }
  return ret;
}

int ObSelectIntoOp::init_csv_env()
{
  int ret = OB_SUCCESS;
  ObSQLSessionInfo *session = NULL;
  set_csv_format_options();
  if (OB_ISNULL(session = ctx_.get_my_session())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("get session failed", K(ret));
  } else if (OB_FAIL(init_env_common())) {
  } else if (OB_FAIL(prepare_escape_printer())) {
  } else {
    if (external_properties_.csv_format_.compression_algorithm_ != CsvCompressType::NONE) {
      has_compress_ = true;
    }
    // setup binary output format for bit/binary
    switch (external_properties_.csv_format_.binary_format_) {
      case ObCSVGeneralFormat::ObCSVBinaryFormat::DEFAULT:
        print_params_.binary_string_print_hex_ = false;
        break;
      case ObCSVGeneralFormat::ObCSVBinaryFormat::HEX:
        print_params_.binary_string_print_hex_ = true;
        break;
      case ObCSVGeneralFormat::ObCSVBinaryFormat::BASE64:
        print_params_.binary_string_print_base64_ = true;
        break;
      default:
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("failed to set csv binary output format", K(ret));
    }
    print_params_.tz_info_ = session->get_timezone_info();
    print_params_.use_memcpy_ = true;
    print_params_.cs_type_ = cs_type_;
  }
  //create buffer
  if (OB_SUCC(ret) && T_INTO_OUTFILE == MY_SPEC.into_type_ && OB_FAIL(create_shared_buffer_for_data_writer())) {
    LOG_WARN("failed to create buffer for data writer", K(ret));
  }
  return ret;
}

void ObSelectIntoOp::set_csv_format_options()
{
  if (MY_SPEC.external_properties_.str_.empty()) {
    field_str_ = MY_SPEC.field_str_;
    line_str_ = MY_SPEC.line_str_;
    has_enclose_ = MY_SPEC.closed_cht_.get_val_len() > 0;
    char_enclose_ = has_enclose_ ? MY_SPEC.closed_cht_.get_char().ptr()[0] : 0;
    is_optional_ = MY_SPEC.is_optional_;
    has_escape_ = MY_SPEC.escaped_cht_.get_val_len() > 0;
    char_escape_ = has_escape_ ? MY_SPEC.escaped_cht_.get_char().ptr()[0] : 0;
    cs_type_ = MY_SPEC.cs_type_;
  } else {
    is_optional_ = external_properties_.csv_format_.is_optional_;
    cs_type_ = ObCharset::get_default_collation(external_properties_.csv_format_.cs_type_);
    field_str_.set_varchar(external_properties_.csv_format_.field_term_str_);
    field_str_.set_collation_type(cs_type_);
    line_str_.set_varchar(external_properties_.csv_format_.line_term_str_);
    line_str_.set_collation_type(cs_type_);
    if (external_properties_.csv_format_.field_enclosed_char_ == INT64_MAX) { // null
      has_enclose_ = false;
      char_enclose_ = 0;
    } else {
      has_enclose_ = true;
      char_enclose_ = external_properties_.csv_format_.field_enclosed_char_;
    }
    if (external_properties_.csv_format_.field_escaped_char_ == INT64_MAX) { // null
      has_escape_ = false;
      char_escape_ = 0;
    } else {
      has_escape_ = true;
      char_escape_ = external_properties_.csv_format_.field_escaped_char_;
    }
  }
}

int ObSelectIntoOp::init_env_common()
{
  int ret = OB_SUCCESS;
  ObPhysicalPlanCtx *phy_plan_ctx = NULL;
  bool need_check = false;
  file_name_ = MY_SPEC.outfile_name_;
  if (OB_ISNULL(phy_plan_ctx = ctx_.get_physical_plan_ctx())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("get phy_plan_ctx failed", K(ret));
  } else if (OB_FAIL(ObSQLUtils::get_param_value(MY_SPEC.outfile_name_,
                                                 phy_plan_ctx->get_param_store(),
                                                 file_name_,
                                                 need_check))) {
  } else if (OB_FAIL(calc_outfile_path())) {
  } else if (OB_FAIL(check_has_lob_or_json())) {
  } else if (has_coll_ && MY_SPEC.into_type_ == T_INTO_VARIABLES) {
    ret = OB_NOT_SUPPORTED;
    LOG_USER_ERROR(OB_NOT_SUPPORTED, "select array/map into variables");
  }
  return ret;
}

//calc first data_writer.url_ and basic_url_
int ObSelectIntoOp::calc_outfile_path()
{
  int ret = OB_SUCCESS;
  const ObItemType into_type = MY_SPEC.into_type_;
  ObString path = file_name_.get_varchar().trim();
  if (T_INTO_OUTFILE == into_type && !MY_SPEC.is_single_ && OB_FAIL(calc_first_file_path(path))) {
    LOG_WARN("failed to calc first file path", K(ret));
  } else if (OB_FAIL(ob_write_string(ctx_.get_allocator(), path, basic_url_, true))) {
  }
  if (OB_SUCC(ret) && (T_INTO_OUTFILE == into_type || T_INTO_DUMPFILE == into_type)
      && OB_FAIL(check_secure_file_path(basic_url_))) {
    LOG_WARN("failed to check secure file path", K(ret));
  }
  return ret;
}
int ObSelectIntoOp::inner_get_next_row()
{
  int ret = 0 == top_limit_cnt_ ? OB_ITER_END : OB_SUCCESS;
  int64_t row_count = 0;
  const ObItemType into_type = MY_SPEC.into_type_;
  ObPhysicalPlanCtx *phy_plan_ctx = NULL;
  ObExternalFileWriter *data_writer = NULL;
  if (OB_ISNULL(phy_plan_ctx = ctx_.get_physical_plan_ctx())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("get phy_plan_ctx failed", K(ret));
  }
  if (OB_SUCC(ret) && ObExternalFileFormat::FormatType::CSV_FORMAT == format_type_
      && T_INTO_VARIABLES != into_type
      && OB_FAIL(create_the_only_data_writer(data_writer))) {
    LOG_WARN("failed to create the only data writer", K(ret));
  }
  while (OB_SUCC(ret) && row_count < top_limit_cnt_) {
    clear_evaluated_flag();
    if (OB_FAIL(child_->get_next_row())) {
      if (OB_LIKELY(OB_ITER_END == ret)) {
      } else {
        LOG_WARN("get next row failed", K(ret));
      }
    } else {
      ++row_count;
      if (T_INTO_VARIABLES == into_type) {
        if (OB_FAIL(into_varlist())) {
        }
      } else if (T_INTO_OUTFILE == into_type) {
        if (OB_FAIL(into_outfile(data_writer))) {
        }
      } else {
        if (OB_FAIL(into_dumpfile(data_writer))) {
        }
      }
    }
    if (OB_SUCC(ret) || OB_ITER_END == ret) { // if into user variables or into dumpfile, must be one row
      if ((T_INTO_VARIABLES == into_type || T_INTO_DUMPFILE == into_type) && row_count > 1) {
        ret = OB_ERR_TOO_MANY_ROWS;
        LOG_WARN("more than one row for into variables or into dumpfile", K(ret), K(row_count));
      }
    }
  } //end while
  if (OB_ITER_END == ret || OB_SUCC(ret)) { // set affected rows
    phy_plan_ctx->set_affected_rows(row_count);
  }
  if (OB_FAIL(ret) && OB_ITER_END != ret) {
    need_commit_ = false;
  }
  return ret;
}

int ObSelectIntoOp::inner_get_next_batch(const int64_t max_row_cnt)
{
  int ret = OB_SUCCESS;
  const ObBatchRows *child_brs = NULL;
  int64_t batch_size = min(max_row_cnt, MY_SPEC.max_batch_size_);
  int64_t row_count = 0;
  const ObItemType into_type = MY_SPEC.into_type_;
  ObPhysicalPlanCtx *phy_plan_ctx = NULL;
  ObExternalFileWriter *data_writer = NULL;
  bool stop_loop = false;
  bool is_iter_end = false;
  if (OB_ISNULL(phy_plan_ctx = ctx_.get_physical_plan_ctx())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("get phy_plan_ctx failed", K(ret));
  }
  if (OB_SUCC(ret) && T_INTO_VARIABLES != into_type
      && ObExternalFileFormat::FormatType::CSV_FORMAT == format_type_) {
    if (OB_FAIL(create_the_only_data_writer(data_writer))) {
    } else if (OB_ISNULL(data_writer)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("get unexpected null", K(ret));
    }
  }

  if (0 == top_limit_cnt_) {
    brs_.size_ = 0;
    brs_.end_ = true;
    stop_loop = true;
  }
  while (OB_SUCC(ret) && !stop_loop) {
    clear_evaluated_flag();
    int64_t rowkey_batch_size = min(batch_size, top_limit_cnt_ - row_count);
    if (OB_FAIL(child_->get_next_batch(rowkey_batch_size, child_brs))) {
    } else {
      brs_.size_ = child_brs->size_;
      brs_.end_ = child_brs->end_;
      is_iter_end = brs_.end_ && 0 == brs_.size_;
      if (brs_.size_ > 0) {
        brs_.skip_->deep_copy(*(child_brs->skip_), brs_.size_);
        row_count += brs_.size_ - brs_.skip_->accumulate_bit_cnt(brs_.size_);
        if (T_INTO_OUTFILE == into_type) {
          if (ObExternalFileFormat::FormatType::CSV_FORMAT == format_type_) {
            if (OB_FAIL(into_outfile_batch_csv(brs_, data_writer))) {
            }
          } else {
            ret = OB_NOT_SUPPORTED;
            LOG_WARN("not support to write into outfile format.", K(ret), K(format_type_));
          }
        } else {
          ObEvalCtx::BatchInfoScopeGuard guard(eval_ctx_);
          guard.set_batch_size(brs_.size_);
          for (int64_t i = 0; OB_SUCC(ret) && i < brs_.size_; i++) {
            if (brs_.skip_->contain(i)) {
              continue;
            }
            guard.set_batch_idx(i);
            if (T_INTO_VARIABLES == into_type) {
              if (OB_FAIL(into_varlist())) {
              }
            } else {
              if (OB_FAIL(into_dumpfile(data_writer))) {
              }
            }
          }
        }
      }
    }
    if (is_iter_end || row_count >= top_limit_cnt_) {
      stop_loop = true;
    }
    if (OB_SUCC(ret) || is_iter_end) { // if into user variables or into dumpfile, must be one row
      if ((T_INTO_VARIABLES == into_type || T_INTO_DUMPFILE == into_type) && row_count > 1) {
        ret = OB_ERR_TOO_MANY_ROWS;
        LOG_WARN("more than one row for into variables or into dumpfile", K(ret), K(row_count));
      }
    }
  } //end while
  if (OB_SUCC(ret)) { // set affected rows
    phy_plan_ctx->set_affected_rows(row_count);
  }
  if (OB_FAIL(ret)) {
    need_commit_ = false;
  }
  return ret;
}

int ObSelectIntoOp::inner_rescan()
{
  int ret = OB_SUCCESS;
  return ret;
}

int ObSelectIntoOp::inner_close()
{
  int ret = OB_SUCCESS;
  if (OB_NOT_NULL(data_writer_) && OB_FAIL(data_writer_->close_data_writer())) {
    LOG_WARN("failed to close data writer", K(ret));
  }
  return ret;
}

int ObSelectIntoOp::get_row_str(const int64_t buf_len,
                                bool is_first_row,
                                char *buf,
                                int64_t &pos)
{
  int ret = OB_SUCCESS;
  const ObObj &field_str = field_str_;
  char closed_cht = char_enclose_;
  const ObIArray<ObExpr*> &select_exprs = MY_SPEC.select_exprs_;
  if (!is_first_row && line_str_.is_varying_len_char_type()) { // lines terminated by "a"
    ret = databuff_printf(buf, buf_len, pos, "%.*s", line_str_.get_varchar().length(),
                         line_str_.get_varchar().ptr());
  }

  for (int i = 0 ; OB_SUCC(ret) && i < select_exprs.count() ; i++) {
    const ObExpr *expr = select_exprs.at(i);
    if (0 != closed_cht && (!is_optional_ || ob_is_string_type(expr->datum_meta_.type_))) {
      // closed by "a" (for all cell) or optionally by "a" (for string cell)
      if (OB_FAIL(databuff_printf(buf, buf_len, pos, "%c", closed_cht))) {
      }
    }
    if (OB_SUCC(ret)) {
      ObObj cell;
      ObDatum *datum = NULL;
      if (OB_FAIL(expr->eval(eval_ctx_, datum))) {
      } else if (OB_FAIL(datum->to_obj(cell, expr->obj_meta_))) {
      } else if (OB_FAIL(cell.print_plain_str_literal(buf, buf_len, pos))) {
      } else if (0 != closed_cht && (!is_optional_ || ob_is_string_type(expr->datum_meta_.type_))) {
        if (OB_FAIL(databuff_printf(buf, buf_len, pos, "%c", closed_cht))) {
        }
      }
      // field terminated by "a"
      if (OB_SUCC(ret) && i != select_exprs.count() - 1 && field_str.is_varying_len_char_type()) {
        if (OB_FAIL(databuff_printf(buf, buf_len, pos, "%.*s", field_str.get_varchar().length(), field_str.get_varchar().ptr()))) {
        }
      }
    }
  }

  return ret;
}

int ObSelectIntoOp::calc_first_file_path(ObString &path)
{
  int ret = OB_SUCCESS;
  ObSqlString file_name_with_suffix;
  ObString file_extension;
  ObSelectIntoOpInput *input = static_cast<ObSelectIntoOpInput*>(input_);
  ObString input_file_name = path;
  if (OB_ISNULL(input)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("op input is null", K(ret));
  } else if (input_file_name.length() == 0 || path.length() == 0) {
    ret = OB_INVALID_ARGUMENT;
    LOG_USER_ERROR(OB_INVALID_ARGUMENT, "invalid outfile path");
    LOG_WARN("invalid outfile path", K(ret));
  } else {
    if (input_file_name.ptr()[input_file_name.length() - 1] == '/'){
      OZ(file_name_with_suffix.append_fmt("%.*sdata", input_file_name.length(), input_file_name.ptr()));
    } else {
      OZ(file_name_with_suffix.append_fmt("%.*s", input_file_name.length(), input_file_name.ptr()));
    }
    if (MY_SPEC.parallel_ > 1) {
      OZ(file_name_with_suffix.append_fmt("_%ld_%ld_%d", input->sqc_id_, input->task_id_, 0));
    } else {
      OZ(file_name_with_suffix.append_fmt("_%d", 0));
    }
    OZ(external_properties_.get_format_file_extension(format_type_, file_extension));
    if (!file_extension.empty() && file_extension.ptr()[0] != '.') {
      OZ(file_name_with_suffix.append("."));
    }
    OZ(file_name_with_suffix.append(file_extension));
    if (format_type_ == ObExternalFileFormat::FormatType::CSV_FORMAT) {
      OZ(file_name_with_suffix.append(compression_algorithm_to_suffix(external_properties_.csv_format_.compression_algorithm_)));
    }
    if (OB_SUCC(ret) && OB_FAIL(ob_write_string(ctx_.get_allocator(), file_name_with_suffix.string(), path))) {
      LOG_WARN("failed to write string", K(ret));
    }
  }
  return ret;
}

int ObSelectIntoOp::calc_next_file_path(ObExternalFileWriter &data_writer)
{
  int ret = OB_SUCCESS;
  ObSqlString url_with_suffix;
  ObString file_path;
  data_writer.split_file_id_++;
  if (data_writer.split_file_id_ <= 0) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("get unexpected split file id", K(ret));
  } else if (MY_SPEC.is_single_) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("get unexpected single value", K(ret));
  } else {
    file_path = data_writer.url_.split_on(data_writer.url_.reverse_find('_'));
    if (OB_FAIL(url_with_suffix.assign(file_path))) {
    } else if (OB_FAIL(url_with_suffix.append_fmt("_%ld", data_writer.split_file_id_))) {
    }
    ObString file_extension;
    OZ(external_properties_.get_format_file_extension(format_type_, file_extension));
    if (!file_extension.empty() && file_extension.ptr()[0] != '.') {
      OZ(url_with_suffix.append("."));
    }
    OZ(url_with_suffix.append(file_extension));
    if (format_type_ == ObExternalFileFormat::FormatType::CSV_FORMAT) {
      OZ(url_with_suffix.append(compression_algorithm_to_suffix(external_properties_.csv_format_.compression_algorithm_)));
    }
    if (OB_SUCC(ret) && OB_FAIL(ob_write_string(ctx_.get_allocator(),
                                                url_with_suffix.string(),
                                                data_writer.url_, true))) {
      LOG_WARN("failed to write string", K(ret));
    }
  }
  return ret;
}

int ObSelectIntoOp::split_file(ObExternalFileWriter &data_writer)
{
  int ret = OB_SUCCESS;
  if (ObExternalFileFormat::FormatType::CSV_FORMAT == format_type_) {
    ObCsvFileWriter *csv_data_writer = static_cast<ObCsvFileWriter*>(&data_writer);
    if (OB_ISNULL(csv_data_writer)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("get unexpected null data writer", K(ret));
    } else if (!use_shared_buf_ && OB_FAIL(csv_data_writer->flush_buf())) {
      LOG_WARN("failed to flush buffer", K(ret));
    } else if (has_lob_ && use_shared_buf_ && OB_FAIL(csv_data_writer->flush_shared_buf(shared_buf_))) {
      // To ensure the integrity of each line in the file, when there is a lob, the shared buffer may not contain a complete line
      // Therefore the remaining content in the shared buffer also needs to be flushed to the current file, in this case, the max_file_size limit cannot be strictly enforced
      LOG_WARN("failed to flush shared buffer", K(ret));
    }
  }
  if (OB_FAIL(ret)) {
  } else if (OB_FAIL(data_writer.close_file())) {
  } else if (OB_FAIL(calc_next_file_path(data_writer))) {
  }
  return ret;
}

int ObSelectIntoOp::check_csv_file_size(ObCsvFileWriter &data_writer)
{
  int ret = OB_SUCCESS;
  int64_t curr_bytes = data_writer.get_file_size();
  int64_t curr_bytes_exclude_curr_line = data_writer.get_curr_bytes_exclude_curr_line();
  int64_t curr_line_len = curr_bytes - curr_bytes_exclude_curr_line;
  bool has_split = false;
  bool has_use_shared_buf = use_shared_buf_;
  if (has_compress_ && OB_ISNULL(data_writer.get_compress_stream_writer())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("get unexpected null compress stream writer", K(ret));
  } else if (!(has_lob_ && has_use_shared_buf) && curr_bytes_exclude_curr_line == 0) {
  } else if (file_need_split(curr_bytes)) {
    if (OB_FAIL(split_file(data_writer))) {
    } else {
      has_split = true;
    }
  }
  if (OB_SUCC(ret)) {
    if (has_lob_ && has_use_shared_buf) {
      if (!has_compress_) {
        data_writer.set_write_bytes(has_split ? 0 : curr_bytes);
      }
      data_writer.reset_curr_line_len();
    } else {
      if (!has_compress_) {
        data_writer.set_write_bytes(has_split ? curr_line_len : curr_bytes);
      }
    }
    if (has_compress_ && has_split) {
      data_writer.get_compress_stream_writer()->reuse();
    }
    data_writer.update_last_line_pos();
  }
  return ret;
}

int ObSelectIntoOp::get_buf(char* &buf, int64_t &buf_len, int64_t &pos, ObCsvFileWriter &data_writer)
{
  int ret = OB_SUCCESS;
  buf = use_shared_buf_ ? get_shared_buf() : data_writer.get_buf();
  buf_len = use_shared_buf_ ? get_shared_buf_len() : data_writer.get_buf_len();
  pos = data_writer.get_curr_pos();
  if (OB_ISNULL(buf) && !use_shared_buf_ && OB_FAIL(use_shared_buf(data_writer, buf, buf_len, pos))) {
    LOG_WARN("failed to use shared buffer", K(ret));
  } else if (OB_ISNULL(buf)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("buf should not be null", K(ret));
  }
  return ret;
}

int ObSelectIntoOp::use_shared_buf(ObCsvFileWriter &data_writer,
                                   char* &buf,
                                   int64_t &buf_len,
                                   int64_t &pos)
{
  int ret = OB_SUCCESS;
  int64_t curr_pos = data_writer.get_curr_pos();
  if (!use_shared_buf_ && data_writer.get_last_line_pos() == 0) {
    if (OB_NOT_NULL(data_writer.get_buf()) && curr_pos > 0) {
      MEMCPY(shared_buf_, data_writer.get_buf(), curr_pos);
    }
    use_shared_buf_ = true;
    buf = shared_buf_;
    buf_len = shared_buf_len_;
    pos = curr_pos;
  } else {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("last line should be flushed before this line copied", K(ret));
  }
  return ret;
}

int ObSelectIntoOp::resize_buf(char* &buf,
                               int64_t &buf_len,
                               int64_t &pos,
                               int64_t curr_pos,
                               bool is_json)
{
  int ret = OB_SUCCESS;
  int64_t new_buf_len = buf_len * 2;
  char* new_buf = NULL;
  if (OB_ISNULL(new_buf = static_cast<char*>(ctx_.get_allocator().alloc(new_buf_len)))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    LOG_WARN("failed to allocate buffer", K(ret), K(new_buf_len));
  } else if (!is_json) {
    if (curr_pos > 0) {
      MEMCPY(new_buf, shared_buf_, curr_pos);
    }
    shared_buf_ = new_buf;
    shared_buf_len_ = new_buf_len;
  } else {
    json_buf_ = new_buf;
    json_buf_len_ = new_buf_len;
  }
  if (OB_SUCC(ret)) {
    buf = new_buf;
    buf_len = new_buf_len;
    pos = is_json ? 0 : curr_pos;
  }
  return ret;
}

int ObSelectIntoOp::resize_or_flush_shared_buf(ObCsvFileWriter &data_writer,
                                               char* &buf,
                                               int64_t &buf_len,
                                               int64_t &pos)
{
  int ret = OB_SUCCESS;
  if (!use_shared_buf_) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("get invalid argument", K(use_shared_buf_), K(ret));
  } else if (has_lob_ && data_writer.get_curr_pos() > 0) {
    if (OB_FAIL(data_writer.flush_shared_buf(shared_buf_, true))) {
    } else {
      pos = 0;
    }
  } else if (OB_FAIL(resize_buf(buf, buf_len, pos, data_writer.get_curr_pos()))) {
  }
  return ret;
}

int ObSelectIntoOp::check_buf_sufficient(ObCsvFileWriter &data_writer,
                                         char* &buf,
                                         int64_t &buf_len,
                                         int64_t &pos,
                                         int64_t str_len)
{
  int ret = OB_SUCCESS;
  if (buf_len < str_len * 1.1) {
    if (OB_FAIL(data_writer.flush_buf())) {
    } else if (OB_FAIL(use_shared_buf(data_writer, buf, buf_len, pos))) {
    }
  }
  return ret;
}

int ObSelectIntoOp::write_obj_to_file(const ObObj &obj, ObCsvFileWriter &data_writer, bool need_escape)
{
  int ret = OB_SUCCESS;
  // binary collation do not require to escape when encode with base64/hex
  if (obj.get_collation_type() == CS_TYPE_BINARY &&
      (print_params_.binary_string_print_hex_ || print_params_.binary_string_print_base64_)) {
    need_escape = false;
  }

  if ((obj.is_string_type() || obj.is_json() || obj.is_collection_sql_type()) && need_escape) {
    if (OB_FAIL(print_str_or_json_with_escape(obj, data_writer))) {
    }
  } else if (OB_FAIL(print_normal_obj_without_escape(obj, data_writer))) {
  }
  return ret;
}

int ObSelectIntoOp::print_str_or_json_with_escape(const ObObj &obj, ObCsvFileWriter &data_writer)
{
  int ret = OB_SUCCESS;
  char* buf = NULL;
  int64_t buf_len = 0;
  int64_t pos = 0;
  ObCharsetType src_type = ObCharset::charset_type_by_coll(obj.get_collation_type());
  ObCharsetType dst_type = ObCharset::charset_type_by_coll(cs_type_);
  escape_printer_.do_encode_ = !(src_type == CHARSET_BINARY || src_type == dst_type
                                 || src_type == CHARSET_INVALID);
  escape_printer_.need_enclose_ = has_enclose_ && !obj.is_null();
  escape_printer_.do_escape_ = true;
  escape_printer_.print_hex_ = obj.get_collation_type() == CS_TYPE_BINARY
                               && print_params_.binary_string_print_hex_;
  ObString str_to_escape;
  ObEvalCtx::TempAllocGuard tmp_alloc_g(eval_ctx_);
  common::ObArenaAllocator &temp_allocator = tmp_alloc_g.get_allocator();
  const common::ObLobReadOptions *lob_read_options = nullptr;
  if (OB_FAIL(get_buf(escape_printer_.buf_, escape_printer_.buf_len_, escape_printer_.pos_, data_writer))) {
  } else if (obj.is_json() || obj.is_collection_sql_type()) {
    ObObj inrow_obj = obj;
    if (obj.is_lob_storage()
        && OB_FAIL(get_exec_ctx().get_lob_read_options(lob_read_options))) {
      LOG_WARN("failed to get LOB read options", K(ret));
    } else if (obj.is_lob_storage()
        && OB_FAIL(ObTextStringIter::convert_outrow_lob_to_inrow_templob(
                       obj, inrow_obj, lob_read_options, &temp_allocator))) {
      LOG_WARN("failed to convert outrow lobs", K(ret), K(obj));
    } else if (obj.is_collection_sql_type()) {
      ObSubSchemaValue sub_meta;
      if (OB_FAIL((get_exec_ctx().get_sqludt_meta_by_subschema_id(obj.get_meta().get_subschema_id(), sub_meta)))) {
      } else {
        print_params_.coll_meta_ = reinterpret_cast<ObSqlCollectionInfo *>(sub_meta.value_);
      }
    }
    if (OB_FAIL(ret)) {
    } else if (OB_FAIL(print_json_to_json_buf(inrow_obj, buf, buf_len, pos, data_writer))) {
    } else {
      str_to_escape.assign_ptr(buf, pos);
      escape_printer_.do_encode_ = false;
    }
  } else {
    str_to_escape = obj.get_varchar();
  }
  if (OB_SUCC(ret) && !use_shared_buf_ && OB_FAIL(check_buf_sufficient(data_writer,
                                                                       escape_printer_.buf_,
                                                                       escape_printer_.buf_len_,
                                                                       escape_printer_.pos_,
                                                                       str_to_escape.length()))) {
    LOG_WARN("failed to check if buf is sufficient", K(ret));
  }
  if (OB_SUCC(ret) && !use_shared_buf_) {
    if (OB_FAIL(ObFastStringScanner::foreach_char(str_to_escape,
                                                  src_type,
                                                  escape_printer_,
                                                  escape_printer_.do_encode_,
                                                  escape_printer_.ignore_convert_failed_))) {
      if (OB_SIZE_OVERFLOW != ret) {
        LOG_WARN("failed to print plain str", K(ret), K(src_type), K(escape_printer_.do_encode_));
      } else if (OB_FAIL(data_writer.flush_buf())) {
      } else if (OB_FALSE_IT(escape_printer_.pos_ = data_writer.get_curr_pos())) {
      } else if (OB_FAIL(ObFastStringScanner::foreach_char(str_to_escape,
                                                           src_type,
                                                           escape_printer_,
                                                           escape_printer_.do_encode_,
                                                           escape_printer_.ignore_convert_failed_))) {
        if (OB_SIZE_OVERFLOW != ret) {
          LOG_WARN("failed to print plain str", K(ret), K(src_type), K(escape_printer_.do_encode_));
        } else if (OB_FAIL(use_shared_buf(data_writer,
                                          escape_printer_.buf_,
                                          escape_printer_.buf_len_,
                                          escape_printer_.pos_))) {
        }
      }
    }
  }
  if (OB_SUCC(ret) && use_shared_buf_) {
    do {
      if (OB_FAIL(ObFastStringScanner::foreach_char(str_to_escape,
                                                    src_type,
                                                    escape_printer_,
                                                    escape_printer_.do_encode_,
                                                    escape_printer_.ignore_convert_failed_))) {
      }
    } while (OB_SIZE_OVERFLOW == ret && OB_SUCC(resize_or_flush_shared_buf(data_writer,
                                                                           escape_printer_.buf_,
                                                                           escape_printer_.buf_len_,
                                                                           escape_printer_.pos_)));
    if (OB_FAIL(ret)) {
    }
  }
  if (OB_SUCC(ret)) {
    data_writer.set_curr_pos(escape_printer_.pos_);
  }

  return ret;
}

int ObSelectIntoOp::print_normal_obj_without_escape(const ObObj &obj, ObCsvFileWriter &data_writer)
{
  int ret = OB_SUCCESS;
  char* buf = NULL;
  int64_t buf_len = 0;
  int64_t pos = 0;
  OZ(get_buf(buf, buf_len, pos, data_writer));
  if (OB_SUCC(ret) && !use_shared_buf_) {
    if (OB_FAIL(obj.print_plain_str_literal(buf, buf_len, pos, print_params_))) {
      if (OB_SIZE_OVERFLOW != ret) {
        LOG_WARN("failed to print obj", K(ret));
      } else if (OB_FAIL(data_writer.flush_buf())) {
      } else if (OB_FALSE_IT(pos = data_writer.get_curr_pos())) {
      } else if (OB_FAIL(obj.print_plain_str_literal(buf, buf_len, pos, print_params_))) {
        if (OB_SIZE_OVERFLOW != ret) {
          LOG_WARN("failed to print obj", K(ret));
        } else if (OB_FAIL(use_shared_buf(data_writer, buf, buf_len, pos))) {
        }
      }
    }
  }
  if (OB_SUCC(ret) && use_shared_buf_) {
    do {
      if (OB_FAIL(obj.print_plain_str_literal(buf, buf_len, pos, print_params_))) {
      }
    } while (OB_SIZE_OVERFLOW == ret
             && OB_SUCC(resize_or_flush_shared_buf(data_writer, buf, buf_len, pos)));
    if (OB_FAIL(ret)) {
    }
  }
  if (OB_SUCC(ret)) {
    data_writer.set_curr_pos(pos);
  }
  return ret;
}

int ObSelectIntoOp::print_json_to_json_buf(const ObObj &obj,
                                           char* &buf,
                                           int64_t &buf_len,
                                           int64_t &pos,
                                           ObCsvFileWriter &data_writer)
{
  int ret = OB_SUCCESS;
  buf = get_json_buf();
  buf_len = get_json_buf_len();
  pos = 0;
  do {
    if (OB_FAIL(obj.print_plain_str_literal(buf, buf_len, pos, print_params_))) {
    }
  } while (OB_SIZE_OVERFLOW == ret
           && OB_SUCC(resize_buf(buf, buf_len, pos, data_writer.get_curr_pos(), true)));
  if (OB_FAIL(ret)) {
  }
  return ret;
}

int ObSelectIntoOp::write_lob_to_file(const ObObj &obj,
                                      const ObExpr &expr,
                                      const ObDatum &datum,
                                      ObCsvFileWriter &data_writer)
{
  int ret = OB_SUCCESS;
  ObCharsetType src_type = ObCharset::charset_type_by_coll(obj.get_collation_type());
  ObCharsetType dst_type = ObCharset::charset_type_by_coll(cs_type_);
  escape_printer_.need_enclose_ = has_enclose_;
  escape_printer_.do_encode_ = !(src_type == CHARSET_BINARY || src_type == dst_type
                                 || src_type == CHARSET_INVALID);
  escape_printer_.do_escape_ = has_escape_;
  escape_printer_.print_hex_ = obj.get_collation_type() == CS_TYPE_BINARY
                               && print_params_.binary_string_print_hex_;
  ObDatumMeta input_meta = expr.datum_meta_;
  ObTextStringIterState state;
  ObString src_block_data;
  ObTextStringIter lob_iter(input_meta.type_, input_meta.cs_type_, datum.get_string(),
                            expr.obj_meta_.has_lob_header());
  ObEvalCtx::TempAllocGuard tmp_alloc_g(eval_ctx_);
  common::ObArenaAllocator &temp_allocator = tmp_alloc_g.get_allocator();
  int64_t truncated_len = 0;
  bool stop_when_truncated = false;
  OZ(ObTextStringHelper::build_text_iter(
      lob_iter, eval_ctx_.exec_ctx_, &temp_allocator));
  OZ(get_buf(escape_printer_.buf_, escape_printer_.buf_len_, escape_printer_.pos_, data_writer));
  // When truncated_len == src_block_data.length() when truncated length equals source block data length
  // Indicates that the current foreach_char is processing only invalid data at the end of the lob, i.e., truncated data from the previous round, to avoid infinite loops
  while (OB_SUCC(ret)
         && (state = lob_iter.get_next_block(src_block_data)) == TEXTSTRING_ITER_NEXT) {
    // outrow lob will only be false on the last iteration, inrow lob iterates only once, and is false
    stop_when_truncated = (truncated_len != src_block_data.length()) && lob_iter.is_outrow_lob();
    if (!use_shared_buf_ && OB_FAIL(check_buf_sufficient(data_writer,
                                                         escape_printer_.buf_,
                                                         escape_printer_.buf_len_,
                                                         escape_printer_.pos_,
                                                         src_block_data.length()))) {
      LOG_WARN("failed to check if buf is sufficient", K(ret));
    }
    if (OB_SUCC(ret) && !use_shared_buf_) {
      if (OB_FAIL(ObFastStringScanner::foreach_char(src_block_data,
                                                    src_type,
                                                    escape_printer_,
                                                    escape_printer_.do_encode_,
                                                    escape_printer_.ignore_convert_failed_,
                                                    stop_when_truncated,
                                                    &truncated_len))) {
        if (OB_ERR_DATA_TRUNCATED == ret && stop_when_truncated) {
          lob_iter.set_reserved_byte_len(truncated_len);
          ret = OB_SUCCESS;
        } else if (OB_SIZE_OVERFLOW != ret) {
          LOG_WARN("failed to print lob", K(ret));
        } else if (OB_FAIL(data_writer.flush_buf())) {
        } else if (OB_FALSE_IT(escape_printer_.pos_ = data_writer.get_curr_pos())) {
        } else if (OB_FAIL(ObFastStringScanner::foreach_char(src_block_data,
                                                             src_type,
                                                             escape_printer_,
                                                             escape_printer_.do_encode_,
                                                             escape_printer_.ignore_convert_failed_,
                                                             stop_when_truncated,
                                                             &truncated_len))) {
          if (OB_ERR_DATA_TRUNCATED == ret && stop_when_truncated) {
            lob_iter.set_reserved_byte_len(truncated_len);
            ret = OB_SUCCESS;
          } else if (OB_SIZE_OVERFLOW != ret) {
            LOG_WARN("failed to print lob", K(ret));
          } else if (OB_FAIL(use_shared_buf(data_writer,
                                            escape_printer_.buf_,
                                            escape_printer_.buf_len_,
                                            escape_printer_.pos_))) {
          }
        }
      }
    }
    if (OB_SUCC(ret) && use_shared_buf_) {
      if (OB_FAIL(ObFastStringScanner::foreach_char(src_block_data,
                                                    src_type,
                                                    escape_printer_,
                                                    escape_printer_.do_encode_,
                                                    escape_printer_.ignore_convert_failed_,
                                                    stop_when_truncated,
                                                    &truncated_len))) {
        if (OB_ERR_DATA_TRUNCATED == ret && stop_when_truncated) {
          lob_iter.set_reserved_byte_len(truncated_len);
          ret = OB_SUCCESS;
        } else if (OB_SIZE_OVERFLOW != ret) {
          LOG_WARN("failed to print lob", K(ret));
        } else if (OB_FAIL(data_writer.flush_shared_buf(shared_buf_, true))) {
        } else if (OB_FALSE_IT(escape_printer_.pos_ = 0)) {
        } else if (OB_FAIL(ObFastStringScanner::foreach_char(src_block_data,
                                                             src_type,
                                                             escape_printer_,
                                                             escape_printer_.do_encode_,
                                                             escape_printer_.ignore_convert_failed_,
                                                             stop_when_truncated,
                                                             &truncated_len))) {
          if (OB_ERR_DATA_TRUNCATED == ret && stop_when_truncated) {
            lob_iter.set_reserved_byte_len(truncated_len);
            ret = OB_SUCCESS;
          } else {
            LOG_WARN("failed to print lob", K(ret), K(src_block_data.length()), K(shared_buf_len_),
            K(data_writer.get_curr_pos()), K(escape_printer_.buf_len_), K(escape_printer_.pos_));
          }
        }
      }
    }
    data_writer.set_curr_pos(escape_printer_.pos_);
  }
  if (OB_FAIL(ret)) {
  } else if (state != TEXTSTRING_ITER_NEXT && state != TEXTSTRING_ITER_END) {
    ret = (lob_iter.get_inner_ret() != OB_SUCCESS) ?
          lob_iter.get_inner_ret() : OB_INVALID_DATA;
    LOG_WARN("iter state invalid", K(ret), K(state), K(lob_iter));
  }
  return ret;
}

int ObSelectIntoOp::write_single_char_to_file(const char *wchar, ObCsvFileWriter &data_writer)
{
  int ret = OB_SUCCESS;
  char* buf = NULL;
  int64_t buf_len = 0;
  int64_t pos = 0;
  OZ(get_buf(buf, buf_len, pos, data_writer));
  if (OB_SUCC(ret) && !use_shared_buf_) {
    if (pos < buf_len) {
      MEMCPY(buf + pos, wchar, 1);
      data_writer.set_curr_pos(pos + 1);
    } else if (OB_FAIL(data_writer.flush_buf())) {
    } else if (OB_FALSE_IT(pos = data_writer.get_curr_pos())) {
    } else if (pos < buf_len) {
      MEMCPY(buf + pos, wchar, 1);
      data_writer.set_curr_pos(pos + 1);
    } else if (OB_FAIL(use_shared_buf(data_writer, buf, buf_len, pos))) {
    } 
  }
  if (OB_SUCC(ret) && use_shared_buf_) {
    if (pos < buf_len) {
      MEMCPY(buf + pos, wchar, 1);
      data_writer.set_curr_pos(pos + 1);
    } else if (OB_FAIL(resize_or_flush_shared_buf(data_writer, buf, buf_len, pos))) {
    } else if (pos < buf_len) {
      MEMCPY(buf + pos, wchar, 1);
      data_writer.set_curr_pos(pos + 1);
    } else {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("get unexpected error", K(ret));
    }
  }
  return ret;
}

int ObSelectIntoOp::print_lob_field(const ObObj &obj,
                                    const ObExpr &expr,
                                    const ObDatum &datum,
                                    ObCsvFileWriter &data_writer)
{
  int ret = OB_SUCCESS;
  if (has_enclose_) {
    OZ(write_single_char_to_file(&char_enclose_, data_writer));
  }
  OZ(write_lob_to_file(obj, expr, datum, data_writer));
  if (has_enclose_) {
    OZ(write_single_char_to_file(&char_enclose_, data_writer));
  }
  return ret;
}

int ObSelectIntoOp::print_field(const ObObj &obj, ObCsvFileWriter &data_writer)
{
  int ret = OB_SUCCESS;
  char char_n = 'N';
  const bool need_enclose = has_enclose_ && !obj.is_null()
                            && (!is_optional_ || obj.is_string_type() || obj.is_collection_sql_type()
                                || obj.is_json() || obj.is_geometry() || obj.is_date()
                                || obj.is_time() || obj.is_timestamp() || obj.is_datetime()
                                || obj.is_mysql_date() || obj.is_mysql_datetime());
  if (need_enclose) {
    OZ(write_single_char_to_file(&char_enclose_, data_writer));
  }
  if (!has_escape_) {
    OZ(write_obj_to_file(obj, data_writer, false));
  } else if (obj.is_null()) {
    OZ(write_single_char_to_file(&char_escape_, data_writer));
    OZ(write_single_char_to_file(&char_n, data_writer));
  } else {
    OZ(write_obj_to_file(obj, data_writer, true));
  }
  if (need_enclose) {
    OZ(write_single_char_to_file(&char_enclose_, data_writer));
  }
  return ret;
}

int ObSelectIntoOp::into_outfile(ObExternalFileWriter *data_writer)
{
  int ret = OB_SUCCESS;
  const ObIArray<ObExpr*> &select_exprs = MY_SPEC.select_exprs_;
  ObDatum *datum = NULL;
  ObObj obj;
  ObCsvFileWriter *csv_data_writer = NULL;
  if (OB_ISNULL(csv_data_writer = static_cast<ObCsvFileWriter *>(data_writer))) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("get unexpected null data writer", K(ret));
  }
  for (int64_t i = 0; OB_SUCC(ret) && i < select_exprs.count(); ++i) {
    if (OB_ISNULL(select_exprs.at(i))) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("select expr is unexpected null", K(ret));
    } else if (OB_FAIL(select_exprs.at(i)->eval(eval_ctx_, datum))) {
    } else if (OB_ISNULL(datum)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("datum is unexpected null", K(ret));
    } else if (OB_FAIL(datum->to_obj(obj,
                                     select_exprs.at(i)->obj_meta_,
                                     select_exprs.at(i)->obj_datum_map_))) {
    } else if (!ob_is_text_tc(select_exprs.at(i)->obj_meta_.get_type()) || obj.is_null()) {
      OZ(print_field(obj, *csv_data_writer));
    } else { // text tc
      OZ(print_lob_field(obj, *select_exprs.at(i), *datum, *csv_data_writer));
    }
    // print field terminator
    if (OB_SUCC(ret) && i != select_exprs.count() - 1) {
      OZ(write_obj_to_file(field_str_, *csv_data_writer));
    }
  }
  // print line terminator
  OZ(write_obj_to_file(line_str_, *csv_data_writer));
  // check if need split file
  OZ(check_csv_file_size(*csv_data_writer));
  // clear shared buffer
  OZ(csv_data_writer->flush_shared_buf(shared_buf_));
  if (has_compress_) {
    OZ(csv_data_writer->flush_buf());
  }
  return ret;
}

int ObSelectIntoOp::into_outfile_batch_csv(const ObBatchRows &brs, ObExternalFileWriter *data_writer)
{
  int ret = OB_SUCCESS;
  const ObIArray<ObExpr*> &select_exprs = MY_SPEC.select_exprs_;
  ObArray<ObDatumVector> datum_vectors;
  ObDatum *datum = NULL;
  ObObj obj;
  ObCsvFileWriter *csv_data_writer = NULL;
  for (int64_t i = 0; OB_SUCC(ret) && i < select_exprs.count(); ++i) {
    if (OB_FAIL(select_exprs.at(i)->eval_batch(eval_ctx_, *brs.skip_, brs.size_))) {
    } else if (OB_FAIL(datum_vectors.push_back(select_exprs.at(i)->locate_expr_datumvector(eval_ctx_)))) {
    }
  }
  for (int64_t i = 0; OB_SUCC(ret) && i < brs.size_; ++i) {
    if (brs.skip_->contain(i)) {
      // do nothing
    } else if (OB_ISNULL(csv_data_writer = static_cast<ObCsvFileWriter *>(data_writer))) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("get unexpected null data writer", K(ret));
    } else if (has_compress_ && OB_ISNULL(csv_data_writer->get_compress_stream_writer())
               && OB_FAIL(csv_data_writer->init_compress_writer(ctx_.get_allocator(),
                                                                external_properties_.csv_format_.compression_algorithm_,
                                                                MY_SPEC.buffer_size_))) {
      LOG_WARN("failed to init compress stream writer", K(ret));
    } else {
      for (int64_t col_idx = 0; OB_SUCC(ret) && col_idx < select_exprs.count(); ++col_idx) {
        if (OB_ISNULL(datum = datum_vectors.at(col_idx).at(i))) {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("datum is unexpected null", K(ret));
        } else if (OB_FAIL(datum->to_obj(obj,
                                         select_exprs.at(col_idx)->obj_meta_,
                                         select_exprs.at(col_idx)->obj_datum_map_))) {
        } else if (!ob_is_text_tc(select_exprs.at(col_idx)->obj_meta_.get_type()) || obj.is_null()) {
          OZ(print_field(obj, *csv_data_writer));
        } else { // text tc
          OZ(print_lob_field(obj, *select_exprs.at(col_idx), *datum, *csv_data_writer));
        }
        // print field terminator
        if (OB_SUCC(ret) && col_idx != select_exprs.count() - 1) {
          OZ(write_obj_to_file(field_str_, *csv_data_writer));
        }
      }
      // print line terminator
      OZ(write_obj_to_file(line_str_, *csv_data_writer));
      // check if need split file
      OZ(check_csv_file_size(*csv_data_writer));
      // clear shared buffer
      OZ(csv_data_writer->flush_shared_buf(shared_buf_));
      if (has_compress_) {
        OZ(csv_data_writer->flush_buf());
      }
    }
  }
  return ret;
}

bool ObSelectIntoOp::file_need_split(int64_t file_size)
{
  return !MY_SPEC.is_single_ && file_size > MY_SPEC.max_file_size_;
}


int ObSelectIntoOp::into_dumpfile(ObExternalFileWriter *data_writer)
{
  int ret = OB_SUCCESS;
  char buf[MAX_VALUE_LENGTH];
  int64_t buf_len = MAX_VALUE_LENGTH;
  int64_t pos = 0;
  if (OB_ISNULL(data_writer)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("get unexpected null", K(ret));
  } else if (OB_FAIL(get_row_str(buf_len, is_first_, buf, pos))) {
  } else if (is_first_) { // create file
    if (OB_FAIL(data_writer->file_appender_.create(file_name_.get_varchar(), true))) {
    } else {
      is_first_ = false;
    }
  }
  if (OB_SUCC(ret)) {
    if (OB_FAIL(data_writer->file_appender_.append(buf, pos, false))) {
    } else {
      //do nothing
    }
  }
  return ret;
}

int ObSelectIntoOp::into_varlist()
{
  int ret = OB_SUCCESS;
  const ObIArray<ObExpr*> &select_exprs = MY_SPEC.select_exprs_;
  const ObIArray<ObString> &user_vars = MY_SPEC.user_vars_;
  ObArenaAllocator lob_tmp_allocator("LobTmp", OB_MALLOC_NORMAL_BLOCK_SIZE);
  const common::ObLobReadOptions *lob_read_options = nullptr;
  if (select_exprs.count() != user_vars.count()) {
    ret = OB_ERR_COLUMN_SIZE;
    LOG_WARN("user vars count should be equal to select exprs count" , K(ret),
            K(select_exprs.count()), K(user_vars.count()));
  } else {
    for (int i = 0 ; i < user_vars.count(); ++i) {
      const ObString &var_name = user_vars.at(i);
      ObObj obj;
      ObDatum *datum = NULL;
      if (OB_FAIL(select_exprs.at(i)->eval(eval_ctx_, datum))) {
      } else if (OB_FAIL(datum->to_obj(obj, select_exprs.at(i)->obj_meta_))) {
      } else if (obj.is_lob_storage()
          && OB_FAIL(ctx_.get_lob_read_options(lob_read_options))) {
        LOG_WARN("failed to get LOB read options", K(ret));
      } else if (obj.is_lob_storage()
          // outrow lob can not be assigned to user var, so convert outrow to inrow lob
          // user var has independent memory, so using temporary memory here is fine
          && OB_FAIL(ObTextStringIter::convert_outrow_lob_to_inrow_templob(
                         obj, obj, lob_read_options, &lob_tmp_allocator,
                         true/*allow_persist_inrow*/))) {
        LOG_WARN("convert outrow to inrow lob failed", K(ret), K(obj));
      } else if (OB_FAIL(ObVariableSetExecutor::set_user_variable(obj, var_name,
                  ctx_.get_my_session()))) {
      }
    }
  }
  return ret;
}

int ObSelectIntoOp::extract_fisrt_wchar_from_varhcar(const ObObj &obj, int32_t &wchar)
{
  int ret = OB_SUCCESS;
  int32_t length = 0;
  if (obj.is_varying_len_char_type()) {
    ObString str = obj.get_varchar();
    if (str.length() > 0) {
      ret = ObCharset::mb_wc(obj.get_collation_type(), str.ptr(), str.length(), length, wchar);
    }
  }
  return ret;
}

int ObSelectIntoOp::print_wchar_to_buf(char *buf,
                                       const int64_t buf_len,
                                       int64_t &pos,
                                       int32_t wchar,
                                       ObString &str,
                                       ObCollationType coll_type)
{
  int ret = OB_SUCCESS;
  int result_len = 0;
  if (OB_FAIL(ObCharset::wc_mb(coll_type, wchar, buf + pos, buf_len - pos, result_len))) {
  } else {
    str = ObString(result_len, buf + pos);
    pos += result_len;
  }
  return ret;
}

int ObSelectIntoOp::prepare_escape_printer()
{
  int ret = OB_SUCCESS;
  int64_t pos = 0;
  char *buf = NULL;
  int64_t buf_len = 6 * ObCharset::MAX_MB_LEN;
  // mb->wc
  int32_t wchar_enclose = char_enclose_;
  int32_t wchar_escape = char_escape_;
  int32_t wchar_field = 0;
  int32_t wchar_line = 0;
  int32_t wchar_zero = '\0';
  int32_t wchar_replace = 0;
  OZ(extract_fisrt_wchar_from_varhcar(field_str_, wchar_field));
  OZ(extract_fisrt_wchar_from_varhcar(line_str_, wchar_line));
  OZ(ObCharset::get_replace_character(cs_type_, wchar_replace));
  // wc->mb
  if (OB_ISNULL(buf = static_cast<char*>(ctx_.get_allocator().alloc(buf_len)))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    LOG_WARN("failed to allocate buffer", K(ret), K(buf_len));
  }
  if (has_enclose_) {
    OZ(print_wchar_to_buf(buf, buf_len, pos, wchar_enclose, escape_printer_.enclose_, cs_type_));
  }
  if (has_escape_) {
    OZ(print_wchar_to_buf(buf, buf_len, pos, wchar_escape, escape_printer_.escape_, cs_type_));
  }
  OZ(print_wchar_to_buf(buf, buf_len, pos, wchar_zero, escape_printer_.zero_, cs_type_));
  OZ(print_wchar_to_buf(buf, buf_len, pos, wchar_field, escape_printer_.field_terminator_, cs_type_));
  OZ(print_wchar_to_buf(buf, buf_len, pos, wchar_line, escape_printer_.line_terminator_, cs_type_));
  OZ(print_wchar_to_buf(buf, buf_len, pos, wchar_replace, escape_printer_.convert_replacer_, cs_type_));
  escape_printer_.coll_type_ = cs_type_;
  escape_printer_.ignore_convert_failed_ = true; // todo@linyi provide user-defined interface
  return ret;
}

int ObSelectIntoOp::check_has_lob_or_json()
{
  int ret = OB_SUCCESS;
  const ObIArray<ObExpr*> &select_exprs = MY_SPEC.select_exprs_;
  for (int64_t i = 0; OB_SUCC(ret) && (!has_lob_ || !has_json_ || !has_coll_) && i < select_exprs.count(); ++i) {
    if (OB_ISNULL(select_exprs.at(i))) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("select expr is unexpected null", K(ret));
    } else if (ob_is_text_tc(select_exprs.at(i)->obj_meta_.get_type())) {
      has_lob_ = true;
    } else if (ob_is_json_tc(select_exprs.at(i)->obj_meta_.get_type())) {
      has_json_ = true;
    } else if (ob_is_collection_sql_type(select_exprs.at(i)->obj_meta_.get_type())) {
      has_coll_ = true;
    }
  }
  return ret;
}

int ObSelectIntoOp::create_shared_buffer_for_data_writer()
{
  int ret = OB_SUCCESS;
  shared_buf_len_ = has_lob_ ? (5 * SHARED_BUFFER_SIZE) : SHARED_BUFFER_SIZE;
  if (OB_ISNULL(shared_buf_ = static_cast<char*>(ctx_.get_allocator().alloc(shared_buf_len_)))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    LOG_WARN("failed to allocate buffer", K(ret), K(shared_buf_len_));
  }
  if (OB_SUCC(ret) && (has_json_ || has_coll_) && has_escape_) {
    json_buf_len_ = OB_MALLOC_MIDDLE_BLOCK_SIZE;
    if (OB_ISNULL(json_buf_ = static_cast<char*>(ctx_.get_allocator().alloc(json_buf_len_)))) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
      LOG_WARN("failed to allocate buffer", K(ret), K(json_buf_len_));
    }
  }
  return ret;
}

int ObSelectIntoOp::check_secure_file_path(ObString file_name)
{
  int ret = OB_SUCCESS;
  ObString file_path = file_name.split_on(file_name.reverse_find('/'));
  char full_path_buf[PATH_MAX+1];
  char *actual_path = nullptr;
  ObSqlString sql_str;
  ObString secure_file_priv;
  
  if (OB_FAIL(sql_str.append(file_path.empty() ? "." : file_path))) {
    LOG_WARN("failed to append string", K(ret));
#ifdef _WIN32
  } else if (OB_ISNULL(actual_path = _fullpath(full_path_buf, sql_str.ptr(), PATH_MAX))) {
#else
  } else if (OB_ISNULL(actual_path = realpath(sql_str.ptr(), full_path_buf))) {
#endif
    ret = OB_FILE_NOT_EXIST;
    LOG_WARN("file not exist", K(ret), K(sql_str));
  } else if (OB_FAIL(ObSchemaUtils::get_runtime_varchar_variable(*GCTX.schema_service_,
                                                                SYS_VAR_SECURE_FILE_PRIV,
                                                                ctx_.get_allocator(),
                                                                secure_file_priv))) {
  } else if (OB_FAIL(ObResolverUtils::check_secure_path(secure_file_priv, actual_path))) {
    LOG_WARN("failed to check secure path", K(ret), K(secure_file_priv));
    if (OB_ERR_NO_PRIVILEGE == ret) {
      ret = OB_ERR_NO_PRIV_DIRECT_PATH_ACCESS;
      LOG_ERROR("failed to check secure path", K(ret), K(secure_file_priv));
    }
  }
  return ret;
}

int ObSelectIntoOp::create_the_only_data_writer(ObExternalFileWriter *&data_writer)
{
  int ret = OB_SUCCESS;
  ObCsvFileWriter *csv_data_writer = NULL;
  if (OB_FAIL(new_data_writer(data_writer))) {
  } else if (OB_ISNULL(data_writer)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("get unexpected null", K(ret));
  } else {
    data_writer->url_ = basic_url_;
    data_writer_ = data_writer;
  }
  if (OB_FAIL(ret)) {
  } else if (T_INTO_OUTFILE == MY_SPEC.into_type_ && MY_SPEC.is_single_
             && OB_FAIL(data_writer->open_file())) {
    LOG_WARN("failed to open file", K(ret));
  } else if (ObExternalFileFormat::FormatType::CSV_FORMAT == format_type_ && MY_SPEC.buffer_size_ > 0) {
    csv_data_writer = static_cast<ObCsvFileWriter*>(data_writer);
    if (OB_FAIL(csv_data_writer->alloc_buf(ctx_.get_allocator(), MY_SPEC.buffer_size_))) {
    }
  }
  return ret;
}

int ObSelectIntoOp::new_data_writer(ObExternalFileWriter *&data_writer)
{
  int ret = OB_SUCCESS;
  void *ptr = NULL;
  switch (format_type_)
  {
    case ObExternalFileFormat::FormatType::CSV_FORMAT:
    {
      if (OB_ISNULL(ptr = ctx_.get_allocator().alloc(sizeof(ObCsvFileWriter)))) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_WARN("failed to allocate data writer", K(ret), K(sizeof(ObCsvFileWriter)));
      } else {
        data_writer = new(ptr) ObCsvFileWriter(use_shared_buf_, has_compress_, has_lob_);
      }
      break;
    }
    default:
    {
      ret = OB_NOT_SUPPORTED;
      LOG_WARN("not support select into type", K(format_type_));
    }
  }
  return ret;
}

void ObSelectIntoOp::destroy()
{
  if (OB_NOT_NULL(data_writer_)) {
    data_writer_->~ObExternalFileWriter();
  }
  external_properties_.~ObExternalFileFormat();
  ObOperator::destroy();
}

}
}
