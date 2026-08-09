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

#define USING_LOG_PREFIX  SQL_ENG

#include "ob_load_data_parser.h"
#include "common/ob_hex_utils_base.h"
#include "src/sql/engine/ob_exec_context.h"
#include "sql/resolver/cmd/ob_load_data_stmt.h"

using namespace oceanbase::sql;
using namespace oceanbase::common;

namespace oceanbase
{
namespace sql
{

const char INVALID_TERM_CHAR = '\xff';

const char * ObExternalFileFormat::FORMAT_TYPE_STR[] = {
  "CSV",
};
static_assert(array_elements(ObExternalFileFormat::FORMAT_TYPE_STR) == ObExternalFileFormat::MAX_FORMAT, "Not enough initializer for ObExternalFileFormat");

int ObCSVGeneralFormat::init_format(const ObDataInFileStruct &format,
                                    int64_t file_column_nums,
                                    ObCollationType file_cs_type)
{
  int ret = OB_SUCCESS;

  if (!ObCharset::is_valid_collation(file_cs_type)) {
    ret = OB_ERR_UNKNOWN_CHARSET;
    LOG_WARN("invalid charset", K(ret), K(file_cs_type));
  } else {
    cs_type_ = ObCharset::charset_type_by_coll(file_cs_type);
    file_column_nums_ = file_column_nums;
    field_enclosed_char_ = format.field_enclosed_char_;
    field_escaped_char_ = format.field_escaped_char_;
    field_term_str_ = format.field_term_str_;
    line_term_str_ = format.line_term_str_;
    line_start_str_ = format.line_start_str_;
    if (line_term_str_.empty() && !field_term_str_.empty()) {
      line_term_str_ = field_term_str_;
    }
  }
  return ret;
}


int ObCSVGeneralParser::init(const ObDataInFileStruct &format,
                             int64_t file_column_nums,
                             ObCollationType file_cs_type)
{
  int ret = OB_SUCCESS;

  if (OB_FAIL(format_.init_format(format, file_column_nums, file_cs_type))) {
  } else if (OB_FAIL(init_opt_variables())) {
  }

  return ret;
}

int ObCSVGeneralParser::init(const ObCSVGeneralFormat &format)
{
  int ret = OB_SUCCESS;

  format_ = format;

  if (OB_FAIL(init_opt_variables())) {
  }

  return ret;
}

int ObCSVGeneralParser::init_opt_variables()
{
  int ret = OB_SUCCESS;
  if (OB_SUCC(ret)) {
    opt_param_.line_term_c_ = format_.line_term_str_.empty() ? INVALID_TERM_CHAR : format_.line_term_str_[0];
    opt_param_.field_term_c_ = format_.field_term_str_.empty() ? INVALID_TERM_CHAR : format_.field_term_str_[0];
    opt_param_.max_term_ = std::max(static_cast<unsigned> (opt_param_.field_term_c_),
                                    static_cast<unsigned> (opt_param_.line_term_c_));
    opt_param_.min_term_ = std::min(static_cast<unsigned> (opt_param_.field_term_c_),
                                    static_cast<unsigned> (opt_param_.line_term_c_));
    opt_param_.is_filling_zero_to_empty_field_ = true;
    opt_param_.is_line_term_by_counting_field_ =
        0 == format_.line_term_str_.compare(format_.field_term_str_);
    opt_param_.is_same_escape_enclosed_ = (format_.field_enclosed_char_ == format_.field_escaped_char_);

    opt_param_.is_simple_format_ =
        !opt_param_.is_line_term_by_counting_field_
        && format_.field_term_str_.length() == 1
        && format_.line_term_str_.length() == 1
        && format_.line_start_str_.length() == 0
        && !opt_param_.is_same_escape_enclosed_
        && format_.field_enclosed_char_ == INT64_MAX;

  }

  if (OB_SUCC(ret) && OB_FAIL(fields_per_line_.prepare_allocate(format_.file_column_nums_))) {
    LOG_WARN("fail to allocate memory", K(ret), K(format_.file_column_nums_));
  }
  return ret;
}

int ObCSVGeneralParser::handle_irregular_line(int field_idx,
                          int line_no,
                          int output_line_no,
                          bool is_batch_mode,
                          common::ObIArray<LineErrRec> &errors) {
  int ret = OB_SUCCESS;
  LineErrRec rec;
  rec.err_code = field_idx > format_.file_column_nums_ ?
        OB_WARN_TOO_MANY_RECORDS : OB_WARN_TOO_FEW_RECORDS;
  rec.line_no = line_no;
  ret = errors.push_back(rec);
  if (is_batch_mode) {
    for (int i = field_idx, loc_idx = field_idx + output_line_no * format_.file_column_nums_;
        OB_SUCC(ret) && i < format_.file_column_nums_; ++i, ++loc_idx) {
      FieldValue &new_field = fields_per_line_.at(loc_idx);
      new_field = FieldValue();
      new_field.is_null_ = 1;
    }
  } else {
    for (int i = field_idx; OB_SUCC(ret) && i < format_.file_column_nums_; ++i) {
      FieldValue &new_field = fields_per_line_.at(i);
      new_field = FieldValue();
      new_field.is_null_ = 1;
    }
  }
  return ret;
}


int ObCSVGeneralFormat::to_json_kv_string(char *buf, const int64_t buf_len, int64_t &pos, bool into_outfile) const
{
  int ret = OB_SUCCESS;
  ObCStringHelper helper;
  OZ(J_COMMA());
  OZ(databuff_printf(buf, buf_len, pos, R"("%s":"%s")",
                     OPTION_NAMES[static_cast<int32_t>(ObCSVOptionsEnum::LINE_DELIMITER)],
                     helper.convert(ObHexStringWrap(line_term_str_))));
  OZ(J_COMMA());
  OZ(databuff_printf(buf, buf_len, pos, R"("%s":"%s")",
                     OPTION_NAMES[static_cast<int32_t>(ObCSVOptionsEnum::FIELD_DELIMITER)],
                     helper.convert(ObHexStringWrap(field_term_str_))));
  OZ(J_COMMA());
  OZ(databuff_printf(buf, buf_len, pos, R"("%s":%ld)",
                     OPTION_NAMES[static_cast<int32_t>(ObCSVOptionsEnum::ESCAPE)],
                     field_escaped_char_));
  OZ(J_COMMA());
  OZ(databuff_printf(buf, buf_len, pos, R"("%s":%ld)",
                     OPTION_NAMES[static_cast<int32_t>(ObCSVOptionsEnum::FIELD_OPTIONALLY_ENCLOSED_BY)],
                     field_enclosed_char_));
  OZ(J_COMMA());
  OZ(databuff_printf(buf, buf_len, pos, R"("%s":"%s")",
                     OPTION_NAMES[static_cast<int32_t>(ObCSVOptionsEnum::ENCODING)],
                     ObCharset::charset_name(cs_type_)));
  OZ(J_COMMA());
  OZ(databuff_printf(buf, buf_len, pos, R"("%s":%ld)",
                     OPTION_NAMES[static_cast<int32_t>(ObCSVOptionsEnum::SKIP_HEADER)],
                     skip_header_lines_));
  OZ(J_COMMA());
  OZ(databuff_printf(buf, buf_len, pos, R"("%s":%s)",
                     OPTION_NAMES[static_cast<int32_t>(ObCSVOptionsEnum::SKIP_BLANK_LINES)],
                     STR_BOOL(skip_blank_lines_)));
  OZ(J_COMMA());
  OZ(databuff_printf(buf, buf_len, pos, R"("%s":%s)",
                     OPTION_NAMES[static_cast<int32_t>(ObCSVOptionsEnum::TRIM_SPACE)],
                     STR_BOOL(trim_space_)));
  OZ(J_COMMA());
  OZ(databuff_printf(buf, buf_len, pos, R"("%s":)",
                     OPTION_NAMES[static_cast<int32_t>(ObCSVOptionsEnum::NULL_IF_EXETERNAL)]));
    OZ(J_ARRAY_START());
      for (int64_t i = 0; OB_SUCC(ret) && i < null_if_.count(); i++) {
        if (i != 0) {
          OZ(J_COMMA());
        }
        OZ(databuff_printf(buf, buf_len, pos, R"("%s")", helper.convert(ObHexStringWrap(null_if_.at(i)))));
      }
    OZ(J_ARRAY_END());
  OZ(J_COMMA());
  OZ(databuff_printf(buf, buf_len, pos, R"("%s":%s)",
                     OPTION_NAMES[static_cast<int32_t>(ObCSVOptionsEnum::EMPTY_FIELD_AS_NULL)],
                     STR_BOOL(empty_field_as_null_)));
  OZ(J_COMMA());
  OZ(databuff_printf(buf, buf_len, pos, R"("%s":"%s")",
                     OPTION_NAMES[static_cast<int32_t>(ObCSVOptionsEnum::COMPRESSION)],
                     compression_algorithm_to_string(compression_algorithm_)));
  if (into_outfile) {
    OZ(J_COMMA());
    OZ(databuff_printf(buf, buf_len, pos, R"("%s":%s)",
                       OPTION_NAMES[static_cast<int32_t>(ObCSVOptionsEnum::IS_OPTIONAL)],
                       STR_BOOL(is_optional_)));
    OZ(J_COMMA());
    OZ(databuff_printf(buf, buf_len, pos, R"("%s":"%s")",
                       OPTION_NAMES[static_cast<int32_t>(ObCSVOptionsEnum::FILE_EXTENSION)],
                       helper.convert(ObHexStringWrap(file_extension_))));
  }
  OZ(J_COMMA());
  OZ(databuff_printf(buf, buf_len, pos, R"("%s":%s)",
                      OPTION_NAMES[static_cast<int32_t>(ObCSVOptionsEnum::PARSE_HEADER)],
                      STR_BOOL(parse_header_)));
  OZ(J_COMMA());
  OZ(databuff_printf(buf, buf_len, pos, R"("%s":"%s")",
                      OPTION_NAMES[static_cast<int32_t>(ObCSVOptionsEnum::BINARY_FORMAT)],
                      binary_format_to_string(binary_format_)));
  OZ(J_COMMA());
  OZ(databuff_printf(buf, buf_len, pos, R"("%s":%s)",
                      OPTION_NAMES[static_cast<int32_t>(ObCSVOptionsEnum::IGNORE_LAST_EMPTY_COLUMN)],
                      STR_BOOL(ignore_last_empty_col_)));
  return ret;
}

int ObCSVGeneralFormat::load_from_json_data(json::Pair *&node, ObIAllocator &allocator)
{
  int ret = OB_SUCCESS;
  if (OB_SUCC(ret) && OB_NOT_NULL(node) && 0 == node->name_.case_compare(OPTION_NAMES[static_cast<int32_t>(ObCSVOptionsEnum::LINE_DELIMITER)])
      && json::JT_STRING == node->value_->get_type()) {
    ObObj obj;
    OZ(ObHexUtilsBase::unhex(node->value_->get_string(), allocator, obj));
    if (OB_SUCC(ret) && !obj.is_null()) {
      line_term_str_ = obj.get_string();
    }
    node = node->get_next();
  }
  if (OB_NOT_NULL(node) && 0 == node->name_.case_compare(OPTION_NAMES[static_cast<int32_t>(ObCSVOptionsEnum::FIELD_DELIMITER)])
      && json::JT_STRING == node->value_->get_type()) {
    ObObj obj;
    OZ(ObHexUtilsBase::unhex(node->value_->get_string(), allocator, obj));
    if (OB_SUCC(ret) && !obj.is_null()) {
      field_term_str_ = obj.get_string();
    }
    node = node->get_next();
  }
  if (OB_NOT_NULL(node) && 0 == node->name_.case_compare(OPTION_NAMES[static_cast<int32_t>(ObCSVOptionsEnum::ESCAPE)])
      && json::JT_NUMBER == node->value_->get_type()) {
    field_escaped_char_ = node->value_->get_number();
    node = node->get_next();
  }
  if (OB_NOT_NULL(node) && 0 == node->name_.case_compare(OPTION_NAMES[static_cast<int32_t>(ObCSVOptionsEnum::FIELD_OPTIONALLY_ENCLOSED_BY)])
      && json::JT_NUMBER == node->value_->get_type()) {
    field_enclosed_char_ = node->value_->get_number();
    node = node->get_next();
  }
  if (OB_NOT_NULL(node) && 0 == node->name_.case_compare(OPTION_NAMES[static_cast<int32_t>(ObCSVOptionsEnum::ENCODING)])
      && json::JT_STRING == node->value_->get_type()) {
    cs_type_ = ObCharset::charset_type(node->value_->get_string());
    node = node->get_next();
  }
  if (OB_NOT_NULL(node) && 0 == node->name_.case_compare(OPTION_NAMES[static_cast<int32_t>(ObCSVOptionsEnum::SKIP_HEADER)])
      && json::JT_NUMBER == node->value_->get_type()) {
    skip_header_lines_ = node->value_->get_number();
    node = node->get_next();
  }
  if (OB_NOT_NULL(node) && 0 == node->name_.case_compare(OPTION_NAMES[static_cast<int32_t>(ObCSVOptionsEnum::SKIP_BLANK_LINES)])) {
    if (json::JT_TRUE == node->value_->get_type()) {
      skip_blank_lines_ = true;
    } else {
      skip_blank_lines_ = false;
    }
    node = node->get_next();
  }
  if (OB_NOT_NULL(node) && 0 == node->name_.case_compare(OPTION_NAMES[static_cast<int32_t>(ObCSVOptionsEnum::TRIM_SPACE)])) {
    if (json::JT_TRUE == node->value_->get_type()) {
      trim_space_ = true;
    } else {
      trim_space_ = false;
    }
    node = node->get_next();
  }
  if (OB_SUCC(ret) && OB_NOT_NULL(node) && 0 == node->name_.case_compare(OPTION_NAMES[static_cast<int32_t>(ObCSVOptionsEnum::NULL_IF_EXETERNAL)])
      && json::JT_ARRAY == node->value_->get_type()) {
    const json::Array &it_array = node->value_->get_array();
    int64_t idx = 0;
    if (it_array.get_size() > 0
        && OB_FAIL(null_if_.allocate_array(allocator, it_array.get_size()))) {
      LOG_WARN("allocate array failed", K(ret));
    }
    for (auto it_tmp = it_array.get_first();
         OB_SUCC(ret) && it_tmp != it_array.get_header() && it_tmp != NULL;
         it_tmp = it_tmp->get_next()) {
      if (OB_UNLIKELY(json::JT_STRING != it_tmp->get_type())) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("null_if_ child is not string", K(ret), "type", it_tmp->get_type());
      } else {
        ObObj obj;
        OZ(ObHexUtilsBase::unhex(it_tmp->get_string(), allocator, obj));
        if (OB_SUCC(ret) && !obj.is_null()) {
          null_if_.at(idx++) = obj.get_string();
        }
      }
    }
    node = node->get_next();
  }
  if (OB_NOT_NULL(node) && 0 == node->name_.case_compare(OPTION_NAMES[static_cast<int32_t>(ObCSVOptionsEnum::EMPTY_FIELD_AS_NULL)])) {
    if (json::JT_TRUE == node->value_->get_type()) {
      empty_field_as_null_ = true;
    } else {
      empty_field_as_null_ = false;
    }
    node = node->get_next();
  }
  if (OB_NOT_NULL(node) && 0 == node->name_.case_compare(OPTION_NAMES[static_cast<int32_t>(ObCSVOptionsEnum::COMPRESSION)])
      && json::JT_STRING == node->value_->get_type()) {
    if (OB_FAIL(compression_algorithm_from_string(node->value_->get_string(), compression_algorithm_))) {
    } else {
      node = node->get_next();
    }
  }
  if (OB_NOT_NULL(node) && 0 == node->name_.case_compare(OPTION_NAMES[static_cast<int32_t>(ObCSVOptionsEnum::IS_OPTIONAL)])) {
    if (json::JT_TRUE == node->value_->get_type()) {
      is_optional_ = true;
    } else {
      is_optional_ = false;
    }
    node = node->get_next();
  }
  if (OB_NOT_NULL(node) && 0 == node->name_.case_compare(OPTION_NAMES[static_cast<int32_t>(ObCSVOptionsEnum::FILE_EXTENSION)])
      && json::JT_STRING == node->value_->get_type()) {
    ObObj obj;
    OZ (ObHexUtilsBase::unhex(node->value_->get_string(), allocator, obj));
    if (OB_SUCC(ret) && !obj.is_null()) {
      file_extension_ = obj.get_string();
    }
    node = node->get_next();
  }
  if (OB_NOT_NULL(node) && 0 == node->name_.case_compare(OPTION_NAMES[static_cast<int32_t>(ObCSVOptionsEnum::PARSE_HEADER)])) {
    if (json::JT_TRUE == node->value_->get_type()) {
      parse_header_ = true;
    } else {
      parse_header_ = false;
    }
    node = node->get_next();
  }
  if (OB_NOT_NULL(node) && 0 == node->name_.case_compare(OPTION_NAMES[static_cast<int32_t>(ObCSVOptionsEnum::BINARY_FORMAT)])
      && json::JT_STRING == node->value_->get_type()) {
    if (OB_FAIL(binary_format_from_string(node->value_->get_string(), binary_format_))) {
    } else {
      node = node->get_next();
    }
  }
  // the default value of ignore_last_empty_col_ is true
  // if ignore_last_empty_col_ is missing in ddl json, set ignore_last_empty_col_ to false for previous tables
  ignore_last_empty_col_ = false;
  if (OB_NOT_NULL(node) && 0 == node->name_.case_compare(OPTION_NAMES[static_cast<int32_t>(ObCSVOptionsEnum::IGNORE_LAST_EMPTY_COLUMN)])) {
    if (json::JT_TRUE == node->value_->get_type()) {
      ignore_last_empty_col_ = true;
    } else {
      ignore_last_empty_col_ = false;
    }
    node = node->get_next();
  }
  return ret;
}

int ObOriginFileFormat::to_json_kv_string(char *buf, const int64_t buf_len, int64_t &pos) const
{
  int ret = OB_SUCCESS;
  int64_t idx = 0;
  ObCStringHelper helper;
  OZ(J_COMMA());
  OZ(databuff_printf(buf, buf_len, pos, "\"%s\":\"%s\"", ORIGIN_FORMAT_STRING[idx++], helper.convert(ObHexStringWrap(origin_line_term_str_))));
  OZ(J_COMMA());
  OZ(databuff_printf(buf, buf_len, pos, "\"%s\":\"%s\"", ORIGIN_FORMAT_STRING[idx++], helper.convert(ObHexStringWrap(origin_field_term_str_))));
  OZ(J_COMMA());
  OZ(databuff_printf(buf, buf_len, pos, "\"%s\":\"%s\"", ORIGIN_FORMAT_STRING[idx++], helper.convert(ObHexStringWrap(origin_field_escaped_str_))));
  OZ(J_COMMA());
  OZ(databuff_printf(buf, buf_len, pos, "\"%s\":\"%s\"", ORIGIN_FORMAT_STRING[idx++], helper.convert(ObHexStringWrap(origin_field_enclosed_str_))));
  OZ(J_COMMA());
  OZ(databuff_printf(buf, buf_len, pos, "\"%s\":\"%s\"", ORIGIN_FORMAT_STRING[idx++], helper.convert(ObHexStringWrap(origin_null_if_str_))));
  return ret;
}

int ObOriginFileFormat::load_from_json_data(json::Pair *&node, ObIAllocator &allocator)
{
  int ret = OB_SUCCESS;
  int64_t idx = 0;
  if (OB_SUCC(ret) && OB_NOT_NULL(node)
      && 0 == node->name_.case_compare(ORIGIN_FORMAT_STRING[idx++])
      && json::JT_STRING == node->value_->get_type()) {
    ObObj obj;
    OZ (ObHexUtilsBase::unhex(node->value_->get_string(), allocator, obj));
    if (OB_SUCC(ret) && !obj.is_null()) {
      origin_line_term_str_ = obj.get_string();
    }
    node = node->get_next();
  }
  if (OB_SUCC(ret) && OB_NOT_NULL(node)
      && 0 == node->name_.case_compare(ORIGIN_FORMAT_STRING[idx++])
      && json::JT_STRING == node->value_->get_type()) {
    ObObj obj;
    OZ (ObHexUtilsBase::unhex(node->value_->get_string(), allocator, obj));
    if (OB_SUCC(ret) && !obj.is_null()) {
      origin_field_term_str_ = obj.get_string();
    }
    node = node->get_next();
  }
  if (OB_SUCC(ret) && OB_NOT_NULL(node)
      && 0 == node->name_.case_compare(ORIGIN_FORMAT_STRING[idx++])
      && json::JT_STRING == node->value_->get_type()) {
    ObObj obj;
    OZ (ObHexUtilsBase::unhex(node->value_->get_string(), allocator, obj));
    if (OB_SUCC(ret) && !obj.is_null()) {
      origin_field_escaped_str_ = obj.get_string();
    }
    node = node->get_next();
  }
  if (OB_SUCC(ret) && OB_NOT_NULL(node)
      && 0 == node->name_.case_compare(ORIGIN_FORMAT_STRING[idx++])
      && json::JT_STRING == node->value_->get_type()) {
    ObObj obj;
    OZ (ObHexUtilsBase::unhex(node->value_->get_string(), allocator, obj));
    if (OB_SUCC(ret) && !obj.is_null()) {
      origin_field_enclosed_str_ = obj.get_string();
    }
    node = node->get_next();
  }
  if (OB_SUCC(ret) && OB_NOT_NULL(node)
      && 0 == node->name_.case_compare(ORIGIN_FORMAT_STRING[idx++])
      && json::JT_STRING == node->value_->get_type()) {
    ObObj obj;
    OZ (ObHexUtilsBase::unhex(node->value_->get_string(), allocator, obj));
    if (OB_SUCC(ret) && !obj.is_null()) {
      origin_null_if_str_ = obj.get_string();
    }
    node = node->get_next();
  }
  return ret;
}

const char *compression_algorithm_to_string(ObCSVGeneralFormat::ObCSVCompression compression_algorithm)
{
  switch (compression_algorithm) {
    case ObCSVGeneralFormat::ObCSVCompression::NONE:    return "NONE";
    case ObCSVGeneralFormat::ObCSVCompression::AUTO:    return "AUTO";
    case ObCSVGeneralFormat::ObCSVCompression::GZIP:    return "GZIP";
    case ObCSVGeneralFormat::ObCSVCompression::DEFLATE: return "DEFLATE";
    case ObCSVGeneralFormat::ObCSVCompression::ZSTD:    return "ZSTD";
    default:                               return "INVALID";
  }
}

int compression_algorithm_from_string(ObString compression_name,
                                      ObCSVGeneralFormat::ObCSVCompression &compression_algorithm)
{
  int ret = OB_SUCCESS;

  if (compression_name.length() == 0 ||
      0 == compression_name.case_compare("none")) {
    compression_algorithm = ObCSVGeneralFormat::ObCSVCompression::NONE;
  } else if (0 == compression_name.case_compare("gzip")) {
    compression_algorithm = ObCSVGeneralFormat::ObCSVCompression::GZIP;
  } else if (0 == compression_name.case_compare("deflate")) {
    compression_algorithm = ObCSVGeneralFormat::ObCSVCompression::DEFLATE;
  } else if (0 == compression_name.case_compare("zstd")) {
    compression_algorithm = ObCSVGeneralFormat::ObCSVCompression::ZSTD;
  } else if (0 == compression_name.case_compare("auto")) {
    compression_algorithm = ObCSVGeneralFormat::ObCSVCompression::AUTO;
  } else {
    ret = OB_INVALID_ARGUMENT;
    compression_algorithm = ObCSVGeneralFormat::ObCSVCompression::INVALID;
  }
  return ret;
}

const char *binary_format_to_string(const ObCSVGeneralFormat::ObCSVBinaryFormat binary_format)
{
  switch (binary_format) {
    case ObCSVGeneralFormat::ObCSVBinaryFormat::HEX:    return "HEX";
    case ObCSVGeneralFormat::ObCSVBinaryFormat::BASE64:    return "BASE64";
    default: return "DEFAULT";
  }
}

int binary_format_from_string(const ObString binary_format_str,
                              ObCSVGeneralFormat::ObCSVBinaryFormat &binary_format) {
  int ret = OB_SUCCESS;

  if (binary_format_str.empty() || 0 == binary_format_str.case_compare("default")) {
    binary_format = ObCSVGeneralFormat::ObCSVBinaryFormat::DEFAULT;
  } else if (0 == binary_format_str.case_compare("hex")) {
    binary_format = ObCSVGeneralFormat::ObCSVBinaryFormat::HEX;
  } else if (0 == binary_format_str.case_compare("base64")) {
    binary_format = ObCSVGeneralFormat::ObCSVBinaryFormat::BASE64;
  } else {
    ret = OB_INVALID_ARGUMENT;
    binary_format = ObCSVGeneralFormat::ObCSVBinaryFormat::DEFAULT;
  }
  return ret;
}

int compression_algorithm_from_suffix(ObString filename,
                                      ObCSVGeneralFormat::ObCSVCompression &compression_algorithm)
{
  int ret = OB_SUCCESS;
  if (filename.suffix_match_ci(".gz")) {
    compression_algorithm = ObCSVGeneralFormat::ObCSVCompression::GZIP;
  } else if (filename.suffix_match_ci(".deflate")) {
    compression_algorithm = ObCSVGeneralFormat::ObCSVCompression::DEFLATE;
  } else if (filename.suffix_match_ci(".zst") || filename.suffix_match_ci(".zstd")) {
    compression_algorithm = ObCSVGeneralFormat::ObCSVCompression::ZSTD;
  } else {
    compression_algorithm = ObCSVGeneralFormat::ObCSVCompression::NONE;
  }
  return ret;
}
const char *compression_algorithm_to_suffix(ObCSVGeneralFormat::ObCSVCompression compression_algorithm)
{
  switch (compression_algorithm) {
    case ObCSVGeneralFormat::ObCSVCompression::GZIP:    return ".gz";
    case ObCSVGeneralFormat::ObCSVCompression::DEFLATE: return ".deflate";
    case ObCSVGeneralFormat::ObCSVCompression::ZSTD:    return ".zst";
    default:                                            return "";
  }
}

int ObExternalFileFormat::to_string_with_alloc(ObString &str, ObIAllocator &allocator, bool into_outfile) const
{
  int ret = OB_SUCCESS;
  char *buf = NULL;
  int64_t buf_len = DEFAULT_BUF_LENGTH / 2;
  int64_t pos = 0;
  do {
    buf_len *= 2;
    ret = OB_SUCCESS;
    if (OB_ISNULL(buf = static_cast<char*>(allocator.alloc(buf_len)))) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
      LOG_WARN("failed to alloc buf", K(ret), K(buf_len));
    } else if (OB_FAIL(to_string(buf, buf_len, pos, into_outfile))) {
    }
  } while (OB_SIZE_OVERFLOW == ret);
  OX(str.assign_ptr(buf, pos));
  return ret;
}

