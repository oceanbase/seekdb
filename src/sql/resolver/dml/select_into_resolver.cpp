// Copyright (c) 2026 OceanBase.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#define USING_LOG_PREFIX SQL_RESV
#include "sql/resolver/dml/ob_select_resolver.h"
#include "sql/resolver/parameterization_result.h"
#include "sql/resolver/cmd/ob_load_data_stmt.h"
#include "sql/engine/cmd/ob_load_data_parser.h"
#include "sql/code_generator/ob_column_index_provider.h"
#include "sql/code_generator/ob_static_engine_expr_cg.h"
#include "sql/ob_sql_utils.h"
#include "sql/engine/ob_exec_context.h"
#include "share/config/ob_config_helper.h"

namespace oceanbase::sql {
using namespace common;

namespace {
int resolve_output_size(const peg::Node &node, int64_t &size)
{
  int ret = OB_SUCCESS;
  size = 0;
  const auto literal = peg::expression_literal(node);
  if (literal.kind != peg::LiteralKind::String) {
    uint64_t integer = 0;
    OZ(ObResolverUtils::resolve_unsigned_integer(node, "Output size", integer));
    OX(size = static_cast<int64_t>(integer));
  } else {
    ObSqlString text;
    bool valid = false;
    if (OB_FAIL(text.append(syntax_text(literal.text).ptr(), literal.text.size()))) {
    } else {
      size = ObConfigCapacityParser::get(text.ptr(), valid, false, true);
      if (!valid) { ret = OB_ERR_PARSE_SQL; }
    }
  }
  if (OB_SUCC(ret) && size < 0) {
    ret = OB_INVALID_ARGUMENT;
    LOG_USER_ERROR(OB_INVALID_ARGUMENT, "size, size value should not be negative");
  }
  return ret;
}
}

int ObSelectResolver::resolve_output_literal(const peg::Node &node, ObObj &value)
{
  int ret = OB_SUCCESS;
  const ParameterBinding *binding = params_.parameterization_ == nullptr
      ? nullptr : params_.parameterization_->find(&node);
  if (binding != nullptr) {
    value.set_unknown(binding->parameter_index);
  } else {
    const auto literal = peg::expression_literal(node);
    ObString owned;
    if (literal.binary_notation != peg::BinaryNotation::None) {
      const auto bytes = peg::decode_binary_literal(literal.text, literal.binary_notation);
      OZ(ob_write_string(*allocator_,
          ObString(bytes.size(), reinterpret_cast<const char *>(bytes.data())), owned));
      value.set_varchar(owned);
      value.set_collation_type(CS_TYPE_BINARY);
    } else {
      OZ(ob_write_string(*allocator_, syntax_text(literal.text), owned));
      value.set_varchar(owned);
      value.set_collation_type(params_.session_info_->get_local_collation_connection());
    }
  }
  return ret;
}

int ObSelectResolver::resolve_output_fields(rust::Slice<const peg::Node> fields, ObSelectIntoItem &into_item)
{
  int ret = OB_SUCCESS;
  for (const auto &node : fields) {
    const auto option = peg::load_format_option(node);
    if (option.kind == peg::LoadFormatKind::FieldTerminated) {
      // A later TERMINATED BY replaces the preceding field-option error in the legacy resolver.
      ret = resolve_output_literal(option.value, into_item.field_str_);
    } else {
      ObObj value;
      const int copy_ret = resolve_output_literal(option.value, value);
      if (copy_ret != OB_SUCCESS) {
        ret = copy_ret;
      } else if (value.get_string().length() > 1) {
        ret = OB_WRONG_FIELD_TERMINATORS;
      } else {
        value.meta_.set_char();
        value.set_collation_type(params_.session_info_->get_local_collation_connection());
        if (option.kind == peg::LoadFormatKind::Escaped) {
          into_item.escaped_cht_ = value;
        } else {
          into_item.closed_cht_ = value;
        }
      }
      if (option.kind == peg::LoadFormatKind::OptionallyEnclosed) {
        into_item.is_optional_ = true;
      }
    }
  }
  return ret;
}

int ObSelectResolver::resolve_output_file_options(rust::Slice<const peg::Node> options,
                                                  ObSelectIntoItem &into_item)
{
  int ret = OB_SUCCESS;
  for (const auto &node : options) {
    if (OB_FAIL(ret)) { break; }
    const auto option = peg::file_option(node);
    switch (option.kind) {
      case peg::FileOptionKind::Single:
        into_item.is_single_ = peg::expression_literal(option.value).integer;
        break;
      case peg::FileOptionKind::MaxFileSize:
        OZ(resolve_output_size(option.value, into_item.max_file_size_));
        break;
      case peg::FileOptionKind::BufferSize:
        OZ(resolve_output_size(option.value, into_item.buffer_size_));
        break;
      default:
        ret = OB_ERR_UNEXPECTED;
    }
  }
  return ret;
}

int ObSelectResolver::resolve_file_output(const peg::FileOutputDefinition &output,
                                          ObSelectIntoItem &into_item)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(params_.session_info_) || OB_ISNULL(params_.expr_factory_)) {
    ret = OB_ERR_UNEXPECTED;
  }
  OZ(resolve_output_literal(output.filename, into_item.outfile_name_));
  if (OB_SUCC(ret) && !output.external_format.empty()) {
    OZ(resolve_output_format(output.external_format, into_item));
  } else if (OB_SUCC(ret)) {
    if (output.has_charset) {
      ObCharsetType charset_type = CHARSET_INVALID;
      const ObString charset(syntax_text(output.charset));
      if (CHARSET_INVALID == (charset_type = ObCharset::charset_type(charset.trim()))) {
        ret = OB_ERR_UNKNOWN_CHARSET;
        LOG_USER_ERROR(OB_ERR_UNKNOWN_CHARSET, charset.length(), charset.ptr());
      } else if (CHARSET_UTF16 == charset_type) {
        ret = OB_NOT_SUPPORTED;
        LOG_USER_ERROR(OB_NOT_SUPPORTED, "upload data using utf16");
      } else {
        into_item.cs_type_ = ObCharset::get_default_collation(charset_type);
      }
    }
    OZ(resolve_output_fields(output.fields, into_item));
    if (OB_SUCC(ret)) {
      for (const auto &node : output.lines) {
        const auto option = peg::load_format_option(node);
        if (option.kind == peg::LoadFormatKind::LineTerminated) {
          ret = resolve_output_literal(option.value, into_item.line_str_);
        }
        // LINES STARTING BY is accepted but unused by the SELECT INTO executor.
      }
    }
  }
  OZ(resolve_output_file_options(output.file_options, into_item));
  return ret;
}

