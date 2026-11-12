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
#include "sql/resolver/cmd/load_data_resolver.h"
#include "sql/resolver/cmd/ob_load_data_stmt.h"
#include "sql/engine/cmd/ob_load_data_file_reader.h"
#include "sql/resolver/dml/ob_del_upd_resolver.h"
#include "share/schema/ob_part_mgr_util.h"
#include "lib/utility/ob_platform_io.h"
#include "rust/path_utils.h"

#include <cstring>

namespace oceanbase::sql {
using namespace common;
using namespace share::schema;

namespace {
bool has_wildcard(const ObString &path)
{
  const ObString wildcards("?*[]");
  for (int32_t i = 0; i < path.length(); ++i) {
    for (int32_t j = 0; j < wildcards.length(); ++j) {
      if (path[i] == wildcards[j]) { return true; }
    }
  }
  return false;
}

bool non_ascii(const ObString &text)
{
  for (int32_t i = 0; i < text.length(); ++i) {
    if (!ob_isascii(text[i])) { return true; }
  }
  return false;
}
}

int LoadDataResolver::resolve(const peg::Node &node)
{
  int ret = OB_SUCCESS;
  ObNameCaseMode case_mode = OB_NAME_CASE_INVALID;
  ObLoadDataStmt *statement = nullptr;
  const auto definition = peg::load_data_definition(node);
  uint64_t ignore_rows = 0;
  if (!definition.ignore_rows.empty()
      && OB_FAIL(ObResolverUtils::resolve_unsigned_integer(definition.ignore_rows[0], "LOAD DATA IGNORE", ignore_rows))) return ret;
  if (OB_ISNULL(session_info_) || OB_ISNULL(allocator_) || OB_ISNULL(schema_checker_)) {
    ret = OB_INVALID_ARGUMENT;
  } else if (OB_FAIL(session_info_->get_name_case_mode(case_mode))) {
  } else if (OB_ISNULL(statement = create_stmt<ObLoadDataStmt>())) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
  } else {
    stmt_ = statement;
    ObLoadArgument &arguments = statement->get_load_arguments();
    if (definition.location == peg::LoadLocation::Client) {
      bool enabled = false;
      if (OB_FAIL(local_infile_enabled(enabled))) {
      } else if (!enabled) {
        ret = OB_ERR_CLIENT_LOCAL_FILES_DISABLED;
        LOG_USER_ERROR(OB_ERR_CLIENT_LOCAL_FILES_DISABLED);
      } else {
        arguments.load_file_storage_ = ObLoadFileLocation::CLIENT_DISK;
      }
    }
    OZ(resolve_filename(definition.filename, *statement));
    if (OB_SUCC(ret)) {
      arguments.dupl_action_ = definition.duplicate == peg::LoadDuplicate::Ignore
          ? ObLoadDupActionType::LOAD_IGNORE : definition.duplicate == peg::LoadDuplicate::Replace
          ? ObLoadDupActionType::LOAD_REPLACE : ObLoadDupActionType::LOAD_STOP_ON_DUP;
    }
    OZ(resolve_table(definition.table, *statement));
    if (OB_SUCC(ret) && definition.has_compression) {
      const ObString compression(syntax_text(definition.compression));
      if (OB_FAIL(ObFileReadParam::parse_compression_format(
          compression, arguments.file_name_, arguments.compression_format_))) {
        LOG_USER_ERROR(OB_INVALID_ARGUMENT, "unknown compression format or cannot detect compression format by filename");
      }
    }
    if (OB_SUCC(ret)) {
      arguments.is_default_charset_ = definition.default_charset;
      if (!definition.default_charset) {
        const ObString charset(syntax_text(definition.charset));
        arguments.file_cs_type_ = ObCharset::get_default_collation(ObCharset::charset_type(charset));
        if (!ObCharset::is_valid_collation(arguments.file_cs_type_)) {
          ret = OB_ERR_UNKNOWN_CHARSET;
          LOG_USER_ERROR(OB_ERR_UNKNOWN_CHARSET, charset.length(), charset.ptr());
        }
      }
      int64_t minimum_length = 0;
      OZ(ObCharset::get_mbminlen_by_coll(arguments.file_cs_type_, minimum_length));
      if (OB_SUCC(ret) && minimum_length > 1) {
        ret = OB_NOT_SUPPORTED;
        LOG_USER_ERROR(OB_NOT_SUPPORTED, "compatible with ascii files are");
      }
    }
    if (OB_SUCC(ret)) {
      ObDataInFileStruct &formats = statement->get_data_struct_in_file();
      bool no_default_escape = false;
      IS_NO_BACKSLASH_ESCAPES(session_info_->get_sql_mode(), no_default_escape);
      if (no_default_escape) {
        formats.field_escaped_char_ = INT64_MAX;
        formats.field_escaped_str_ = "";
      }
      OZ(resolve_formats(definition.format_options, formats));
      const int64_t rows = static_cast<int64_t>(ignore_rows);
      arguments.ignore_rows_ = rows > 0 ? rows : 0;
    }
    OZ(resolve_fields(definition.input_fields, case_mode, *statement));
    OZ(resolve_assignments(definition.assignments, case_mode, *statement));
    OZ(resolve_hints(definition.hints, *statement));
    OZ(resolve_partitions(definition.partitions, *statement));
    if (OB_SUCC(ret) && arguments.dupl_action_ == ObLoadDupActionType::LOAD_STOP_ON_DUP
        && arguments.load_file_storage_ == ObLoadFileLocation::CLIENT_DISK) {
      arguments.dupl_action_ = ObLoadDupActionType::LOAD_IGNORE;
    }
    OZ(validate_stmt(*statement));
  }
  return ret;
}