int ObExternalFileFormat::to_string(char *buf, const int64_t buf_len, int64_t &pos, bool into_outfile) const
{
  int ret = OB_SUCCESS;
  bool is_valid_format = format_type_ > INVALID_FORMAT && format_type_ < MAX_FORMAT;
  OZ(J_OBJ_START());
  OZ(databuff_print_kv(buf, buf_len, pos, "\"TYPE\"", is_valid_format ? ObExternalFileFormat::FORMAT_TYPE_STR[format_type_] : "INVALID"));
  switch (format_type_) {
    case CSV_FORMAT:
      OZ(csv_format_.to_json_kv_string(buf, buf_len, pos, into_outfile));
      OZ(origin_file_format_str_.to_json_kv_string(buf, buf_len, pos));
      break;
    default:
      // do nothing, format type can be invalid
      break;
  }
  OZ(J_OBJ_END());
  return ret;
}

int64_t ObExternalFileFormat::to_string(char *buf, const int64_t buf_len, bool into_outfile) const
{
  int64_t pos = 0;
  // ignore ret
  to_string(buf, buf_len, pos, into_outfile);
  return pos;
}

int ObExternalFileFormat::load_from_string(const ObString &str, ObIAllocator &allocator)
{
  int ret = OB_SUCCESS;
  json::Value *data = NULL;
  json::Parser parser;
  ObArenaAllocator temp_allocator;
  if (OB_UNLIKELY(str.empty())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("format string is empty", K(ret), K(str));
  } else if (OB_FAIL(parser.init(&temp_allocator))) {
  } else if (OB_FAIL(parser.parse(str.ptr(), str.length(), data))) {
  } else if (NULL == data || json::JT_OBJECT != data->get_type()) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("error json value", K(ret), KPC(data));
  } else {
    auto format_type_node = data->get_object().get_first();
    if (format_type_node->value_->get_type() != json::JT_STRING) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("unexpected json format", K(ret), K(str));
    } else {
      ObString format_type_str = format_type_node->value_->get_string();
      for (int i = 0; i < array_elements(ObExternalFileFormat::FORMAT_TYPE_STR); ++i) {
        if (format_type_str.case_compare(ObExternalFileFormat::FORMAT_TYPE_STR[i]) == 0) {
          format_type_ = static_cast<FormatType>(i);
          break;
        }
      }
      format_type_node = format_type_node->get_next();
      switch (format_type_) {
        case CSV_FORMAT:
          OZ (csv_format_.load_from_json_data(format_type_node, allocator));
          OZ (origin_file_format_str_.load_from_json_data(format_type_node, allocator));
          break;
        default:
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("invalid format type", K(ret), K(format_type_str));
          break;
      }
    }
  }
  return ret;
}