int ObSelectResolver::resolve_output_format(rust::Slice<const peg::Node> options,
                                            ObSelectIntoItem &into_item)
{
  int ret = OB_SUCCESS;
  bool has_type = false;
  bool has_charset = false;
  ObExternalFileFormat format;
  for (const auto &node : options) {
    if (OB_FAIL(ret)) { break; }
    const auto kind = peg::external_format_option(node).kind;
    if (kind == peg::ExternalFormatKind::Type || kind == peg::ExternalFormatKind::Encoding) {
      OZ(resolve_output_format_option(node, format));
      has_type |= kind == peg::ExternalFormatKind::Type;
      has_charset |= kind == peg::ExternalFormatKind::Encoding;
    }
  }
  if (OB_FAIL(ret)) {
  } else if (!has_type) {
    ret = OB_NOT_SUPPORTED;
    LOG_USER_ERROR(OB_NOT_SUPPORTED, "format without type");
  } else if (format.format_type_ != ObExternalFileFormat::CSV_FORMAT) {
    ret = OB_NOT_SUPPORTED;
    LOG_USER_ERROR(OB_NOT_SUPPORTED, "this format type");
  } else {
    const ObCollationType collation = has_charset
        ? ObCharset::get_default_collation(format.csv_format_.cs_type_)
        : ObCharset::get_system_collation();
    OZ(format.csv_format_.init_format(ObDataInFileStruct(), 0, collation));
  }
  for (const auto &node : options) {
    if (OB_FAIL(ret)) { break; }
    const auto kind = peg::external_format_option(node).kind;
    if (kind != peg::ExternalFormatKind::Type && kind != peg::ExternalFormatKind::Encoding) {
      OZ(resolve_output_format_option(node, format));
    }
  }
  OZ(format.to_string_with_alloc(into_item.external_properties_, *allocator_, true));
  return ret;
}