int LoadDataResolver::copy_literal(const peg::Node &node, ObString &value)
{
  const auto literal = peg::expression_literal(node);
  if (literal.binary_notation != peg::BinaryNotation::None) {
    const auto bytes = peg::decode_binary_literal(literal.text, literal.binary_notation);
    return ob_write_string(*allocator_,
        ObString(bytes.size(), reinterpret_cast<const char *>(bytes.data())), value, true);
  }
  return ob_write_string(*allocator_, syntax_text(literal.text), value, true);
}

int LoadDataResolver::resolve_filename(const peg::Node &filename, ObLoadDataStmt &statement)
{
  int ret = OB_SUCCESS;
  ObLoadArgument &arguments = statement.get_load_arguments();
  ObString file_name;
  OZ(copy_literal(filename, file_name));
  if (OB_SUCC(ret) && file_name.empty()) {
    if (arguments.load_file_storage_ != ObLoadFileLocation::CLIENT_DISK) {
      ret = OB_FILE_NOT_EXIST;
    }
  } else if (OB_SUCC(ret)) {
    arguments.file_name_ = file_name;
    if (arguments.load_file_storage_ == ObLoadFileLocation::CLIENT_DISK) {
      if (file_name.find(',') != nullptr) {
        ret = OB_NOT_SUPPORTED;
        LOG_USER_ERROR(OB_NOT_SUPPORTED, "load multi files not supported");
      } else {
        OZ(arguments.file_iter_.add_files(&file_name));
      }
    } else {
      ObArray<ObString> matches;
      const int64_t full_path_size = 4096;
      char *path_buffer = static_cast<char *>(allocator_->alloc(full_path_size));
      if (OB_ISNULL(path_buffer)) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
      } else if (has_wildcard(file_name)) {
        ObString pattern;
        OZ(ob_write_string(*allocator_, file_name.trim_space_only(), pattern, true));
        if (OB_SUCC(ret)) {
          const auto *pattern_bytes = reinterpret_cast<const uint8_t *>(pattern.ptr());
          auto glob_result = oceanbase::path_utils::expand_glob(
              rust::Slice<const uint8_t>(pattern_bytes, pattern.length()));
          if (glob_result.code == oceanbase::path_utils::Code::Ok
              && glob_result.paths.size() == 0) {
            ret = OB_FILE_NOT_EXIST;
          } else if (glob_result.code != oceanbase::path_utils::Code::Ok) {
            ret = OB_ERR_SYS;
          } else {
            for (const auto &path : glob_result.paths) {
              ObString match_file;
              ObString path_string(static_cast<int32_t>(path.bytes.size()),
                                   reinterpret_cast<const char *>(path.bytes.data()));
              OZ(ob_write_string(*allocator_, path_string, match_file, true));
              OZ(matches.push_back(match_file));
              if (OB_FAIL(ret)) {
                break;
              }
            }
          }
        }
      } else {
        while (OB_SUCC(ret) && !file_name.empty()) {
          const char *comma = file_name.find(',');
          ObString part;
          if (comma == nullptr) {
            part = file_name.trim_space_only();
            file_name.reset();
          } else {
            part = file_name.split_on(comma).trim_space_only();
          }
          if (!part.empty()) {
            ObString path;
            OZ(ob_write_string(*allocator_, part, path, true));
            OZ(matches.push_back(path));
          }
        }
      }
      if (OB_SUCC(ret) && matches.empty()) {
        ret = OB_FILE_NOT_EXIST;
      }
      for (int64_t i = 0; OB_SUCC(ret) && i < matches.count(); ++i) {
        ObString secure_file_priv;
        if (0 != common::ob_absolute_path_utf8(matches.at(i).ptr(), path_buffer,
                                               full_path_size)) {
          ret = OB_FILE_NOT_EXIST;
        } else if (OB_FAIL(session_info_->get_secure_file_priv(secure_file_priv))) {
        } else if (!session_info_->is_inner()
            && OB_FAIL(ObResolverUtils::check_secure_path(secure_file_priv, path_buffer))) {
        } else {
          OZ(arguments.file_iter_.add_files(&matches.at(i)));
        }
      }
    }
  }
  return ret;
}

