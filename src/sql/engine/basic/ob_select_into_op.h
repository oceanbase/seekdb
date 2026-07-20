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

#ifndef SRC_SQL_ENGINE_BASIC_OB_SELECT_INTO_OP_H_
#define SRC_SQL_ENGINE_BASIC_OB_SELECT_INTO_OP_H_

#include "sql/engine/ob_operator.h"
#include "lib/file/ob_file.h"
#include "share/io/ob_backup_storage_info.h"
#include "sql/engine/cmd/ob_load_data_parser.h"
#include "sql/engine/basic/ob_select_into_basic.h"
#include "sql/engine/basic/ob_external_file_writer.h"
#include "sql/resolver/dml/ob_select_stmt.h"

namespace oceanbase
{
namespace sql
{
class ObSelectIntoOpInput : public ObOpInput
{
  OB_UNIS_VERSION_V(1);
public:
  ObSelectIntoOpInput(ObExecContext &ctx, const ObOpSpec &spec)
  : ObOpInput(ctx, spec),
    task_id_(common::OB_INVALID_ID),
    sqc_id_(common::OB_INVALID_ID)
  {}
  virtual ~ObSelectIntoOpInput() = default;
  virtual int init(ObTaskInfo &task_info) override
  {
    UNUSED(task_info);
    return common::OB_SUCCESS;
  }
  virtual void reset() override {}
  virtual void set_task_id(int64_t task_id) { task_id_ = task_id; }
  virtual void set_sqc_id(int64_t sqc_id) { sqc_id_ = sqc_id; }
  int64_t get_task_id() const { return task_id_; }
  int64_t get_sqc_id() const { return sqc_id_; }

  int64_t task_id_;
  int64_t sqc_id_;
private:
  DISALLOW_COPY_AND_ASSIGN(ObSelectIntoOpInput);
};

class ObSelectIntoSpec : public ObOpSpec
{
  OB_UNIS_VERSION_V(1);
public:
  ObSelectIntoSpec(common::ObIAllocator &alloc, const ObPhyOperatorType type)
    : ObOpSpec(alloc, type),
      into_type_(T_INTO_OUTFILE),
      user_vars_(alloc),
      outfile_name_(),
      field_str_(),
      line_str_(),
      closed_cht_(),
      is_optional_(false),
      select_exprs_(alloc),
      is_single_(true),
      max_file_size_(DEFAULT_MAX_FILE_SIZE),
      escaped_cht_(),
      cs_type_(CS_TYPE_INVALID),
      parallel_(1),
      file_partition_expr_(NULL),
      buffer_size_(DEFAULT_BUFFER_SIZE),
      is_overwrite_(false),
      external_properties_(alloc),
      external_partition_(alloc)
  {
  }

  ObItemType into_type_;
  common::ObFixedArray<common::ObString, common::ObIAllocator> user_vars_;
  common::ObObj outfile_name_;
  common::ObObj field_str_; // FARM COMPAT WHITELIST FOR filed_str_: renamed
  common::ObObj line_str_;
  // Versions below 431 cannot execute select into in parallel, will not serialize operators, modifying closed_cht_type will not cause upgrade compatibility issues
  common::ObObj closed_cht_; // FARM COMPAT WHITELIST FOR closed_cht_: change type
  bool is_optional_;
  common::ObFixedArray<ObExpr*, common::ObIAllocator> select_exprs_;
  bool is_single_;
  int64_t max_file_size_;
  common::ObObj escaped_cht_;
  common::ObCollationType cs_type_;
  int64_t parallel_;
  sql::ObExpr* file_partition_expr_;
  int64_t buffer_size_;
  bool is_overwrite_;
  ObExternalFileFormat::StringData external_properties_;
  ObExternalFileFormat::StringData external_partition_;
  static const int64_t DEFAULT_MAX_FILE_SIZE = 256LL * 1024 * 1024;
  static const int64_t DEFAULT_BUFFER_SIZE = 1LL * 1024 * 1024;
};

class ObSelectIntoOp : public ObOperator
{
public:
  ObSelectIntoOp(ObExecContext &exec_ctx, const ObOpSpec &spec, ObOpInput *input)
    : ObOperator(exec_ctx, spec, input),
      top_limit_cnt_(INT64_MAX),
      is_first_(true),
      field_str_(),
      line_str_(),
      cs_type_(CS_TYPE_INVALID),
      basic_url_(),
      file_location_(IntoFileLocation::SERVER_DISK),
      write_offset_(0),
      data_writer_(NULL),
      char_enclose_(0),
      char_escape_('\\'),
      has_enclose_(false),
      is_optional_(false),
      has_escape_(true),
      has_lob_(false),
      has_json_(false),
      has_coll_(false),
      print_params_(),
      escape_printer_(),
      do_partition_(false),
      json_buf_(NULL),
      json_buf_len_(0),
      shared_buf_(NULL),
      shared_buf_len_(0),
      use_shared_buf_(false),
      has_compress_(false),
      partition_map_(),
      curr_partition_num_(0),
      external_properties_(),
      format_type_(ObExternalFileFormat::FormatType::CSV_FORMAT),
      block_id_(0),
      need_commit_(true)
  {
  }