int ObSelectResolver::resolve_output_format_option(const peg::Node &node, ObExternalFileFormat &format)
{
  int ret = OB_SUCCESS;
  const auto option = peg::external_format_option(node);
  const auto text = option.value.text();
  ObString string_value(syntax_text(text));
  auto &csv = format.csv_format_;
  auto &origin = format.origin_file_format_str_;
  switch (option.kind) {
    case peg::ExternalFormatKind::Type: {
      const ObString name = string_value.trim_space_only();
      for (int i = 0; i < ObExternalFileFormat::MAX_FORMAT; ++i) {
        if (ObExternalFileFormat::FORMAT_TYPE_STR[i] != nullptr
            && name.case_compare(ObExternalFileFormat::FORMAT_TYPE_STR[i]) == 0) {
          format.format_type_ = static_cast<ObExternalFileFormat::FormatType>(i);
          break;
        }
      }
      if (format.format_type_ == ObExternalFileFormat::INVALID_FORMAT) {
        ObSqlString message;
        message.append_fmt("format '%.*s'", name.length(), name.ptr());
        ret = OB_NOT_SUPPORTED;
        LOG_USER_ERROR(OB_NOT_SUPPORTED, message.ptr());
      }
      break;
    }
    case peg::ExternalFormatKind::Encoding: {
      const ObCharsetType charset = ObCharset::charset_type(string_value.trim_space_only());
      if (charset == CHARSET_INVALID) {
        ret = OB_ERR_UNSUPPORTED_CHARACTER_SET;
        LOG_USER_ERROR(OB_ERR_UNSUPPORTED_CHARACTER_SET);
      } else {
        csv.cs_type_ = charset;
      }
      break;
    }
    case peg::ExternalFormatKind::FieldTerminated:
      OZ(resolve_output_format_string(option.value, csv.cs_type_, csv.field_term_str_));
      if (OB_SUCC(ret) && csv.field_term_str_.empty()) {
        ret = OB_INVALID_ARGUMENT;
      }
      if (OB_SUCC(ret)) { origin.origin_field_term_str_ = params_.syntax_->source(node); }
      break;
    case peg::ExternalFormatKind::LineTerminated:
      OZ(resolve_output_format_string(option.value, csv.cs_type_, csv.line_term_str_));
      if (OB_SUCC(ret)) { origin.origin_line_term_str_ = params_.syntax_->source(node); }
      break;
    case peg::ExternalFormatKind::Escaped: {
      ObString value;
      OZ(resolve_output_format_string(option.value, csv.cs_type_, value));
      if (OB_SUCC(ret) && value.length() > 1) {
        ret = OB_ERR_INVALID_ESCAPE_CHAR_LENGTH;
        LOG_USER_ERROR(OB_ERR_INVALID_ESCAPE_CHAR_LENGTH);
      } else if (OB_SUCC(ret)) {
        csv.field_escaped_char_ = value.empty() ? INT64_MAX : value.ptr()[0];
        origin.origin_field_escaped_str_ = params_.syntax_->source(node);
      }
      break;
    }
    case peg::ExternalFormatKind::Enclosed:
    case peg::ExternalFormatKind::OptionallyEnclosed: {
      ObString value;
      OZ(resolve_output_format_string(option.value, csv.cs_type_, value));
      if (OB_SUCC(ret) && value.length() > 1) {
        ret = OB_WRONG_FIELD_TERMINATORS;
        LOG_USER_ERROR(OB_WRONG_FIELD_TERMINATORS);
      } else if (OB_SUCC(ret)) {
        csv.field_enclosed_char_ = value.empty() ? INT64_MAX : value.ptr()[0];
        csv.is_optional_ = option.kind == peg::ExternalFormatKind::OptionallyEnclosed;
        origin.origin_field_enclosed_str_ = params_.syntax_->source(node);
      }
      break;
    }
    case peg::ExternalFormatKind::SkipHeader: {
      uint64_t lines = 0;
      OZ(ObResolverUtils::resolve_unsigned_integer(option.value, "SKIP_HEADER", lines));
      OX(csv.skip_header_lines_ = lines);
      if (OB_SUCC(ret) && csv.parse_header_ && csv.skip_header_lines_ > 0) {
        ret = OB_SKIP_PARSE_HEADER_CONFLICT;
        LOG_USER_ERROR(OB_SKIP_PARSE_HEADER_CONFLICT);
      }
      break;
    }
    case peg::ExternalFormatKind::SkipBlankLines:
      csv.skip_blank_lines_ = peg::expression_literal(option.value).integer;
      break;
    case peg::ExternalFormatKind::TrimSpace:
      csv.trim_space_ = peg::expression_literal(option.value).integer;
      break;
    case peg::ExternalFormatKind::NullIf:
      OZ(csv.null_if_.allocate_array(*allocator_, option.values.size()));
      for (size_t i = 0; OB_SUCC(ret) && i < option.values.size(); ++i) {
        OZ(resolve_output_format_string(option.values[i], csv.cs_type_, csv.null_if_.at(i)));
      }
      if (OB_SUCC(ret)) { origin.origin_null_if_str_ = params_.syntax_->source(node); }
      break;
    case peg::ExternalFormatKind::EmptyFieldAsNull:
      csv.empty_field_as_null_ = peg::expression_literal(option.value).integer;
      break;
    case peg::ExternalFormatKind::Compression:
      OZ(compression_algorithm_from_string(string_value.trim(), csv.compression_algorithm_));
      break;
    case peg::ExternalFormatKind::BinaryFormat: {
      const ObString name = string_value.trim();
      if (name.case_compare("hex") == 0) {
        csv.binary_format_ = ObCSVGeneralFormat::ObCSVBinaryFormat::HEX;
      } else if (name.case_compare("base64") == 0) {
        csv.binary_format_ = ObCSVGeneralFormat::ObCSVBinaryFormat::BASE64;
      } else {
        ret = OB_NOT_SUPPORTED;
        ObSqlString message;
        message.append_fmt("%.*s -> binary_format", name.length(), name.ptr());
        LOG_USER_ERROR(OB_NOT_SUPPORTED, message.ptr());
      }
      break;
    }
    case peg::ExternalFormatKind::FileExtension: {
      const ObString extension = string_value.trim_space_only();
      if (extension.find('/') != nullptr) {
        ret = OB_INVALID_ARGUMENT;
        LOG_USER_ERROR(OB_INVALID_ARGUMENT, "file_extension can not contain '/'");
      } else {
        csv.file_extension_ = extension;
      }
      break;
    }
    case peg::ExternalFormatKind::ParseHeader:
      if (csv.skip_header_lines_ > 0) {
        ret = OB_SKIP_PARSE_HEADER_CONFLICT;
        LOG_USER_ERROR(OB_SKIP_PARSE_HEADER_CONFLICT);
      } else {
        csv.parse_header_ = peg::expression_literal(option.value).integer;
        if (csv.parse_header_ && csv.skip_header_lines_ == 0) {
          csv.skip_header_lines_ = 1;
        }
      }
      break;
    case peg::ExternalFormatKind::IgnoreLastEmptyColumn:
      csv.ignore_last_empty_col_ = peg::expression_literal(option.value).integer;
      break;
    default:
      ret = OB_ERR_UNEXPECTED;
  }
  return ret;
}