int LoadDataResolver::resolve_table(const peg::Node &table, ObLoadDataStmt &statement)
{
  int ret = OB_SUCCESS;
  ObString database_name;
  ObString table_name;
  const ObTableSchema *schema = nullptr;
  bool exists = false;
  OZ(resolve_table_relation_node(&table, table_name, database_name));
  OZ(schema_checker_->check_table_exists(database_name, table_name, false, false, exists));
  if (OB_SUCC(ret) && !exists) { ret = OB_TABLE_NOT_EXIST; }
  OZ(schema_checker_->get_table_schema(database_name, table_name, false, schema));
  if (OB_SUCC(ret) && OB_ISNULL(schema)) {
    ret = OB_ERR_UNEXPECTED;
  } else if (OB_SUCC(ret) && schema->is_view_table()) {
    ret = OB_NOT_SUPPORTED;
    LOG_USER_ERROR(OB_NOT_SUPPORTED, "load data to the view is");
  }
  OZ(check_trigger_constraint(schema));
  if (OB_SUCC(ret)) {
    ObLoadArgument &arguments = statement.get_load_arguments();
    arguments.table_id_ = schema->get_table_id();
    arguments.database_id_ = schema->get_database_id();
    arguments.part_level_ = schema->get_part_level();
    OZ(ob_write_string(*allocator_, table_name, arguments.table_name_));
    OZ(ob_write_string(*allocator_, database_name, arguments.database_name_));
    ObSqlString combined_name;
    OZ(ObResolverUtils::append_qualified_identifier(combined_name, database_name, table_name));
    OZ(ob_write_string(*allocator_, combined_name.string(), arguments.combined_name_));
  }
  return ret;
}