  // cs_type of ObString in ObEscapeInfo should be dst_cs_type
  struct ObEscapePrinter
  {
    ObEscapePrinter():
      enclose_(), escape_(), zero_(), field_terminator_(), line_terminator_(), convert_replacer_(),
      need_enclose_(false), do_encode_(false), do_escape_(false), print_hex_(false),
      ignore_convert_failed_(false) {}
    int operator() (const ObString &src_str, const ob_wc_t &unicode_value) {
      int ret = OB_SUCCESS;
      ObString dst_str = src_str;
      int result_len = 0;
      char tmp_buf[ObCharset::MAX_MB_LEN];
      if (do_encode_) {
        ret = ObCharset::wc_mb(coll_type_, unicode_value, tmp_buf, ObCharset::MAX_MB_LEN, result_len);
        if (OB_SUCC(ret)) {
          dst_str = ObString(result_len, tmp_buf);
        } else if (ret == OB_ERR_INCORRECT_STRING_VALUE && ignore_convert_failed_) {
          dst_str = convert_replacer_;
          ret = OB_SUCCESS;
        }
      }
      if (OB_FAIL(ret) || !do_escape_ || print_hex_) {
      } else if (dst_str.compare(zero_) == 0
                 || dst_str.compare(enclose_) == 0
                 || dst_str.compare(escape_) == 0
                 || (!need_enclose_ && (dst_str.compare(field_terminator_) == 0
                                        || dst_str.compare(line_terminator_) == 0))) {
        ret = databuff_memcpy(buf_, buf_len_, pos_, escape_.length(), escape_.ptr());
      }
      if (OB_FAIL(ret)) {
      } else if (print_hex_) {
        ret = hex_print(dst_str.ptr(), dst_str.length(), buf_, buf_len_, pos_);
      } else if (do_escape_ && dst_str.compare(zero_) == 0) {
        char zero = '0';
        ret = databuff_memcpy(buf_, buf_len_, pos_, 1, &zero);
      } else {
        ret = databuff_memcpy(buf_, buf_len_, pos_, dst_str.length(), dst_str.ptr());
      }
      return ret;
    }
    ObString enclose_;
    ObString escape_;
    ObString zero_;
    ObString field_terminator_;
    ObString line_terminator_;
    ObString convert_replacer_;
    ObCollationType coll_type_;
    bool need_enclose_;
    bool do_encode_;
    bool do_escape_;
    bool print_hex_;
    bool ignore_convert_failed_;
    char *buf_;
    int64_t buf_len_;
    int64_t pos_;
  };

  virtual int inner_open() override;
  virtual int inner_close() override;
  virtual int inner_rescan() override;
  virtual int inner_get_next_row() override;
  virtual int inner_get_next_batch(const int64_t max_row_cnt) override;
  virtual void destroy() override;

private:
  int init_env_common();
  int init_csv_env();
  void set_csv_format_options();