int ObSelectResolver::resolve_output_format_string(const peg::Node &node,
                                                    ObCharsetType format_charset,
                                                    ObString &result_value)
{
  int ret = OB_SUCCESS;
  // 1. resolve expr
  ObRawExpr *expr = NULL;
  ObRawExprFactory *expr_factory = params_.expr_factory_;
  ObSQLSessionInfo *session_info = params_.session_info_;
  ObRawExpr *new_expr = NULL;
  const int64_t max_len = 64;
  ObCastMode cast_mode = CM_NONE;
  ObRawExprResType expr_output_type;
  ObCollationType result_collation_type = ObCharset::get_bin_collation(format_charset);
  ObExprResType cast_dst_type;
  uint32_t res_len = 0;
  char *buf = nullptr;
  int64_t buf_size = 0;
  RowDesc row_desc;
  ObNewRow tmp_row;
  ObObj value_obj;
  ObTempExpr *temp_expr = NULL;
  ObExecContext *exec_ctx = NULL;
  if (OB_ISNULL(expr_factory) || OB_ISNULL(session_info)
      || OB_ISNULL(exec_ctx = session_info->get_cur_exec_ctx()) || OB_ISNULL(exec_ctx->get_sql_ctx())) {
    ret = OB_ERR_UNEXPECTED;
  } else if (OB_FAIL(ObResolverUtils::resolve_const_expr(params_, node, expr, nullptr))) {
  } else if (OB_ISNULL(expr)) {
    ret = OB_ERR_UNEXPECTED;
  } else if (OB_FAIL(expr->formalize(session_info))) {
  } else if (!expr->is_static_scalar_const_expr()) {
    ret = OB_NOT_SUPPORTED;
    ObSqlString err_msg;
    err_msg.append_fmt("using '%s' as format value", get_type_name(expr->get_expr_type()));
    LOG_USER_ERROR(OB_NOT_SUPPORTED, err_msg.ptr());
  }

  // 2. Cast non-string constants before evaluation.
  if (OB_SUCC(ret)) {
    expr_output_type = expr->get_result_type();
    cast_dst_type.set_type(ObVarcharType);
    cast_dst_type.set_length(max_len);
    cast_dst_type.set_collation_type(result_collation_type);
    cast_dst_type.set_collation_level(expr->get_collation_level());
    if (!(expr_output_type.is_varchar() ||
          expr_output_type.is_char() ||
          expr_output_type.is_varbinary() ||
          expr_output_type.is_binary())) {
      if (result_collation_type == CS_TYPE_INVALID) {
        ret = OB_ERR_PARAM_INVALID;
      } else if (OB_FAIL(ObSQLUtils::get_default_cast_mode(session_info, cast_mode))) {
      } else if (OB_FAIL(ObRawExprUtils::try_add_cast_expr_above(expr_factory,
                                                                 session_info,
                                                                 *expr,
                                                                 cast_dst_type,
                                                                 cast_mode,
                                                                 new_expr))) {
      } else if (OB_ISNULL(new_expr)) {
        ret = OB_ERR_UNEXPECTED;
      } else if (OB_FAIL(new_expr->add_flag(IS_OP_OPERAND_IMPLICIT_CAST))) {
      } else {
        expr = new_expr;
      }
    }
  }

  // 3. compute expr result
  if (OB_SUCC(ret)) {
    if (OB_FAIL(ObStaticEngineExprCG::gen_expr_with_row_desc(expr,
                                                            row_desc,
                                                            exec_ctx->get_allocator(),
                                                            exec_ctx->get_my_session(),
                                                            exec_ctx->get_sql_ctx()->schema_guard_,
                                                            temp_expr))) {
    } else if (OB_ISNULL(temp_expr)) {
      ret = OB_INVALID_ARGUMENT;
    } else if (OB_FAIL(temp_expr->eval(*exec_ctx, tmp_row, value_obj))) {
    } else if (value_obj.is_null()) {
      result_value = ObString();
    } else {
      result_value = value_obj.get_string();
    }
    if (OB_SUCC(ret) && result_value.length() > 0
        && format_charset != expr->get_result_type().get_charset_type()
        && CHARSET_BINARY != expr->get_result_type().get_charset_type()) {
      buf_size = result_value.length() * ObCharset::MAX_MB_LEN;
      if (OB_ISNULL(buf = static_cast<char *>(exec_ctx->get_allocator().alloc(buf_size)))) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
      } else if (OB_FAIL(ObCharset::charset_convert(expr->get_result_type().get_collation_type(),
                                                    result_value.ptr(), result_value.length(),
                                                    result_collation_type, buf, buf_size, res_len,
                                                    false, false))) {
      } else {
        result_value = ObString(res_len, buf);
      }
    }
  }
  return ret;
}

} // namespace oceanbase::sql