int LoadDataResolver::resolve_formats(rust::Slice<const peg::Node> options, ObDataInFileStruct &formats)
{
  int ret = OB_SUCCESS;
  for (const auto &node : options) {
    if (OB_FAIL(ret)) { break; }
    const auto option = peg::load_format_option(node);
    switch (option.kind) {
      case peg::LoadFormatKind::FieldTerminated:
        OZ(copy_literal(option.value, formats.field_term_str_));
        break;
      case peg::LoadFormatKind::OptionallyEnclosed:
        formats.is_opt_field_enclosed_ = true;
        [[fallthrough]];
      case peg::LoadFormatKind::Enclosed:
        OZ(copy_literal(option.value, formats.field_enclosed_str_));
        break;
      case peg::LoadFormatKind::Escaped:
        OZ(copy_literal(option.value, formats.field_escaped_str_));
        break;
      case peg::LoadFormatKind::LineStarting:
        OZ(copy_literal(option.value, formats.line_start_str_));
        break;
      case peg::LoadFormatKind::LineTerminated:
        OZ(copy_literal(option.value, formats.line_term_str_));
        break;
      default:
        ret = OB_ERR_UNEXPECTED;
        break;
    }
  }
  return ret;
}

int LoadDataResolver::validate_stmt(ObLoadDataStmt &statement)
{
  int ret = OB_SUCCESS;
  ObDataInFileStruct &formats = statement.get_data_struct_in_file();
  if (formats.field_enclosed_str_.length() > 1 || formats.field_escaped_str_.length() > 1
      || formats.field_term_str_.length() > ObLoadDataStmt::MAX_DELIMIT_STR_LEN
      || formats.line_term_str_.length() > ObLoadDataStmt::MAX_DELIMIT_STR_LEN
      || formats.line_start_str_.length() > ObLoadDataStmt::MAX_DELIMIT_STR_LEN) {
    ret = OB_WRONG_FIELD_TERMINATORS;
  } else {
    if (non_ascii(formats.field_enclosed_str_) || non_ascii(formats.field_escaped_str_)
        || non_ascii(formats.field_term_str_) || non_ascii(formats.line_term_str_)
        || non_ascii(formats.line_start_str_)) {
      LOG_WARN("LOAD DATA NON_ASCII_SEPARATOR not fully supported");
    }
    formats.field_enclosed_char_ = formats.field_enclosed_str_.empty()
        ? INT64_MAX : static_cast<int64_t>(formats.field_enclosed_str_[0]);
    formats.field_escaped_char_ = formats.field_escaped_str_.empty()
        ? INT64_MAX : static_cast<int64_t>(formats.field_escaped_str_[0]);
    statement.get_load_arguments().is_csv_format_ = true;
  }
  return ret;
}