  int get_row_str(const int64_t buf_len, bool is_first_row, char *buf, int64_t &pos);
  int into_dumpfile(ObExternalFileWriter *data_writer);
  int into_outfile(ObExternalFileWriter *data_writer);
  int into_outfile_batch_csv(const ObBatchRows &brs, ObExternalFileWriter *data_writer);
  int extract_fisrt_wchar_from_varhcar(const ObObj &obj, int32_t &wchar);
  int print_wchar_to_buf(char *buf,
                         const int64_t buf_len,
                         int64_t &pos,
                         int32_t wchar,
                         ObString &str,
                         ObCollationType coll_type);
  int print_field(const ObObj &obj, ObCsvFileWriter &data_writer);
  int print_lob_field(const ObObj &obj,
                      const ObExpr &expr,
                      const ObDatum &datum,
                      ObCsvFileWriter &data_writer);
  int get_buf(char* &buf, int64_t &buf_len, int64_t &pos, ObCsvFileWriter &data_writer);
  int use_shared_buf(ObCsvFileWriter &data_writer, char* &buf, int64_t &buf_len, int64_t &pos);
  int resize_buf(char* &buf,
                 int64_t &buf_len,
                 int64_t &pos,
                 int64_t curr_pos,
                 bool is_json = false);
  int resize_or_flush_shared_buf(ObCsvFileWriter &data_writer,
                                 char* &buf,
                                 int64_t &buf_len,
                                 int64_t &pos);
  int check_buf_sufficient(ObCsvFileWriter &data_writer,
                           char* &buf,
                           int64_t &buf_len,
                           int64_t &pos,
                           int64_t str_len);
  int write_obj_to_file(const ObObj &obj, ObCsvFileWriter &data_writer, bool need_escape = false);
  int print_str_or_json_with_escape(const ObObj &obj, ObCsvFileWriter &data_writer);
  int print_normal_obj_without_escape(const ObObj &obj, ObCsvFileWriter &data_writer);
  int print_json_to_json_buf(const ObObj &obj,
                             char* &buf,
                             int64_t &buf_len,
                             int64_t &pos,
                             ObCsvFileWriter &data_writer);
  int write_single_char_to_file(const char *wchar, ObCsvFileWriter &data_writer);
  int write_lob_to_file(const ObObj &obj,
                        const ObExpr &expr,
                        const ObDatum &datum,
                        ObCsvFileWriter &data_writer);
  int into_varlist();
  int calc_next_file_path(ObExternalFileWriter &data_writer);
  int calc_first_file_path(ObString &path);
  int calc_file_path_with_partition(ObString partition, ObExternalFileWriter &data_writer);
  int check_csv_file_size(ObCsvFileWriter &data_writer);
  int split_file(ObExternalFileWriter &data_writer);
  int prepare_escape_printer();
  int check_has_lob_or_json();
  int calc_url_and_set_access_info();
  int create_shared_buffer_for_data_writer();
  int create_the_only_data_writer(ObExternalFileWriter *&data_writer);
  int new_data_writer(ObExternalFileWriter *&data_writer);
  int check_secure_file_path(ObString file_name);
  int get_data_writer_for_partition(const ObString &partition_str, ObExternalFileWriter *&data_writer);
  char *get_json_buf() { return json_buf_; }
  int64_t get_json_buf_len() { return json_buf_len_; }
  char *get_shared_buf() { return shared_buf_; }
  int64_t get_shared_buf_len() { return shared_buf_len_; }

  bool file_need_split(int64_t file_size);

private:
  int64_t top_limit_cnt_;
  bool is_first_;
  ObObj field_str_;
  ObObj line_str_;
  ObObj file_name_;
  common::ObCollationType cs_type_;
  ObString basic_url_; // url without partition expr
  share::ObBackupStorageInfo access_info_;
  IntoFileLocation file_location_;
  int64_t write_offset_;
  ObExternalFileWriter* data_writer_;
  char char_enclose_;
  char char_escape_;
  bool has_enclose_;
  bool is_optional_;
  bool has_escape_;
  bool has_lob_;
  bool has_json_;
  bool has_coll_;
  common::ObObjPrintParams print_params_;
  ObEscapePrinter escape_printer_;
  bool do_partition_;
  char *json_buf_;  // json needs one more buffer to hold the string before escaping
  int64_t json_buf_len_;
  char *shared_buf_;
  int64_t shared_buf_len_;
  bool use_shared_buf_;
  bool has_compress_;
  typedef common::hash::ObHashMap<common::ObString, ObExternalFileWriter*, hash::NoPthreadDefendMode> ObPartitionWriterMap;
  ObPartitionWriterMap partition_map_;
  int curr_partition_num_;
  ObExternalFileFormat external_properties_;
  ObExternalFileFormat::FormatType format_type_;
  uint32_t block_id_;
  bool need_commit_;
  static const int64_t SHARED_BUFFER_SIZE = 2LL * 1024 * 1024;
  static const int64_t MAX_OSS_FILE_SIZE = 5LL * 1024 * 1024 * 1024;

};


}
}
#endif /* SRC_SQL_ENGINE_BASIC_OB_SELECT_INTO_OP_H_ */