int ObExternalFileFormat::mock_gen_column_def(
    const share::schema::ObColumnSchemaV2 &column,
    ObIAllocator &allocator,
    ObString &def)
{
  int ret = OB_SUCCESS;
  ObSqlString temp_str;
  switch (format_type_) {
    case CSV_FORMAT: {
      uint64_t file_column_idx = column.get_column_id() - OB_APP_MIN_COLUMN_ID + 1;
      if (OB_FAIL(temp_str.append_fmt("%s%lu", N_EXTERNAL_FILE_COLUMN_PREFIX, file_column_idx))) {
      }
      break;
    }
    default: {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("unexpected format", K(ret), K(format_type_));
    }

  }
  if (OB_SUCC(ret)) {
    if (OB_FAIL(ob_write_string(allocator, temp_str.string(), def))) {
     }
  }

  return ret;
}

int ObExternalFileFormat::StringData::store_str(const ObString &str)
{
  return ob_write_string(allocator_, str, str_);
}

int ObExternalFileFormat::get_format_file_extension(FormatType format_type, ObString &file_extension)
{
  int ret  = OB_SUCCESS;
  switch (format_type) {
    case CSV_FORMAT: {
      file_extension.assign_ptr(csv_format_.file_extension_.ptr(), csv_format_.file_extension_.length());
      break;
    }
    default: {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("unexpected to get format file extension", K(ret), K(format_type_));
    }
  }
  return ret;
}

OB_DEF_SERIALIZE(ObExternalFileFormat::StringData)
{
  int ret = OB_SUCCESS;
  LST_DO_CODE(OB_UNIS_ENCODE, str_);
  return ret;
}

OB_DEF_DESERIALIZE(ObExternalFileFormat::StringData)
{
  int ret = OB_SUCCESS;
  ObString temp_str;
  LST_DO_CODE(OB_UNIS_DECODE, temp_str);
  if (OB_SUCC(ret)) {
    ret = store_str(temp_str);
  }
  return ret;
}

OB_DEF_SERIALIZE_SIZE(ObExternalFileFormat::StringData)
{
  int64_t len = 0;
  LST_DO_CODE(OB_UNIS_ADD_LEN, str_);
  return len;
}

}
}