int LoadDataResolver::resolve_fields(rust::Slice<const peg::Node> fields,
                                     ObNameCaseMode case_mode, ObLoadDataStmt &statement)
{
  int ret = OB_SUCCESS;
  if (fields.empty()) {
    OZ(resolve_default_fields(statement));
    OX(statement.set_default_table_columns());
  }
  for (const auto &node : fields) {
    if (OB_FAIL(ret)) { break; }
    ObLoadDataStmt::FieldOrVarStruct field;
    if (node.kind() == peg::NodeKind::UserVariable) {
      const auto name = peg::variable_name(node);
      field.is_table_column_ = false;
      OZ(ob_write_string(*allocator_, syntax_text(name), field.field_or_var_name_));
    } else {
      ObQualifiedName name;
      const ObColumnSchemaV2 *column = nullptr;
      const auto &arguments = statement.get_load_arguments();
      OZ(ObResolverUtils::resolve_column_ref(node, case_mode, *allocator_, name));
      if (OB_SUCC(ret) && ((!name.database_name_.empty()
              && name.database_name_.case_compare(arguments.database_name_) != 0)
          || (!name.tbl_name_.empty() && name.tbl_name_.case_compare(arguments.database_name_) != 0))) {
        ret = OB_ERR_BAD_FIELD_ERROR;
        const ObString full_name = concat_qualified_name(name.database_name_, name.tbl_name_, name.col_name_);
        LOG_USER_ERROR(OB_ERR_BAD_FIELD_ERROR, full_name.length(), full_name.ptr(),
            static_cast<int>(strlen("field list")), "field list");
      }
      OZ(get_column_schema(arguments.table_id_, name.col_name_, column, false));
      if (OB_SUCC(ret) && OB_ISNULL(column)) { ret = OB_ERR_UNEXPECTED; }
      if (OB_SUCC(ret)) {
        field.column_id_ = column->get_column_id();
        field.column_type_ = column->get_data_type();
        OZ(ob_write_string(*allocator_, name.col_name_, field.field_or_var_name_));
        for (int64_t i = 0; OB_SUCC(ret) && i < statement.get_field_or_var_list().count(); ++i) {
          const auto &previous = statement.get_field_or_var_list().at(i);
          if (previous.is_table_column_ && previous.column_id_ == field.column_id_) {
            ret = OB_ERR_FIELD_SPECIFIED_TWICE;
            ObCStringHelper helper;
            LOG_USER_ERROR(OB_ERR_FIELD_SPECIFIED_TWICE, helper.convert(field.field_or_var_name_));
          }
        }
      }
    }
    OZ(statement.get_field_or_var_list().push_back(field));
  }
  return ret;
}
int LoadDataResolver::resolve_default_fields(ObLoadDataStmt &load_stmt)
{
  int ret = OB_SUCCESS;
  ObIArray<ObLoadDataStmt::FieldOrVarStruct> &field_or_var_list = load_stmt.get_field_or_var_list();
  uint64_t table_id = load_stmt.get_load_arguments().table_id_;
  const ObTableSchema *table_schema = NULL;
  if (OB_ISNULL(session_info_)) {
    ret = OB_ERR_UNEXPECTED;
  } else if (OB_FAIL(schema_checker_->get_table_schema( table_id, table_schema))) {
  } else {
    ObColumnIterByPrevNextID iter(*table_schema);
    const ObColumnSchemaV2 *column_schema = NULL;
    while (OB_SUCC(ret) && OB_SUCC(iter.next(column_schema))) {
      if (OB_ISNULL(column_schema)) {
        ret = OB_ERR_UNEXPECTED;
      } else if (!column_schema->is_hidden()
                 && !column_schema->is_invisible_column()) {
        ObLoadDataStmt::FieldOrVarStruct tmp_struct;
        if (column_schema->is_virtual_generated_column()) {
          tmp_struct.is_table_column_ = false;
          tmp_struct.field_or_var_name_ = "";
          tmp_struct.column_type_ = ColumnType::ObMaxType;  //unknown type
          tmp_struct.column_id_ = OB_INVALID_ID;
        } else {
          tmp_struct.is_table_column_ = true;
          OZ(ob_write_string(*allocator_, column_schema->get_column_name_str(), tmp_struct.field_or_var_name_));
          tmp_struct.column_id_ = column_schema->get_column_id();
          tmp_struct.column_type_ = column_schema->get_data_type();
        }
        if (OB_SUCC(ret) && OB_FAIL(field_or_var_list.push_back(tmp_struct))) {
        }
      }
    }
    if (ret != OB_ITER_END) {
    } else {
      ret = OB_SUCCESS;
    }
  }
  return ret;
}


int LoadDataResolver::build_column_ref_expr(ObQualifiedName &q_name, ObRawExpr *&column_expr)
{
  int ret = OB_SUCCESS;
  ObLoadDataStmt* load_stmt = static_cast<ObLoadDataStmt*>(stmt_);
  uint64_t table_id = load_stmt->get_load_arguments().table_id_;
  const ObString &db_name = load_stmt->get_load_arguments().table_name_;
  const ObString &tb_name = load_stmt->get_load_arguments().table_name_;
  ObString owned_name;
  if (OB_FAIL(ob_write_string(*allocator_, q_name.col_name_, owned_name))) { return ret; }
  q_name.col_name_ = owned_name;
  const ObColumnSchemaV2 *col_schema = NULL;
  ObColumnRefRawExpr *col_expr = NULL;
  //check DB name and TABLE name
  //for load data, any column should belong to the loaded table
  if ((q_name.database_name_.length() > 0 && q_name.database_name_.compare(db_name) != 0) ||
      (q_name.tbl_name_.length() > 0 && q_name.tbl_name_.compare(tb_name) != 0)) {
    //TODO wjh: check error return code
    //ERROR 1054 (42S22): Unknown column 'xxx' in 'field list'
    ret = OB_ERR_BAD_FIELD_ERROR;
    ObString column_name = concat_qualified_name(q_name.database_name_, q_name.tbl_name_, q_name.col_name_);
    LOG_USER_ERROR(OB_ERR_BAD_FIELD_ERROR, column_name.length(), column_name.ptr(), (int)strlen("field list"), "field list");
    LOG_WARN("unknown column in field list", K(q_name.database_name_), K(q_name.tbl_name_), K(q_name.col_name_), K(db_name), K(tb_name));
  } else if (OB_FAIL(get_column_schema(table_id,
                                       q_name.col_name_,
                                       col_schema,
                                       false))) {
  } else if (OB_ISNULL(col_schema)) {
    ret = OB_ERR_UNEXPECTED;
  } else if (OB_FAIL(ObRawExprUtils::build_column_expr(*params_.expr_factory_, *col_schema,
                                                       params_.session_info_, col_expr))) {
  } else {
    col_expr->set_column_attr(tb_name, q_name.col_name_);
    col_expr->set_database_name(db_name);
    col_expr->set_ref_id(table_id, col_schema->get_column_id());
    col_expr->set_data_type(col_schema->get_data_type());
    column_expr = col_expr;

    ColumnItem column_item;
    column_item.expr_ = col_expr;
    column_item.set_default_value(col_schema->get_cur_default_value());
    column_item.table_id_ = col_expr->get_table_id();
    column_item.column_id_ = col_expr->get_column_id();
    column_item.column_name_ = col_expr->get_column_name();
    column_item.is_geo_ = col_schema->is_geometry();
    if (OB_FAIL(load_stmt->add_column_item(column_item))) {
    }
  }
  return ret;
}


int LoadDataResolver::resolve_assignments(rust::Slice<const peg::Node> assignments,
                                          ObNameCaseMode case_mode, ObLoadDataStmt &statement)
{
  int ret = OB_SUCCESS;
  for (const auto &node : assignments) {
    if (OB_FAIL(ret)) { break; }
    ObAssignment assignment;
    ObQualifiedName name;
    ObRawExpr *column = nullptr;
    OZ(ObResolverUtils::resolve_column_ref(peg::assignment_target(node), case_mode, *allocator_, name));
    OZ(build_column_ref_expr(name, column));
    if (OB_SUCC(ret)) {
      auto *reference = static_cast<ObColumnRefRawExpr *>(column);
      if (OB_ISNULL(reference)) {
        ret = OB_ERR_UNEXPECTED;
      } else if (reference->is_generated_column()) {
        ret = OB_NON_DEFAULT_VALUE_FOR_GENERATED_COLUMN;
        const auto &column_name = reference->get_column_name();
        const auto &table_name = statement.get_load_arguments().table_name_;
        LOG_USER_ERROR(OB_NON_DEFAULT_VALUE_FOR_GENERATED_COLUMN,
            column_name.length(), column_name.ptr(), table_name.length(), table_name.ptr());
      } else {
        assignment.column_expr_ = reference;
      }
    }
    ObRawExpr *expression = nullptr;
    ObArray<ObQualifiedName> columns;
    ObArray<ObSubQueryInfo> subqueries;
    ObArray<ObVarInfo> variables;
    ObArray<ObAggFunRawExpr *> aggregates;
    ObArray<ObWinFunRawExpr *> windows;
    ObArray<ObUDFInfo> functions;
    ObArray<ObOpRawExpr *> operators;
    OZ(ObRawExprUtils::build_raw_expr(*params_.expr_factory_, *session_info_,
        peg::assignment_value(node), params_.syntax_->source(), expression, columns, variables,
        aggregates, windows, subqueries, functions, operators));
    if (OB_SUCC(ret) && (aggregates.count() > 0 || windows.count() > 0 || subqueries.count() > 0
        || functions.count() > 0 || columns.count() > 0)) {
      ret = OB_NOT_SUPPORTED;
      LOG_USER_ERROR(OB_NOT_SUPPORTED, "set expression");
    }
    OX(assignment.expr_ = expression);
    OZ(statement.add_assignment(assignment));
  }
  return ret;
}

int LoadDataResolver::resolve_hints(rust::Slice<const peg::Node> hints, ObLoadDataStmt &statement)
{
  int ret = OB_SUCCESS;
  ObLoadDataHint &resolved = statement.get_hints();
  for (const auto &node : hints) {
    if (OB_FAIL(ret)) { break; }
    switch (node.kind()) {
      case peg::NodeKind::QueryTimeout: {
        const auto &literal = peg::hint_timeout(node);
        if (literal.kind() != peg::NodeKind::Int) { break; }
        int64_t value = static_cast<int64_t>(peg::integer_literal_value(literal));
        if (value > OB_MAX_USER_SPECIFIED_TIMEOUT) {
          value = OB_MAX_USER_SPECIFIED_TIMEOUT;
          LOG_USER_WARN(OB_ERR_TIMEOUT_TRUNCATED);
        }
        OZ(resolved.set_value(ObLoadDataHint::QUERY_TIMEOUT, value));
        break;
      }
      case peg::NodeKind::LogLevel: {
        const auto text = peg::hint_log_level(node);
        const ObString level(syntax_text(text));
        if (level.case_compare("disabled") != 0) {
          const int checked = OB_LOGGER.parse_check(level.ptr(), level.length());
          if (checked != OB_SUCCESS) {
            LOG_WARN("Log level parse check error", K(checked));
          } else {
            ObString owned;
            OZ(ob_write_string(*allocator_, level, owned));
            OZ(resolved.set_value(ObLoadDataHint::LOG_LEVEL, owned));
          }
        }
        break;
      }
      case peg::NodeKind::LoadBatchSize: {
        const auto &size = peg::hint_load_batch_size(node);
        if (size.kind() != peg::NodeKind::Int) break;
        OZ(resolved.set_value(ObLoadDataHint::BATCH_SIZE,
            static_cast<int64_t>(peg::integer_literal_value(size))));
        if (peg::hint_has_load_buffer_size(node)) {
          const auto text = peg::hint_load_buffer_size(node);
          ObString owned;
          OZ(ob_write_string(*allocator_, syntax_text(text), owned));
          OZ(resolved.set_value(ObLoadDataHint::BATCH_BUFFER_SIZE, owned));
        }
        break;
      }
      case peg::NodeKind::Parallel:
        if (peg::hint_parallel_policy(node) == peg::ParallelPolicy::Explicit
            && peg::hint_parallel_degree(node).kind() == peg::NodeKind::Int) {
          OZ(resolved.set_value(ObLoadDataHint::PARALLEL_THREADS,
              static_cast<int64_t>(peg::integer_literal_value(peg::hint_parallel_degree(node)))));
        }
        break;
      case peg::NodeKind::GatherStatistics:
        OZ(resolved.set_value(ObLoadDataHint::GATHER_OPTIMIZER_STATISTICS, 1));
        break;
      case peg::NodeKind::NoGatherStatistics:
        OZ(resolved.set_value(ObLoadDataHint::NO_GATHER_OPTIMIZER_STATISTICS, 1));
        break;
      default:
        LOG_WARN("Unused LOAD DATA hint", "hint_kind", static_cast<int>(node.kind()));
        break;
    }
  }
  return ret;
}

int LoadDataResolver::resolve_partitions(rust::Slice<const peg::Node> names, ObLoadDataStmt &statement)
{
  int ret = OB_SUCCESS;
  if (!names.empty()) {
    const ObTableSchema *schema = nullptr;
    OZ(schema_checker_->get_table_schema(statement.get_load_arguments().table_id_, schema));
    if (OB_SUCC(ret) && OB_ISNULL(schema)) { ret = OB_ERR_UNEXPECTED; }
    ObArray<ObObjectID> ids;
    ObArray<ObString> partition_names;
    for (const auto &node : names) {
      if (OB_FAIL(ret)) { break; }
      const auto text = node.text();
      ObString name;
      OZ(ob_write_string(*allocator_, syntax_text(text), name));
      OX(ObCharset::casedn(CS_TYPE_UTF8MB4_GENERAL_CI, name));
      ObArray<ObObjectID> partition_ids;
      ObPartGetter getter(*schema);
      if (OB_SUCC(ret) && OB_FAIL(getter.get_part_ids(name, partition_ids))) {
        if (ret == OB_UNKNOWN_PARTITION) {
          LOG_USER_ERROR(OB_UNKNOWN_PARTITION, name.length(), name.ptr(),
              schema->get_table_name_str().length(), schema->get_table_name_str().ptr());
        }
      }
      OZ(append_array_no_dup(ids, partition_ids));
      OZ(partition_names.push_back(name));
    }
    OZ(statement.set_part_ids(ids));
    OZ(statement.set_part_names(partition_names));
  }
  return ret;
}

int LoadDataResolver::local_infile_enabled(bool &enabled) const
{
  int ret = OB_SUCCESS;

  // 1. let's check the system variable and the capability flag in the mysql handshake
  enabled = false;
  int64_t local_infile_sys_var = 0;
  if (OB_ISNULL(session_info_)) {
  } else if (OB_FAIL(session_info_->get_sys_variable(share::SYS_VAR_LOCAL_INFILE, local_infile_sys_var))) {
  } else {
    const int64_t local_infile_capability_flag = session_info_->get_capability().cap_flags_.OB_CLIENT_LOCAL_FILES;
    enabled = (local_infile_sys_var != 0) && (local_infile_capability_flag != 0);
  }

  return ret;
}

int LoadDataResolver::check_trigger_constraint(const ObTableSchema *table_schema)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(table_schema)
      || OB_ISNULL(schema_checker_)
      || OB_ISNULL(session_info_)
      || OB_ISNULL(schema_checker_->get_schema_guard())) {
    ret = OB_ERR_UNEXPECTED;
  } else {
    for (int64_t i = 0; OB_SUCC(ret) && i < table_schema->get_trigger_list().count(); i++) {
      const ObTriggerInfo *trg_info = NULL;
      share::schema::ObSchemaGetterGuard *schema_guard = schema_checker_->get_schema_guard();
      if (OB_FAIL(schema_guard->get_trigger_info( table_schema->get_trigger_list().at(i), trg_info))) {
      } else if (OB_ISNULL(trg_info)) {
        ret = OB_ERR_UNEXPECTED;
      } else if (trg_info->is_enable()
                 && (trg_info->has_insert_event() || trg_info->has_update_event())) {
        ret = OB_NOT_SUPPORTED;
        LOG_USER_ERROR(OB_NOT_SUPPORTED, "if table has insert or update trigger, load data");
      }
    }
  }
  return ret;
}


}
