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

#include "sql/engine/cmd/ob_load_data_impl.h"
#include "share/rc/ob_server_runtime.h"

#include "sql/resolver/ob_resolver.h"
#include "sql/resolver/dml/ob_insert_stmt.h"
#include "sql/plan_cache/ob_sql_parameterization.h"
#include "sql/resolver/expr/ob_raw_expr_util.h"
#include "data_plane/ob_i_memory_pressure_service.h"
#include "sql/rewrite/ob_transform_utils.h"
#include "share/ob_timezone_mgr.h"

using namespace oceanbase::sql;
using namespace oceanbase::common;
using namespace oceanbase::share;
using namespace oceanbase::share::schema;
using namespace oceanbase::storage;

namespace oceanbase
{
namespace sql
{

#define OW(statement) \
  do {\
    int inner_ret = statement;\
    if (OB_UNLIKELY(OB_SUCCESS != inner_ret)) {\
      LOG_WARN("fail to exec"#statement, K(inner_ret));\
      if (OB_SUCC(ret)) { ret = inner_ret; }\
    }\
  } while (0)

const char *log_file_column_names = "\nBatchId\tLineNum\tType\tErrCode\tErrMsg\t\n";
const char *log_file_row_fmt = "%ld\t%ld\t%s\t%d\t%.*s\t\n";
static const int64_t WAIT_INTERVAL_US = 1 * 1000 * 1000;  //1s



int ObLoadDataBase::make_parameterize_stmt(ObExecContext &ctx,
                                           ObSqlString &insertsql,
                                           ParamStore &param_store,
                                           ObInsertStmt *&insert_stmt)
{
  int ret = OB_SUCCESS;
  ObSQLSessionInfo *session = NULL;

  if (OB_ISNULL(session = ctx.get_my_session())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("session is null", K(ret));
  } else if (OB_ISNULL(ctx.get_sql_ctx())
             || OB_ISNULL(ctx.get_sql_ctx()->schema_guard_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("sql ctx is null", K(ret));
  } else {
    ObParser parser(ctx.get_allocator(), session->get_sql_mode());
    ParseResult parse_result;

    SqlInfo not_param_info;
    bool is_transform_outline = false;
    if (OB_FAIL(parser.parse(insertsql.string(), parse_result))) {
    } else if (OB_FAIL(ObSqlParameterization::transform_syntax_tree(ctx.get_allocator(),
                                                                    *session,
                                                                    NULL,
                                                                    parse_result.result_tree_,
                                                                    not_param_info,
                                                                    param_store,
                                                                    NULL,
                                                                    is_transform_outline))) {
    } else {
      SMART_VAR(ObResolverParams, resolver_ctx) {
        ObSchemaChecker schema_checker;
        schema_checker.init(*(ctx.get_sql_ctx()->schema_guard_));
        resolver_ctx.allocator_  = &ctx.get_allocator();
        resolver_ctx.schema_checker_ = &schema_checker;
        resolver_ctx.session_info_ = session;
        resolver_ctx.param_list_ = &param_store;
        resolver_ctx.database_id_ = session->get_database_id();
        resolver_ctx.disable_privilege_check_ = PRIV_CHECK_FLAG_DISABLE;
        resolver_ctx.expr_factory_ = ctx.get_expr_factory();
        resolver_ctx.stmt_factory_ = ctx.get_stmt_factory();
        if (OB_ISNULL(ctx.get_stmt_factory())) {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("invalid argument", K(ret), KP(ctx.get_stmt_factory()));
        } else if (OB_ISNULL(ctx.get_stmt_factory()->get_query_ctx())) {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("invalid argument", K(ret), KP(ctx.get_stmt_factory()->get_query_ctx()));
        } else {
          resolver_ctx.query_ctx_ = ctx.get_stmt_factory()->get_query_ctx();
          resolver_ctx.query_ctx_->set_questionmark_count(param_store.count());
          resolver_ctx.query_ctx_->sql_schema_guard_.set_schema_guard(ctx.get_sql_ctx()->schema_guard_);
          ObResolver resolver(resolver_ctx);
          ObStmt *astmt = NULL;
          ParseNode *stmt_tree = parse_result.result_tree_->children_[0];
          if (OB_ISNULL(stmt_tree) || OB_ISNULL(ctx.get_stmt_factory()->get_query_ctx())) {
            ret = OB_ERR_UNEXPECTED;
            LOG_WARN("invalid argument", K(ret), K(stmt_tree));
          } else if (OB_FAIL(resolver.resolve(ObResolver::IS_NOT_PREPARED_STMT,
                                              *stmt_tree,
                                              astmt))) {
          } else {
            insert_stmt = static_cast<ObInsertStmt*>(astmt);
            ctx.get_stmt_factory()->get_query_ctx()->reset();
          }
        }
      }
    }
  }
  return ret;
}

int ObLoadDataBase::memory_check_worker(bool &need_wait_minor_freeze)
{
  int ret = OB_SUCCESS;

  SERVER_MODULE_SCOPE {
    data_plane::ObIMemoryPressureService *memory_pressure = nullptr;
    if (FALSE_IT(memory_pressure = ::oceanbase::share::server_service<::oceanbase::data_plane::ObIMemoryPressureService>())) {
    } else {
      int64_t active_memstore_used = 0;
      int64_t total_memstore_used = 0;
      int64_t major_freeze_trigger = 0;
      int64_t memstore_limit = 0;
      int64_t freeze_cnt = 0;

      if (OB_FAIL(memory_pressure->get_memstore_condition(
              active_memstore_used,
              total_memstore_used,
              major_freeze_trigger,
              memstore_limit,
              freeze_cnt))) {
      } else {
        if (total_memstore_used > (memstore_limit - major_freeze_trigger)/2 + major_freeze_trigger) {
          need_wait_minor_freeze = true;
        } else {
          need_wait_minor_freeze = false;
        }
      }
    }
  } else {
    LOG_ERROR("enter server runtime failed", K(ret));
  }
  return ret;
}

int ObLoadDataBase::wait_local_memory(ObExecContext &ctx, int64_t &total_wait_secs)
{
  int ret = OB_SUCCESS;
  ObSQLSessionInfo *session = NULL;
  const int64_t start_wait_ts = ObTimeUtil::current_time();
  bool need_wait_freeze = true;
  if (OB_ISNULL(session = ctx.get_my_session())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("session is null", K(ret));
  } else {
    LOG_INFO("LOAD DATA is suspended until local memory is available", K(total_wait_secs));
  }
  while (OB_SUCC(ret) && need_wait_freeze) {
    ob_usleep(WAIT_INTERVAL_US);
    if (OB_FAIL(ObLoadDataUtils::check_session_status(*session))) {
    } else if (OB_FAIL(memory_check_worker(need_wait_freeze))) {
    }
  }
  if (OB_SUCC(ret)) {
    const int64_t wait_secs =
        (ObTimeUtil::current_time() - start_wait_ts) / USECS_PER_SEC;
    total_wait_secs += wait_secs;
    LOG_INFO("LOAD DATA is resumed",
             "waited_seconds", wait_secs,
             K(total_wait_secs));
  }
  return ret;
}

int ObLoadDataBase::pre_parse_lines(ObLoadFileBuffer &buffer,
                                    ObCSVGeneralParser &parser,
                                    bool is_last_buf,
                                    int64_t &valid_len,
                                    int64_t &line_count)
{
  int ret = OB_SUCCESS;

  if (OB_UNLIKELY(!buffer.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid buffer", K(ret));
  } else if (parser.get_opt_params().is_simple_format_) {
    const ObCSVGeneralFormat &format = parser.get_format();
    char *cur_pos = buffer.begin_ptr();
    int64_t cur_lines = 0;
    for (char *p = buffer.begin_ptr(); p < buffer.current_ptr(); ++p) {
      char cur_char = *p;
      if (format.field_escaped_char_ == cur_char && p + 1 < buffer.current_ptr()) {
        p++;
      } else if (parser.get_opt_params().line_term_c_ == cur_char) {
        cur_lines++;
        cur_pos = p + 1;
        if (cur_lines >= line_count) {
          break;
        }
      }
    }
    if (is_last_buf && cur_lines < line_count && buffer.current_ptr() > cur_pos) {
      cur_lines++;
      cur_pos = buffer.current_ptr();
    }
    valid_len = cur_pos - buffer.begin_ptr();
    line_count = cur_lines;
  } else {
    ObSEArray<ObCSVGeneralParser::LineErrRec, 128> err_records;
    const char *ptr = buffer.begin_ptr();
    const char *end = ptr + buffer.get_data_len();
    struct Functor {
      int operator()(ObCSVGeneralParser::HandleOneLineParam param) {
        UNUSED(param);
        return OB_SUCCESS;
      }
      int operator()(ObCSVGeneralParser::HandleBatchLinesParam param) {
        UNUSED(param);
        return OB_SUCCESS;
      }
    };
    struct Functor unused_handler;
    if (OB_FAIL(parser.scan(ptr, end, line_count, NULL, NULL, unused_handler, err_records, is_last_buf))) {
    } else {
      valid_len = ptr - buffer.begin_ptr();
    }
  }

  return ret;
}

int ObInsertValueGenerator::fill_field_expr(ObIArray<ObCSVGeneralParser::FieldValue> &field_values,
                                            const ObBitSet<> &string_values)
{
  int ret = OB_SUCCESS;

  if (OB_UNLIKELY(field_values.count() != field_exprs_.count())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid input", K(ret), K(field_values), K(field_exprs_));
  } else {
    for (int i = 0; i < field_values.count(); ++i) {
      auto expr = static_cast<ObConstRawExpr *>(field_exprs_.at(i));
      ObLoadDataBase::field_to_obj(expr->get_value(),
                                   field_values.at(i),
                                   cs_type_,
                                   string_values.has_member(i));
    }
  }
  return ret;
}

int ObInsertValueGenerator::gen_insert_values(ObIArray<ObString> &insert_values,
                                              ObStringBuf &str_buf)
{

  int ret = OB_SUCCESS;
  for (int i = 0; OB_SUCC(ret) && i < insert_exprs_.count(); ++i) {
    auto expr = insert_exprs_.at(i);
    ObString store_value;
    data_buffer_->reset();
    ObConstRawExpr *const_expr = NULL;

    if (expr->get_expr_type() == T_DEFAULT) {
      OZ (str_buf.write_string("DEFAULT", &store_value));
    } else if (expr->is_const_raw_expr()
               && (const_expr = static_cast<ObConstRawExpr *>(expr))->get_value().is_string_type()) {
      ObString const_string = const_expr->get_value().get_string();
      ObCollationType coll_type = const_expr->get_value().get_collation_type();
      uint32_t pos = 0;
      if (ObCharset::charset_type_by_coll(coll_type) != CHARSET_UTF8MB4) {
        if (OB_FAIL(ObCharset::charset_convert(
          coll_type, const_string.ptr(), const_string.length(),
          CS_TYPE_UTF8MB4_BIN, data_buffer_->begin_ptr(), data_buffer_->get_remain_len(), pos, false))) {
        } else {
          const_string.assign_ptr(data_buffer_->begin_ptr(), pos);
          data_buffer_->update_pos(pos);
        }
      }
      if (OB_SUCC(ret)) {
        ObHexEscapeSqlStr escape_str(const_string, !!(SMO_NO_BACKSLASH_ESCAPES & sql_mode_));
        int64_t len = escape_str.to_string(data_buffer_->current_ptr() + 1, data_buffer_->get_remain_len() - 1);
        if (OB_UNLIKELY(len + 2 >= data_buffer_->get_remain_len())) {
          ret = OB_SIZE_OVERFLOW;
          LOG_WARN("fail to print string", K(ret), K(len), K(data_buffer_->get_remain_len()));
        } else {
          *data_buffer_->current_ptr() = '\'';
          *(data_buffer_->current_ptr() + 1 + len) = '\'';
          OZ (str_buf.write_string(ObString(static_cast<int32_t>(len + 2),
                                            data_buffer_->current_ptr()), &store_value));
        }
      }
    } else {
      OZ (expr_printer_.do_print(expr, T_NONE_SCOPE));
      OZ (str_buf.write_string(ObString(static_cast<int32_t>(data_buffer_->get_data_len()),
                                        data_buffer_->begin_ptr()), &store_value));
    }
    OX (insert_values.at(i) = store_value);
    //OZ (insert_values.push_back(store_value));
  }
  return ret;
}


int ObInsertValueGenerator::set_params(ObString &insert_header, ObCollationType cs_type, int64_t sql_mode)
{
  insert_header_ = insert_header;
  cs_type_ = cs_type;
  sql_mode_ = sql_mode;
  return OB_SUCCESS;
}

int ObInsertValueGenerator::init(ObSQLSessionInfo &session,
                                 ObLoadFileBuffer *data_buffer,
                                 ObSchemaGetterGuard *schema_guard)
{
  ObObjPrintParams param = session.create_obj_print_params();
  param.cs_type_ = CS_TYPE_UTF8MB4_BIN;
  expr_printer_.init(data_buffer->begin_ptr(),
                     data_buffer->get_buffer_size(),
                     data_buffer->get_pos(),
                     schema_guard,
                     param);
  data_buffer_ = data_buffer;
  return OB_SUCCESS;
}

int ObLoadDataSPImpl::gen_insert_columns_names_buff(ObExecContext &ctx,
                                                    const ObLoadArgument &load_args,
                                                    ObIArray<ObLoadTableColumnDesc> &insert_infos,
                                                    ObString &data_buff,
                                                    bool need_online_osg)
{
  int ret = OB_SUCCESS;

  ObSqlString insert_stmt;

  ObSEArray<ObString, 16> insert_column_names;
  if (OB_FAIL(insert_column_names.reserve(insert_infos.count()))) {
  }
  for (int64_t i = 0; OB_SUCC(ret) && i < insert_infos.count(); ++i) {
    if (OB_FAIL(insert_column_names.push_back(insert_infos.at(i).column_name_))) {
    }
  }
  /*
  if (OB_SUCC(ret)) {
    int64_t len = 0;
    char *buf = 0;
    OB_UNIS_ADD_LEN(insert_column_names);
    if (OB_ISNULL(buf = static_cast<char *>(ctx.get_allocator().alloc(len)))) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
      LOG_WARN("alloc memory failed", K(ret));
    } else {
      data_buff.set_data(buf, len);
      int64_t buf_len = len;
      int64_t pos = 0;
      OB_UNIS_ENCODE(insert_column_names);
    }
  }
  */

  if (OB_SUCC(ret)) {
    if (OB_FAIL(ObLoadDataUtils::build_insert_sql_string_head(load_args.dupl_action_,
                                                              load_args.combined_name_,
                                                              insert_column_names,
                                                              insert_stmt,
                                                              need_online_osg))) {
    } else if (OB_FAIL(ob_write_string(ctx.get_allocator(), insert_stmt.string(), data_buff))) {
    }
  }

  return ret;
}

class ReplaceVariables : public ObIRawExprReplacer
{
public:
  ReplaceVariables(ObExecContext &ctx,
                   ObLoadDataStmt &stmt,
                   ObIArray<ObRawExpr *> &fields)
    : ctx_(ctx), load_stmt_(stmt), field_exprs_(fields) {}

  int generate_new_expr(ObRawExprFactory &expr_factory, ObRawExpr *raw_expr, ObRawExpr *&new_expr)
  {
    int ret = OB_SUCCESS;
    UNUSED(expr_factory);
    ObSQLSessionInfo *session = NULL;
    if (OB_ISNULL(raw_expr)) {
      ret = OB_INVALID_ARGUMENT;
      LOG_WARN("invalid argument", K((ret)));
    } else if (OB_ISNULL(session = ctx_.get_my_session())) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("session is null", K(ret));
    } else if (raw_expr->get_expr_type() == T_REF_COLUMN
               || raw_expr->get_expr_type() == T_OP_GET_USER_VAR) {
      ObRawExpr *orig_expr = raw_expr;
      bool is_user_variable = false;
      //1. get variable name
      ObString ref_name;
      if (raw_expr->get_expr_type() == T_REF_COLUMN) {
        ObColumnRefRawExpr *column_ref = static_cast<ObColumnRefRawExpr*>(raw_expr);
        ref_name = column_ref->get_column_name();
      } else {
        is_user_variable = true;
        ObSysFunRawExpr *func_expr = static_cast<ObSysFunRawExpr*>(raw_expr);
        if (func_expr->get_param_count() != 1) {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("sys func expr child num is not correct", K(ret));
        } else {
          ObConstRawExpr *c_expr = static_cast<ObConstRawExpr*>(func_expr->get_param_expr(0));
          if (c_expr->get_value().get_type() != ObVarcharType) {
            ret = OB_ERR_UNEXPECTED;
            LOG_WARN("const expr child type is not correct", K(ret));
          } else {
            ref_name = c_expr->get_value().get_string();
          }
        }
      }
      //2. find and replace
      int64_t idx = OB_INVALID_INDEX;

      if (OB_SUCC(ret)) {
        for (int64_t i = 0; i < load_stmt_.get_field_or_var_list().count(); ++i) {
          if (0 == load_stmt_.get_field_or_var_list().at(i).field_or_var_name_.compare(ref_name)) {
            idx = i;
            break;
          }
        }

        if (OB_INVALID_INDEX != idx) {
          new_expr = field_exprs_.at(idx);
        } else {
          if (!is_user_variable) {
            ret = OB_ERR_UNEXPECTED;
            LOG_WARN("unknown column name in set right expr, do nothing", K(ret), K(ref_name));
          } else {
            ObConstRawExpr *c_expr = NULL;
            //find the real value from session
            if (OB_ISNULL(c_expr = OB_NEWx(ObConstRawExpr, (&ctx_.get_allocator())))) {
              ret = OB_ALLOCATE_MEMORY_FAILED;
              LOG_WARN("allocate const raw expr failed", K(ret));
            } else {
              ObObj var_obj;
              ObSessionVariable user_var;
              if (OB_FAIL(session->get_user_variable(ref_name, user_var))) {
              } else {
                var_obj = user_var.value_;
                var_obj.set_meta_type(user_var.meta_);
                c_expr->set_value(var_obj);
                new_expr = c_expr;
              }
            }
          }
        }
      }
      /*
    if (OB_SUCC(ret) && need_replaced_to_loaded_data_from_file) {
      raw_expr = c_expr;
      ObLoadDataReplacedExprInfo varable_info;
      varable_info.replaced_expr = c_expr;
      varable_info.correspond_file_field_idx = idx;
      if (OB_FAIL(generator.add_file_column_replace_info(varable_info))) {
        LOG_WARN("push back replaced variable infos array failed", K(ret));
      }
    }
*/

    }
    return ret;
  }

  ObExecContext &ctx_;
  ObLoadDataStmt &load_stmt_;
  ObIArray<ObRawExpr *> &field_exprs_;
};

int ObLoadDataSPImpl::copy_exprs_for_shuffle_task(ObExecContext &ctx,
                                                  ObLoadDataStmt &load_stmt,
                                                  ObIArray<ObLoadTableColumnDesc> &insert_infos,
                                                  ObIArray<ObRawExpr *> &field_exprs,
                                                  ObIArray<ObRawExpr *> &insert_exprs)
{
  int ret = OB_SUCCESS;

  if (OB_ISNULL(ctx.get_expr_factory())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("expr factory is null", K(ret));
  }

  OZ (field_exprs.reserve(load_stmt.get_field_or_var_list().count()));

  for (int i = 0; OB_SUCC(ret) && i < load_stmt.get_field_or_var_list().count(); ++i) {
    ObConstRawExpr *field_expr = NULL;
    OZ (ObRawExprUtils::build_const_string_expr(*ctx.get_expr_factory(),
                                                ObVarcharType,
                                                ObString(),
                                                load_stmt.get_load_arguments().file_cs_type_,
                                                field_expr));
    OZ (field_exprs.push_back(field_expr));
  }

  OZ (insert_exprs.reserve(insert_infos.count()));

  if (OB_SUCC(ret)) {
    ObRawExprCopier copier(*ctx.get_expr_factory());
    ReplaceVariables replacer(ctx, load_stmt, field_exprs);
    for (int i = 0; OB_SUCC(ret) && i < insert_infos.count(); ++i) {
      ObRawExpr *insert_expr = nullptr;
      ObLoadTableColumnDesc &desc = insert_infos.at(i);
      if (OB_NOT_NULL(desc.expr_value_)) {
        OZ (copier.copy_on_replace(desc.expr_value_, insert_expr, &replacer));
      } else {
        insert_expr = field_exprs.at(desc.array_ref_idx_);
      }
      OZ (insert_exprs.push_back(insert_expr));
    }
  }
  return ret;
}

int ObLoadDataSPImpl::gen_load_table_column_desc(ObExecContext &ctx,
                                                 ObLoadDataStmt &load_stmt,
                                                 ObIArray<ObLoadTableColumnDesc> &insert_infos)
{
  UNUSED(ctx);
  int ret = OB_SUCCESS;

  //e.g. general stmt like "INTO TABLE t1 (c1, c2, @a, @b) SET c3 = @a + @b"
  // step 1: add c1 and c2
  //     the first column of file will be written to t1.c1, so c1 will be added to the generator
  //     similarly, the second column to t1.c2 which also will be added to the generator
  // step 2: add c3 (calced by the first assign)
  //     @a, @b is not match column name, but their data will produce c3 by the "SET" clause,
  //     in result, c3 will be added
  //     in addition, replace expr @a with a const string expr which refer to a column from file
  //     do the same replace to @b

  //step 1
  for (int64_t i = 0; OB_SUCC(ret) && i < load_stmt.get_field_or_var_list().count(); ++i) {
    ObLoadDataStmt::FieldOrVarStruct &item = load_stmt.get_field_or_var_list().at(i);
    if (item.is_table_column_) {
      ObLoadTableColumnDesc tmp_info;
      tmp_info.is_set_values_ = false;
      tmp_info.column_name_ = item.field_or_var_name_;
      tmp_info.column_id_ = item.column_id_;
      tmp_info.column_type_ = item.column_type_;
      tmp_info.array_ref_idx_ = i; //array offset
      tmp_info.expr_value_ = NULL;
      if (OB_FAIL(insert_infos.push_back(tmp_info))) {
      }
    } else {
      //do nothing
      //ignore variables temporarily
    }
  }

  //step 2
  for (int64_t i = 0; OB_SUCC(ret) && i < load_stmt.get_table_assignment().count(); ++i) {
    const ObAssignment &assignment = load_stmt.get_table_assignment().at(i);
    ObColumnRefRawExpr *left = assignment.column_expr_;
    ObRawExpr *right = assignment.expr_;
    if (OB_ISNULL(left)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("set assign expr is null", K(ret));
    } /*else if (OB_FAIL(ObRawExprUtils::copy_expr(*ctx.get_expr_factory(),
                                                 assignment.expr_,
                                                 right,
                                                 COPY_REF_SHARED))) {
      LOG_WARN("fail to copy expr", K(ret));
    } */else {
      int64_t found_index = OB_INVALID_INDEX_INT64;
      for (int64_t j = 0; j < insert_infos.count(); ++j) {
        if (insert_infos.at(j).column_id_ == left->get_column_id()) {
          found_index = j;
          break;
        }
      }

      if (found_index != OB_INVALID_INDEX_INT64) {
        //overwrite
        ObLoadTableColumnDesc &tmp_info = insert_infos.at(found_index);
        tmp_info.is_set_values_ = true;
        tmp_info.array_ref_idx_ = OB_INVALID_INDEX_INT64;
        tmp_info.expr_value_ = right;
      } else {
        //a new insert column is defined by set expr
        ObLoadTableColumnDesc tmp_info;
        tmp_info.column_name_ = left->get_column_name();
        tmp_info.column_id_ = left->get_column_id();
        tmp_info.column_type_ = left->get_result_type().get_type();
        tmp_info.is_set_values_ = true;
        tmp_info.expr_value_ = right;
        if (OB_FAIL(insert_infos.push_back(tmp_info))) {
        }
      }
    }
  }


  return ret;
}



void ObCSVFormats::init(const ObDataInFileStruct &file_formats)
{
  field_term_char_ = file_formats.field_term_str_.empty() ?
        INT64_MAX : file_formats.field_term_str_[0];
  line_term_char_ = file_formats.line_term_str_.empty() ?
        INT64_MAX : file_formats.line_term_str_[0];
  enclose_char_ = file_formats.field_enclosed_char_;
  escape_char_ = file_formats.field_escaped_char_;
  null_column_fill_zero_string_ = true;

  if (!file_formats.field_term_str_.empty()
      && file_formats.line_term_str_.empty()) {
    is_line_term_by_counting_field_ = true;
    line_term_char_ = field_term_char_;
  }
  is_simple_format_ =
      !is_line_term_by_counting_field_
      && (field_term_char_ != INT64_MAX)
      && (line_term_char_ != INT64_MAX)
      && (field_term_char_ != line_term_char_)
      && (enclose_char_ == INT64_MAX);

}

ObShuffleTaskHandle::ObShuffleTaskHandle(ObExecContext &main_exec_ctx,
                                         ObDataFragMgr &main_datafrag_mgr,
                                         ObBitSet<> &main_string_values)
  : allocator(ObMemAttr(ObModIds::OB_SQL_LOAD_DATA)),
    exec_ctx(main_exec_ctx),
    data_buffer(NULL),
    escape_buffer(NULL),
    calc_tablet_id_expr(NULL),
    datafrag_mgr(main_datafrag_mgr),
    string_values(main_string_values)
{
  attr = ObMemAttr(ObModIds::OB_SQL_LOAD_DATA);
}

ObShuffleTaskHandle::~ObShuffleTaskHandle()
{
  if (OB_NOT_NULL(data_buffer)) {
    ob_free(data_buffer);
  }
}

int ObShuffleTaskHandle::expand_buf(const int64_t max_size, const int64_t to_buffer_size)
{
  int ret = OB_SUCCESS;
  int64_t new_size = to_buffer_size;
  if (new_size > max_size) {
    ret = OB_SIZE_OVERFLOW;
    LOG_WARN("buffer size not enough", K(ret));
  } else {
    char *buf = NULL;
    if (OB_ISNULL(buf = static_cast<char*>(ob_malloc(new_size * 2, attr)))) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
    } else {
      if (OB_NOT_NULL(data_buffer)) {
        ob_free(data_buffer);
      }
      data_buffer = new(buf) ObLoadFileBuffer(
            new_size - sizeof(ObLoadFileBuffer));
      escape_buffer = new(buf + new_size) ObLoadFileBuffer(
            new_size - sizeof(ObLoadFileBuffer));
    }
  }
  return ret;
}

int ObLoadDataSPImpl::exec_shuffle(int64_t task_id, ObShuffleTaskHandle *handle)
{
  int ret = OB_SUCCESS;


  void *expr_buf = NULL;
  ObLoadFileBuffer *expr_buffer = NULL;
  ObArrayHashMap<ObTabletID, ObDataFrag *> part_buf_mgr;
  ObSEArray<ObString, 32> insert_values;
  int64_t parsed_line_num = 0;
  ObStringBuf str_buf("LoadDataStrBuf", OB_MALLOC_MIDDLE_BLOCK_SIZE);
  // To call part_buf_mgr.for_each, an anonymous function is used, & references the external frag_mgr
  auto save_frag = [&] (ObTabletID tablet_id, ObDataFrag *frag) -> bool
  {
    // Place the frag filled with data into frag_mgr according to the partition
    int ret = OB_SUCCESS;
    ObPartDataFragMgr *part_datafrag_mgr = NULL;
    if (OB_FAIL(handle->datafrag_mgr.get_part_datafrag(tablet_id,
                                                       part_datafrag_mgr))) {
    } else if (OB_ISNULL(part_datafrag_mgr)) {
      ret = OB_ERR_UNEXPECTED;
    } else if (OB_FAIL(part_datafrag_mgr->queue_.push(frag))) {
    } else {
      ATOMIC_AAF(&(part_datafrag_mgr->total_row_proceduced_), frag->row_cnt);
    }
    return OB_SUCCESS == ret;
  };

  auto free_frag = [&] (ObTabletID tablet_id, ObDataFrag *frag) -> bool
  {
    if (OB_NOT_NULL(frag)) {
      handle->datafrag_mgr.distory_datafrag(frag);
    }
    return true;
  };

  if (OB_ISNULL(handle)
      || OB_ISNULL(handle->data_buffer)
      || OB_ISNULL(handle->escape_buffer)
      || OB_ISNULL(handle->exec_ctx.get_my_session())
      || OB_ISNULL(handle->exec_ctx.get_sql_ctx())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", KP(handle));
//  } else if (FALSE_IT(handle->exec_ctx.get_allocator().reuse())) {
  } else if (OB_FAIL(part_buf_mgr.init(ObMemAttr(ObModIds::OB_SQL_LOAD_DATA),
                                       handle->datafrag_mgr.get_total_part_cnt()))) {
  } else if (OB_FAIL(insert_values.prepare_allocate(
                       handle->generator.get_insert_exprs().count()))) {
  } else if (OB_ISNULL(expr_buf = ob_malloc(handle->data_buffer->get_buffer_size() + sizeof(ObLoadFileBuffer),
                                            ObMemAttr(ObModIds::OB_SQL_LOAD_DATA)))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    LOG_WARN("not enough memory", K(ret));
  } else {
    handle->err_records.reuse();
    expr_buffer = new(expr_buf) ObLoadFileBuffer(handle->data_buffer->get_buffer_size());
    ObSEArray<ObCSVGeneralParser::LineErrRec, 1> err_records;
    ObSEArray<ObObj, 32> parse_result;

    int64_t nrows = 1;
    const char *ptr = handle->data_buffer->begin_ptr();
    const char *end = handle->data_buffer->begin_ptr() + handle->data_buffer->get_data_len();

    struct Functor {
      int operator()(ObCSVGeneralParser::HandleOneLineParam param) {
        UNUSED(param);
        return OB_SUCCESS;
      }
      int operator()(ObCSVGeneralParser::HandleBatchLinesParam param) {
        UNUSED(param);
        return OB_SUCCESS;
      }
    };
    struct Functor handle_one_line;
    if (OB_FAIL(handle->generator.init(*(handle->exec_ctx.get_my_session()), expr_buffer,
                                       handle->exec_ctx.get_sql_ctx()->schema_guard_))) {
    } else if (OB_FAIL(parse_result.prepare_allocate(handle->generator.get_field_exprs().count()))) {
    } else {
      handle->exec_ctx.set_use_temp_expr_ctx_cache(true);
    }

    while (OB_SUCC(ret) && ptr < end) {
      const char *prev_ptr = ptr; //save the old value of ptr
      err_records.reuse();
      ret = handle->parser.scan<decltype(handle_one_line), true>(ptr, end, nrows,
                                                                 handle->escape_buffer->begin_ptr(),
                                                                 handle->escape_buffer->begin_ptr() + handle->escape_buffer->get_buffer_size(),
                                                                 handle_one_line, err_records, true);
      if (OB_FAIL(ret)) {
      } else {
        if (err_records.count() > 0) {
          ObParserErrRec rec;
          rec.row_offset_in_task = parsed_line_num;
          rec.ret = err_records[0].err_code;
          if (OB_FAIL(handle->err_records.push_back(rec))) {
          }
        }
      }
      if (OB_SUCC(ret) && nrows > 0) {
        int64_t cur_line_num = parsed_line_num++;
        // Calculate partition id
        ObObj result;
        ObTabletID tablet_id;
        //insert_values.reuse();
        str_buf.reuse();
        if (OB_FAIL(handle->generator.fill_field_expr(handle->parser.get_fields_per_line(),
                                                      handle->string_values))) {
        } else if (OB_FAIL(handle->generator.gen_insert_values(insert_values, str_buf))) {
        } else if (nullptr == handle->calc_tablet_id_expr) {
          int64_t idx = task_id % handle->datafrag_mgr.get_tablet_ids().count();
          tablet_id = handle->datafrag_mgr.get_tablet_ids().at(idx);
        } else {
          for (int i = 0; i < handle->parser.get_fields_per_line().count(); ++i) {
            ObCSVGeneralParser::FieldValue &str_v = handle->parser.get_fields_per_line().at(i);
            handle->row_in_file.get_cell(i) =
              static_cast<ObConstRawExpr *>(handle->generator.get_field_exprs().at(i))->get_value();
          }
          if (OB_FAIL(handle->calc_tablet_id_expr->eval(handle->exec_ctx, handle->row_in_file, result))) {
          } else {
            tablet_id = ObTabletID(result.get_uint64());
            if (OB_UNLIKELY(!tablet_id.is_valid())) {
              ret = OB_NO_PARTITION_FOR_GIVEN_VALUE;
              LOG_WARN("invalid partition for given value", K(ret));
            }
          }
        }

        LOG_DEBUG("LOAD DATA", "TheadId", get_tid_cache(), K(cur_line_num), K(tablet_id),
                  "line", handle->parser.get_fields_per_line(), "values", insert_values);
        // Serialize to DataFrag
        int64_t len = 0;
        OB_UNIS_ADD_LEN(insert_values);
        OB_UNIS_ADD_LEN(cur_line_num);
        int64_t row_ser_size = len;
        OB_UNIS_ADD_LEN(row_ser_size);

        ObDataFrag *frag = NULL;
        if (OB_SUCC(ret)) {
          int temp_ret = part_buf_mgr.get(tablet_id, frag);
          bool frag_exist = (OB_SUCCESS == temp_ret);
          if (!frag_exist || len > frag->get_remain()) {
            // Create a new
            ObDataFrag *new_frag = NULL;
            if (OB_FAIL(handle->datafrag_mgr.create_datafrag(new_frag, len))) {
            } else {
              if (frag_exist) {
                if (OB_UNLIKELY(!save_frag(tablet_id, frag))) {
                  ret = OB_ERR_UNEXPECTED;
                  LOG_WARN("fail to save frag", K(ret));
                } else if (OB_FAIL(part_buf_mgr.update(tablet_id, new_frag))) {
                }
              } else {
                if (OB_FAIL(part_buf_mgr.insert(tablet_id, new_frag))) {
                }
              }
              if (OB_SUCC(ret)) {
                frag = new_frag;
                frag->shuffle_task_id = task_id;
              } else {
                handle->datafrag_mgr.distory_datafrag(new_frag);
              }
            }
          }
        }

        if (OB_SUCC(ret)) {
          char *buf = frag->get_current();
          int64_t buf_len = frag->get_remain();
          int64_t pos = 0;
          OB_UNIS_ENCODE(row_ser_size);
          OB_UNIS_ENCODE(cur_line_num);
          OB_UNIS_ENCODE(insert_values);
          if (OB_SUCC(ret)) {
            frag->add_pos(pos);
            frag->add_row_cnt(1);
            //use the pointer change to calculate the original data size read to the frag
            frag->add_orig_data_size(static_cast<int64_t>(ptr - prev_ptr));
          }
        }
      }//end if yield
    } //end while

    if (OB_SUCC(ret)) {
      if (OB_FAIL(part_buf_mgr.for_each(save_frag))) {
      }
    }

  }

  if (OB_FAIL(ret)) {
    part_buf_mgr.for_each(free_frag);
  }

  if (OB_NOT_NULL(expr_buf)) {
    ob_free(expr_buf);
  }

  return ret;
}

int ObLoadDataSPImpl::exec_insert(ObInsertTask &task)
{
  int ret = OB_SUCCESS;
  int64_t sql_buff_len_init = OB_MALLOC_BIG_BLOCK_SIZE; //2M
  ObMemAttr attr(ObModIds::OB_SQL_LOAD_DATA);
  ObSqlString sql_str;
  ObSEArray<ObString, 1> single_row_values;
  sql_str.set_attr(attr);

  OZ (single_row_values.reserve(task.column_count_));
  OZ (sql_str.extend(sql_buff_len_init));
  OZ (sql_str.append(task.insert_stmt_head_));
  OZ (sql_str.append(ObString(" values ")));

  int64_t deserialized_rows = 0;
  for (int64_t buf_i = 0; OB_SUCC(ret) && buf_i < task.insert_value_data_.count(); ++buf_i) {
    int64_t pos = 0;
    const char* buf = task.insert_value_data_[buf_i].ptr();
    int64_t data_len = task.insert_value_data_[buf_i].length();
    while (OB_SUCC(ret) && pos < data_len) {
      int64_t row_ser_size = 0;
      int64_t row_num = 0;
      OB_UNIS_DECODE(row_ser_size);
      int64_t pos_back = pos;
      OB_UNIS_DECODE(row_num);
      single_row_values.reuse();
      OB_UNIS_DECODE(single_row_values);
      if (OB_SUCC(ret) && (pos - pos_back != row_ser_size
                           || single_row_values.count() != task.column_count_)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("row size is not as expected", "pos diff", pos - pos_back, K(row_ser_size),
                 "single row values count", single_row_values.count(), K(task.column_count_));
      }

      //print row
      if (deserialized_rows != 0) {
        OZ (sql_str.append(",", 1));
      }
      OZ (sql_str.append("(", 1));
      for (int64_t c = 0; OB_SUCC(ret) && c < single_row_values.count(); ++c) {
        //bool is_set_value = task.set_values_bitset_.has_member(c);
        if (c != 0) {
          OZ (sql_str.append(",", 1));
        }
        OZ (sql_str.append(single_row_values[c]));
      }
      OZ (sql_str.append(")", 1));

      deserialized_rows++;
    }

  } //end for

  if (OB_SUCC(ret) && deserialized_rows != task.row_count_) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("data in task not match deserialized result",
             K(ret), K(deserialized_rows), K(task.row_count_));
  }

  if (OB_SUCC(ret)) {
    ObTZMapWrap tz_map_wrap;
    if (OB_FAIL(OTTZ_MGR.get_timezone_map(tz_map_wrap))) {
    } else {
      task.timezone_.set_tz_info_map(tz_map_wrap.get_tz_map());
    }
  }

  int64_t affected_rows = 0;
  ObSessionParam param;
  param.is_load_data_exec_ = true;

  param.sql_mode_ = &task.sql_mode_;
  param.tz_info_wrap_ = &task.timezone_;

  if (OB_SUCC(ret) && OB_FAIL(GCTX.sql_proxy_->write(sql_str.string(),
                                                     affected_rows,
                                                     &param))) {
    LOG_WARN("fail to execute worker insert", K(ret), "task_id", task.task_id_);
  }


  return ret;
}

int ObLoadDataSPImpl::handle_returned_shuffle_task(ToolBox &box, ObShuffleTaskHandle &handle)
{
  UNUSED(box);
  int ret = OB_SUCCESS;

  if (OB_UNLIKELY(handle.result.task_id_ >= box.file_buf_row_num.count()
                  || handle.result.task_id_ < 0)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("invalid array index", K(ret),
             K(handle.result.task_id_), K(box.file_buf_row_num.count()));
  } else if (!box.file_appender.is_opened()
             && OB_FAIL(create_log_file(box))) {
    LOG_ERROR("fail to create log file", K(ret));
  }

  for (int64_t i = 0; OB_SUCC(ret) && i < handle.err_records.count(); ++i) {
    int64_t line_num = box.file_buf_row_num.at(handle.result.task_id_)
                       + handle.err_records.at(i).row_offset_in_task;
    if (OB_FAIL(log_failed_line(box,
                                TaskType::ShuffleTask,
                                handle.result.task_id_,
                                line_num,
                                handle.err_records.at(i).ret,
                                ObString()))) {
    }
  }

  return ret;
}

int ObLoadDataSPImpl::next_file_buffer(ObExecContext &ctx,
                                       ToolBox &box,
                                       ObShuffleTaskHandle *handle,
                                       int64_t limit)
{
  int ret = OB_SUCCESS;
  bool has_valid_data = true;

  CK (OB_NOT_NULL(handle) && OB_NOT_NULL(handle->data_buffer));

  do {

    if (OB_UNLIKELY(handle->data_buffer->get_struct_size() < box.data_trimer.get_buffer_size())) {
      OZ (handle->expand_buf(box.batch_buffer_size, box.data_trimer.get_buffer_size()));
    }
    // Restore the remaining data from data_trimer
    OZ (box.data_trimer.recover_incomplate_data(*handle->data_buffer));

    OZ (box.file_reader->readn(handle->data_buffer->current_ptr(),
                               handle->data_buffer->get_remain_len(),
                               box.read_cursor.read_size_));

    if (OB_SUCC(ret)) {
      if (OB_LIKELY(box.read_cursor.read_size_ > 0)) {
        handle->data_buffer->update_pos(box.read_cursor.read_size_); // update buffer data length
        int64_t last_proccessed_GBs = box.read_cursor.get_total_read_GBs();
        box.read_cursor.commit_read();
        int64_t processed_GBs = box.read_cursor.get_total_read_GBs();
        if (processed_GBs != last_proccessed_GBs) {
          LOG_INFO("LOAD DATA file read progress: ", K(processed_GBs));
        }

        box.job_status->read_bytes_ += box.read_cursor.read_size_;
      } else if (box.file_reader->eof()) {
        box.read_cursor.is_end_file_ = true;
      }
    }
    // Find complete lines from buffer, the remaining backup to data_trimer
    if (OB_SUCC(ret) && OB_LIKELY(handle->data_buffer->is_valid())) {
      int64_t complete_cnt = limit;
      int64_t complete_len = 0;
      if (OB_FAIL(pre_parse_lines(*handle->data_buffer, box.parser,
                                  box.read_cursor.is_end_file(),
                                  complete_len, complete_cnt))) {
      } else if (OB_FAIL(box.data_trimer.backup_incomplate_data(*handle->data_buffer,
                                                                complete_len))) {
      } else {
        box.data_trimer.commit_line_cnt(complete_cnt);
        has_valid_data = complete_cnt > 0;
        LOG_DEBUG("LOAD DATA",
            "split offset", box.read_cursor.total_read_size_ - box.data_trimer.get_incomplate_data_string().length(),
            K(complete_len), K(complete_cnt),
            "incomplate data length", box.data_trimer.get_incomplate_data_string().length(),
            "incomplate data", box.data_trimer.get_incomplate_data_string());
      }
    }
  } while (OB_SUCC(ret) && !has_valid_data && !box.read_cursor.is_end_file_
           && OB_SUCC(box.data_trimer.expand_buf(ctx.get_allocator())));
  return ret;
}

int ObLoadDataSPImpl::process_shuffle_tasks(ObExecContext &ctx, ToolBox &box)
{
  int ret = OB_SUCCESS;
  ObShuffleTaskHandle *handle = box.shuffle_handle;
  if (OB_ISNULL(handle)) {
    ret = OB_NOT_INIT;
    LOG_WARN("shuffle handle is null", K(ret));
  }

  for (int64_t i = 0;
       OB_SUCC(ret) && !box.read_cursor.is_end_file() && i < box.data_frag_buffer_count_limit;
       ++i) {
    const int64_t task_id = box.file_buf_row_num.count();
    handle->data_buffer->reset();
    handle->result.reset();
    handle->result.task_id_ = task_id;
    handle->err_records.reuse();

    if (OB_FAIL(box.file_buf_row_num.push_back(box.data_trimer.get_lines_count()))) {
    } else if (OB_FAIL(next_file_buffer(ctx, box, handle))) {
    } else if (handle->data_buffer->get_data_len() > 0) {
      const int64_t begin_ts = ObTimeUtil::current_time();
      if (OB_FAIL(exec_shuffle(task_id, handle))) {
      }
      handle->result.process_us_ = ObTimeUtil::current_time() - begin_ts;
      box.suffle_rt_sum += handle->result.process_us_;
      box.shuffle_task_count++;
      box.job_status->shuffle_rt_sum_ = box.suffle_rt_sum;
      box.job_status->total_shuffle_task_ = box.shuffle_task_count;
      if (OB_SUCC(ret) && handle->err_records.count() > 0
          && OB_FAIL(handle_returned_shuffle_task(box, *handle))) {
        LOG_WARN("fail to handle local shuffle result", K(ret), K(task_id));
      }
    }
  }
  return ret;
}

int ObLoadDataSPImpl::create_log_file(ToolBox &box)
{
  int ret = OB_SUCCESS;

  if (OB_FAIL(box.file_appender.open(box.log_file_name, false, true))) {
  } else if (OB_FAIL(box.file_appender.append(box.load_info.ptr(),
                                              box.load_info.length(),
                                              false))) {
  } else if (OB_FAIL(box.file_appender.append(log_file_column_names,
                                              strlen(log_file_column_names),
                                              false))) {
  }
  return ret;
}

int ObLoadDataSPImpl::log_failed_line(ToolBox &box,
                                      TaskType task_type,
                                      int64_t task_id,
                                      int64_t line_num,
                                      int err_code,
                                      ObString err_msg)
{
  int ret = OB_SUCCESS;

  if (OB_ISNULL(box.expr_buffer)
      || !box.file_appender.is_opened()) {
    ret = OB_NOT_INIT;
    LOG_WARN("box not init", K(ret));
  } else {
    box.expr_buffer->reset();
    int64_t log_buf_pos = 0;
    //int err_no = ob_errpkt_errno(err_code);
    if (err_msg.empty()) {
      err_msg = ob_errpkt_strerror(err_code);
    }
    if (OB_FAIL(databuff_printf(box.expr_buffer->begin_ptr(),
                                box.expr_buffer->get_buffer_size(),
                                log_buf_pos,
                                log_file_row_fmt,
                                task_id + 1,
                                line_num + 1,
                                task_type == TaskType::ShuffleTask ? "WARN" : "ERROR",
                                err_code,
                                err_msg.length(),
                                err_msg.ptr()))) {
    } else if (OB_FAIL(box.file_appender.append(box.expr_buffer->begin_ptr(),
                                                log_buf_pos,
                                                false))) {
    } else {
    }

  }
  return ret;
}

int ObLoadDataSPImpl::log_failed_insert_task(ToolBox &box, ObInsertTask &task)
{
  int ret = OB_SUCCESS;
  int log_err = OB_SUCCESS;
  int row_counter = 0;

  if (!box.file_appender.is_opened()
      && OB_FAIL(create_log_file(box))) {
    LOG_ERROR("fail to create log file", K(ret));
  } else {
    log_err = task.result_.exec_ret_;
  }

  for (int64_t buf_i = 0; OB_SUCC(ret) && buf_i < task.insert_value_data_.count(); ++buf_i) {
    int64_t pos = 0;
    const char* buf = task.insert_value_data_[buf_i].ptr();
    int64_t data_len = task.insert_value_data_[buf_i].length();
    ObDataFrag *frag = NULL;
    int64_t line_num_base = 0;

    if (OB_ISNULL(frag = static_cast<ObDataFrag *>(task.source_frag_[buf_i]))) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("source data frag is NULL", K(buf_i), K(ret), K(task));
    } else if (OB_UNLIKELY(OB_INVALID_ID == frag->shuffle_task_id
                           || frag->shuffle_task_id >= box.file_buf_row_num.count())) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("shuffle task id is invalid", K(ret), K(frag->shuffle_task_id));
    } else {
      line_num_base = box.file_buf_row_num.at(frag->shuffle_task_id);
    }
    while (OB_SUCC(ret) && pos < data_len) {
      int64_t row_ser_size = 0;
      int64_t row_num = 0;
      OB_UNIS_DECODE(row_ser_size);
      int64_t pos_back = pos;
      OB_UNIS_DECODE(row_num);
      int64_t line_num = line_num_base + row_num;
      row_counter++;
      if (task.result_.err_line_no_ == row_counter) {
        OZ (log_failed_line(box, TaskType::InsertTask, task.task_id_, line_num, log_err,
                            task.result_.err_msg_));
      }

      pos = pos_back + row_ser_size;
    }

  } //end for
  return ret;
}

int ObLoadDataSPImpl::execute_insert_task(ObExecContext &ctx,
                                          ToolBox &box,
                                          ObInsertTask &insert_task)
{
  UNUSED(ctx);
  UNUSED(box);
  int ret = OB_SUCCESS;
  ObInsertResult &result = insert_task.result_;
  result.reset();
  const int64_t begin_ts = ObTimeUtil::current_time();
  {
    // The original asynchronous insert worker had its own thread-local warning
    // buffer. Keep the same isolation after executing the task synchronously so
    // successful inner inserts do not leak warnings to the LOAD DATA statement.
    ObWarningBufferIgnoreScope ignore_internal_insert_warnings;
    result.exec_ret_ = exec_insert(insert_task);
    if (OB_SUCCESS != result.exec_ret_) {
      ObWarningBuffer *warning_buf = ob_get_tsi_warning_buffer();
      if (OB_NOT_NULL(warning_buf)) {
        result.err_line_no_ = warning_buf->get_error_line();
        int copy_ret = ob_write_string(result.allocator_, warning_buf->get_err_msg(), result.err_msg_);
        if (OB_SUCCESS != copy_ret) {
        }
      }
    }
  }
  bool need_wait_freeze = false;
  int memory_ret = memory_check_worker(need_wait_freeze);
  if (OB_SUCCESS != memory_ret) {
    LOG_WARN("failed to check local memory after insert", K(memory_ret));
    if (OB_SUCCESS == result.exec_ret_) {
      result.exec_ret_ = memory_ret;
    }
  }
  result.need_wait_minor_freeze_ = need_wait_freeze;
  insert_task.process_us_ = ObTimeUtil::current_time() - begin_ts;
  return ret;
}

int ObLoadDataSPImpl::handle_insert_result(ObExecContext &ctx,
                                           ToolBox &box,
                                           ObInsertTask &insert_task)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(insert_task.part_mgr)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("insert task has no local partition", K(ret), K(insert_task));
  } else if (insert_task.result_.need_wait_minor_freeze_
             && OB_FAIL(wait_local_memory(ctx, box.wait_secs_for_mem_release))) {
    LOG_WARN("failed to wait for local memory", K(ret));
  } else if (OB_SUCCESS != insert_task.result_.exec_ret_) {
    if (OB_SUCCESS != log_failed_insert_task(box, insert_task)) {
    }
    ret = insert_task.result_.exec_ret_;
    LOG_WARN("LOAD DATA local insert task failed", K(ret),
             "task_id", insert_task.task_id_, K(insert_task.row_count_));
  } else {
    box.affected_rows += insert_task.row_count_;
    box.insert_rt_sum += insert_task.process_us_;
    box.job_status->parsed_rows_ = box.affected_rows;
    box.job_status->parsed_bytes_ += insert_task.data_size_;
    box.job_status->total_insert_task_ = box.insert_task_count;
    box.job_status->insert_rt_sum_ = box.insert_rt_sum;
    box.job_status->total_wait_secs_ = box.wait_secs_for_mem_release;
  }
  return ret;
}

int ObLoadDataSPImpl::process_insert_tasks(ObExecContext &ctx, ToolBox &box)
{
  int ret = OB_SUCCESS;
  ObSQLSessionInfo *session = ctx.get_my_session();
  if (OB_ISNULL(session)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("session is null", K(ret));
  }

  for (int64_t i = 0; OB_SUCC(ret) && i < box.data_frag_mgr.get_tablet_ids().count(); ++i) {
    ObPartDataFragMgr *part_mgr = NULL;
    const ObTabletID tablet_id = box.data_frag_mgr.get_tablet_ids().at(i);
    if (OB_FAIL(box.data_frag_mgr.get_part_datafrag(tablet_id, part_mgr))) {
    } else if (OB_ISNULL(part_mgr)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("local partition data is null", K(ret), K(tablet_id));
    }

    while (OB_SUCC(ret)) {
      int64_t row_count = box.batch_row_count;
      if (!part_mgr->has_data(row_count)) {
        if (box.read_cursor.is_end_file()) {
          row_count = part_mgr->remain_row_count();
        } else {
          row_count = 0;
        }
      }
      if (row_count <= 0) {
        break;
      }

      ObInsertTask insert_task;
      insert_task.part_mgr = part_mgr;
      insert_task.task_id_ = box.insert_task_count++;
      insert_task.insert_stmt_head_ = box.insert_stmt_head_buff;
      insert_task.column_count_ = box.insert_infos.count();
      insert_task.sql_mode_ = session->get_sql_mode();
      if (OB_FAIL(insert_task.timezone_.deep_copy(session->get_tz_info_wrap()))) {
      } else if (OB_FAIL(part_mgr->next_insert_task(row_count, insert_task))) {
      } else if (OB_FAIL(execute_insert_task(ctx, box, insert_task))) {
      } else if (OB_FAIL(handle_insert_result(ctx, box, insert_task))) {
      }
    }
  }
  return ret;
}

int ObLoadDataSPImpl::execute(ObExecContext &ctx, ObLoadDataStmt &load_stmt)
{
  int ret = OB_SUCCESS;

  HEAP_VAR(ToolBox, box) {
    //init toolbox
    OZ (box.init(ctx, load_stmt));
    
    LOG_INFO("LOAD DATA start report"
             , "file_path", load_stmt.get_load_arguments().file_name_
             , "table_name", load_stmt.get_load_arguments().combined_name_
             , "batch_size", box.batch_row_count
             , "load_mode", box.insert_mode
             );
    
    ObString filename;
    while (OB_SUCC(ret) && OB_SUCC(box.file_iter.get_next_file(filename))) {

      OZ (box.open_file(filename, ctx));

      //ignore rows
      while (OB_SUCC(ret)
             && !box.read_cursor.is_end_file()
             && box.data_trimer.get_current_file_lines_count() < box.ignore_rows) {
        box.shuffle_handle->data_buffer->reset();
        OZ (next_file_buffer(ctx, box, box.shuffle_handle,
                             box.ignore_rows - box.data_trimer.get_current_file_lines_count()));
        OZ (ObLoadDataUtils::check_session_status(*ctx.get_my_session()));
        LOG_DEBUG("LOAD DATA ignore rows", K(box.ignore_rows), K(box.data_trimer.get_current_file_lines_count()));
      }

      //main while
      while (OB_SUCC(ret) && !box.read_cursor.is_end_file()) {
        OZ (process_shuffle_tasks(ctx, box));
        OZ (process_insert_tasks(ctx, box));
        OW (box.data_frag_mgr.free_unused_datafrag());

        /* Check if the session is valid, exit directly if invalid
         */
        OZ (ObLoadDataUtils::check_session_status(*ctx.get_my_session()));
      }
    }

    if (OB_ITER_END == ret) {
      ret = OB_SUCCESS;
    }

    //release
    OW (box.release_resources());

    if (OB_SUCC(ret) && OB_NOT_NULL(ctx.get_physical_plan_ctx())) {
      ctx.get_physical_plan_ctx()->set_affected_rows(box.affected_rows);
      ctx.get_physical_plan_ctx()->set_row_matched_count(box.data_trimer.get_lines_count());
    }

    if (OB_NOT_NULL(ctx.get_my_session())) {
      ctx.get_my_session()->reset_cur_phy_plan_to_null();
    }

    if (OB_FAIL(ret)) {
    }

    if (box.file_appender.is_opened()) {
      LOG_WARN("LOAD DATA error log generated");
    }

    LOG_INFO("LOAD DATA finish report"
             , "total shuffle task", box.shuffle_task_count
             , "total insert task", box.insert_task_count
             , "insert rt sum", box.insert_rt_sum
             , "suffle rt sum", box.suffle_rt_sum
             , "total wait secs", box.wait_secs_for_mem_release
             , "datafrag info", box.data_frag_mgr
             );
  }

  return ret;
}

int ObLoadFileDataTrimer::recover_incomplate_data(ObLoadFileBuffer &buffer)
{
  int ret = OB_SUCCESS;
  char *buf = NULL;
  if (OB_ISNULL(buf = buffer.begin_ptr())
      || OB_UNLIKELY(buffer.get_buffer_size() < incomplate_data_len_)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arg", K(buffer.get_buffer_size()));
  } else if (incomplate_data_len_ > 0) {
    MEMCPY(buf, incomplate_data_, incomplate_data_len_);
    buffer.update_pos(incomplate_data_len_);
  }
  return ret;
}

int ObLoadFileDataTrimer::backup_incomplate_data(ObLoadFileBuffer &buffer, int64_t valid_data_len)
{
  int ret = OB_SUCCESS;
  incomplate_data_len_ = buffer.get_data_len() - valid_data_len;
  if (incomplate_data_len_ > incomplate_data_buf_len_) {
    ret = OB_SIZE_OVERFLOW;
    LOG_WARN("size over flow", K(ret), K(incomplate_data_len_), K(incomplate_data_buf_len_));
  } else if (incomplate_data_len_ > 0 && NULL != incomplate_data_) {
    MEMCPY(incomplate_data_, buffer.begin_ptr() + valid_data_len, incomplate_data_len_);
    buffer.update_pos(-incomplate_data_len_);
  }
  return ret;
}

int ObPartDataFragMgr::rowoffset2pos(ObDataFrag *frag, int64_t row_num, int64_t &pos)
{
  int ret = OB_SUCCESS;
  pos = 0;

  if (OB_ISNULL(frag)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret));
  } else {
    char *buf = frag->data;
    int64_t data_len = frag->frag_pos;
    for (int64_t i = 0; OB_SUCC(ret) && i < row_num; ++i) {
      int64_t row_len = 0;
      OB_UNIS_DECODE(row_len);
      pos+=row_len;
    }
  }

  return ret;
}

int ObPartDataFragMgr::free_frags()
{
  int ret = OB_SUCCESS;
  for (int64_t i = 0; i < frag_free_list_.count(); ++i) {
    data_frag_mgr_.distory_datafrag(frag_free_list_[i]);
  }
  frag_free_list_.reuse();
  return ret;
}

int ObPartDataFragMgr::clear()
{
  int ret = OB_SUCCESS;
  ObLink *link = NULL;

  if (!has_data(1)) {
    //do nothing
  } else {
    while (OB_SUCC(ret) && OB_EAGAIN != queue_.pop(link)) {
      data_frag_mgr_.distory_datafrag(static_cast<ObDataFrag *>(link));
    }
  }
  return ret;
}

int ObPartDataFragMgr::next_insert_task(int64_t batch_row_count, ObInsertTask &task)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(batch_row_count <= 0)) {
    ret = OB_ERR_UNEXPECTED;
  } else if (OB_UNLIKELY(!has_data(batch_row_count))) {
    ret = OB_EAGAIN; //for now, never reach here
  } else {
    total_row_consumed_ += batch_row_count;
  }

  ObLink *link = NULL;
  ObDataFrag *frag = NULL;
  int64_t row_count = -queue_top_begin_point_.frag_row_pos_;
  InsertTaskSplitPoint new_top_begin_point;

  while (OB_SUCC(ret) && row_count < batch_row_count) {
    new_top_begin_point.reset();
    //handle one frag from head
#ifdef _WIN32
    while (OB_EAGAIN == queue_.top(link)) { YieldProcessor(); }
#else
    while (OB_EAGAIN == queue_.top(link)) { pause(); }
#endif
    if (OB_ISNULL(frag = static_cast<ObDataFrag *>(link))) {
      ret = OB_ERR_UNEXPECTED;
    } else if ((row_count += frag->row_cnt) > batch_row_count) {
      //case1 frag has data remained，do not pop
      new_top_begin_point.frag_row_pos_ = frag->row_cnt - (row_count - batch_row_count);
      if (OB_FAIL(rowoffset2pos(frag,
                                new_top_begin_point.frag_row_pos_,
                                new_top_begin_point.frag_data_pos_))) {
      } else if (OB_FAIL(task.insert_value_data_.push_back(
         ObString(new_top_begin_point.frag_data_pos_ - queue_top_begin_point_.frag_data_pos_,
         frag->data + queue_top_begin_point_.frag_data_pos_)))) {
      } else if (OB_FAIL(task.source_frag_.push_back(frag))) {
      }
    } else {
      //case2 frag is empty，need pop
      if (OB_FAIL(queue_.pop(link))) {
        ret = OB_ERR_UNEXPECTED;
      } else if (OB_FAIL(frag_free_list_.push_back(frag))) {
      } else {
        if (OB_FAIL(task.insert_value_data_.push_back(
                      ObString(frag->frag_pos - queue_top_begin_point_.frag_data_pos_,
                               frag->data + queue_top_begin_point_.frag_data_pos_)))) {
        } else if (OB_FAIL(task.source_frag_.push_back(frag))) {
        }

        task.data_size_ += frag->orig_data_size;
      }
    }
    queue_top_begin_point_ = new_top_begin_point;
  }

  task.row_count_ = batch_row_count;


  return ret;
}

int ObDataFragMgr::free_unused_datafrag()
{
  int ret = OB_SUCCESS;

  for (int64_t i = 0; OB_SUCC(ret) && i < tablet_ids_.count(); ++i) {
    ObTabletID tablet_id = tablet_ids_[i];
    ObPartDataFragMgr *part_data_frag = NULL;

    if (OB_FAIL(get_part_datafrag(tablet_id, part_data_frag))) {
    } else if (OB_ISNULL(part_data_frag)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("part data frag is null", K(ret));
    } else if (OB_FAIL(part_data_frag->free_frags())) {
    }
  }

  return ret;
}


int ObDataFragMgr::clear_all_datafrag()
{
  int ret = OB_SUCCESS;

  for (int64_t i = 0; OB_SUCC(ret) && i < tablet_ids_.count(); ++i) {
    ObTabletID tablet_id = tablet_ids_[i];
    ObPartDataFragMgr *part_data_frag = NULL;

    if (OB_FAIL(get_part_datafrag(tablet_id, part_data_frag))) {
    } else if (OB_ISNULL(part_data_frag)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("part data frag is null", K(ret));
    } else if (OB_FAIL(part_data_frag->clear())) {
    } else {
      part_data_frag->~ObPartDataFragMgr();
    }
  }

  return ret;
}

int ObDataFragMgr::init(ObExecContext &ctx, uint64_t table_id)
{
  int ret = OB_SUCCESS;
  ObSchemaGetterGuard *schema_guard = NULL;
  const ObTableSchema *table_schema = NULL;
  ObSEArray<ObObjectID, 4> part_ids;
  tablet_ids_.reset();
  if (OB_ISNULL(ctx.get_sql_ctx())
      || OB_ISNULL(schema_guard = ctx.get_sql_ctx()->schema_guard_)
      || OB_ISNULL(ctx.get_my_session())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("sql ctx is null", K(ret), KP(ctx.get_sql_ctx()));
  } else if (OB_FAIL(schema_guard->get_table_schema(
             table_id, table_schema))) {
  } else if (OB_ISNULL(table_schema)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("table schema is NULL", K(ret));
  } else if (OB_FAIL(table_schema->get_all_tablet_and_object_ids(tablet_ids_, part_ids))) {
  } else {
    LOG_INFO("table partition ids", K(tablet_ids_));
    total_part_cnt_ = tablet_ids_.count();
  }

  for (int64_t i = 0; OB_SUCC(ret) && i < tablet_ids_.count(); ++i) {
    ObTabletID tablet_id = tablet_ids_[i];
    ObPartDataFragMgr *part_data_frag = NULL;

    if (OB_ISNULL(part_data_frag
                  = OB_NEWx(ObPartDataFragMgr,
                            (&ctx.get_allocator()),
                            *this,
                            tablet_id))) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
      LOG_WARN("allocate memory failed", K(ret));
    } else if (FALSE_IT(part_data_frag->tablet_id_ = tablet_id)) {
    } else if (OB_FAIL(part_datafrag_map_.set_refactored(part_data_frag))) {
    } else if (OB_FAIL(part_bitset_.add_member(i))) {
    }
  }

  if (OB_SUCC(ret)) {

    attr_.label_ = common::ObModIds::OB_SQL_LOAD_DATA;
    //attr_.ctx_id_ = common::ObCtxIds::WORK_AREA;
    total_alloc_cnt_ = 0;
    total_free_cnt_ = 0;
  }

  return ret;
}

int ObDataFragMgr::get_part_datafrag(ObTabletID tablet_id,
                                     ObPartDataFragMgr *&part_datafrag_mgr)
{
  return part_datafrag_map_.get_refactored(tablet_id, part_datafrag_mgr);
}

int ObDataFragMgr::create_datafrag(ObDataFrag *&frag, int64_t min_len) {
  int ret = OB_SUCCESS;
  frag = NULL;
  void *buf = NULL;
  int64_t min_alloc_size = sizeof(ObDataFrag) + min_len;

  if (OB_ISNULL(buf = ob_malloc(min_alloc_size, attr_))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    LOG_WARN("fail to malloc", K(ret), KP(this));
  } else {
    frag = new(buf) ObDataFrag(min_alloc_size);
    ATOMIC_AAF(&total_alloc_cnt_, 1);
  }
  return ret;
}

void ObDataFragMgr::distory_datafrag(ObDataFrag *frag) {
  if (OB_ISNULL(frag)) {
    //do nothing
  } else {
    frag->~ObDataFrag();
    ob_free(frag);
    total_free_cnt_++;
  }
}

int ObLoadFileDataTrimer::expand_buf(ObIAllocator &allocator)
{
  int ret = OB_SUCCESS;

  int64_t new_buf_len = 0;

  if (NULL == incomplate_data_) {
    new_buf_len = ObLoadFileBuffer::MAX_BUFFER_SIZE;
  } else {
    new_buf_len = incomplate_data_buf_len_ * 2;
  }

  char *new_buf = NULL;
  if (OB_ISNULL(new_buf = static_cast<char*>(allocator.alloc(new_buf_len)))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    LOG_WARN("no memory", K(ret));
  } else {
    if (NULL != incomplate_data_) {
      MEMCPY(new_buf, incomplate_data_, incomplate_data_len_);
    }
    incomplate_data_ = new_buf;
    incomplate_data_buf_len_ = new_buf_len;
  }
  return ret;
}

int ObLoadFileDataTrimer::init(ObIAllocator &allocator, const ObCSVFormats &formats)
{
  formats_ = formats;
  return expand_buf(allocator);
}

int ObLoadDataSPImpl::ToolBox::release_resources()
{
  int ret = OB_SUCCESS;

  if (gid.is_valid()) {
    ObLoadDataStat *job_status = nullptr;
    if (OB_FAIL(ObGlobalLoadDataStatMap::getInstance()->unregister_job(gid, job_status))) {
    } else if (OB_ISNULL(job_status)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_ERROR("fail to unregister job", K(ret), K(gid));
    } else {
      int64_t log_print_cnt = 0;
      int64_t ref_cnt = 0;
      while ((ref_cnt = job_status->get_ref_cnt()) > 0) {
        ob_usleep(WAIT_INTERVAL_US); //1s
        if ((log_print_cnt++) % 10 == 0) {
          LOG_WARN("LOAD DATA wait job handle release",
                   K(ret), "wait_seconds", log_print_cnt * 10, K(gid), K(ref_cnt));
        }
      }
      job_status->~ObLoadDataStat();
    }
  }

  int tmp_ret = data_frag_mgr.clear_all_datafrag();
  if (OB_SUCCESS != tmp_ret) {
    LOG_WARN("fail to clear all data frag", K(tmp_ret));
    if (OB_SUCC(ret)) {
      ret = tmp_ret;
    }
  }

  if (OB_NOT_NULL(expr_buffer)) {
    ob_free(expr_buffer);
  }

  //release file reader
  if (OB_NOT_NULL(file_reader)) {
    ObFileReader::destroy(file_reader);
    file_reader = NULL;
  }

  if (OB_NOT_NULL(shuffle_handle)) {
    shuffle_handle->~ObShuffleTaskHandle();
    shuffle_handle = NULL;
  }

  return ret;
}

int ObLoadDataSPImpl::ToolBox::build_calc_partid_expr(ObExecContext &ctx,
                                                      ObLoadDataStmt &load_stmt,
                                                      ObTempExpr *&calc_tablet_id_expr)
{
  int ret = OB_SUCCESS;
  ParamStore paramstore(ObWrapperAllocator(ctx.get_allocator()));
  ObInsertStmt *insert_stmt = nullptr;
  ObSqlString insert_sql;
  ObSEArray<ObString, 16> column_names;
  ObLoadArgument &load_args = load_stmt.get_load_arguments();
  bool need_online_osg = false;

  for (int i = 0; OB_SUCC(ret) && i < insert_infos.count(); ++i) {
    OZ (column_names.push_back(insert_infos.at(i).column_name_));
  }
  OZ (ObLoadDataUtils::check_need_opt_stat_gather(ctx, load_stmt, need_online_osg));
  OZ (ObLoadDataUtils::build_insert_sql_string_head(load_args.dupl_action_,
                                                    load_args.combined_name_,
                                                    column_names,
                                                    insert_sql,
                                                    need_online_osg));
  OZ (insert_sql.append(" VALUES("));
  for (int i = 0; OB_SUCC(ret) && i < insert_infos.count(); ++i) {
    if (i != 0) {
      OZ (insert_sql.append(","));
    }
    OZ (insert_sql.append_fmt("'%d'", i));
  }
  OZ (insert_sql.append(")"));

  OZ (ObLoadDataBase::make_parameterize_stmt(ctx, insert_sql, paramstore, insert_stmt));

  if (OB_SUCC(ret)) {
    ObIArray<ObRawExpr*> &column_convert_exprs = insert_stmt->get_column_conv_exprs();
    ObIArray<ObColumnRefRawExpr*> &column_exprs = insert_stmt->get_insert_table_info().column_exprs_;
    ObRawExpr *part_expr = nullptr;
    ObRawExpr *subpart_expr = nullptr;
    ObRawExpr *calc_partid_expr = NULL;
    ObTempExpr *temp_expr = nullptr;
    TableItem *table_item = nullptr;
    RowDesc row_desc;
    ObSEArray<ObRawExpr *, 16> insert_columns;
    ObSEArray<ObRawExpr *, 16> value_mock_columns;
    ObSEArray<ObRawExpr *, 16> field_exprs;
    ObSEArray<ObRawExpr *, 16> insert_exprs;

    if (insert_stmt->get_table_items().count() != 1
        || OB_ISNULL(table_item = insert_stmt->get_table_items().at(0))) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("unexpected table items", K(ret));
    } else {
      if (schema::PARTITION_LEVEL_ZERO != load_args.part_level_) {
        part_expr = insert_stmt->get_part_expr(table_item->table_id_, table_item->ref_id_);
        if (schema::PARTITION_LEVEL_ONE != load_args.part_level_) {
          subpart_expr = insert_stmt->get_subpart_expr(table_item->table_id_, table_item->ref_id_);
        }
      }
    }

    for (int i = 0; OB_SUCC(ret) && i < num_of_file_column; i++) {
      ObColumnRefRawExpr *field_expr = nullptr;
      if (OB_FAIL(ctx.get_expr_factory()->create_raw_expr(T_REF_COLUMN, field_expr))) {
      } else if (OB_ISNULL(field_expr)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN(("field_expr is null"));
      } else {
        field_expr->set_data_type(ObVarcharType);
        field_expr->set_collation_type(load_args.file_cs_type_);
        field_expr->set_column_attr("__field", ObCharsetUtils::get_const_str(CS_TYPE_UTF8MB4_BIN, '0' + i));
        if (OB_FAIL(field_expr->add_flag(IS_COLUMN))) {
        } else if (OB_FAIL(field_exprs.push_back(field_expr))) {
        }
      }
    }

    if (OB_SUCC(ret)) {
      ObRawExprCopier copier(*ctx.get_expr_factory());
      ReplaceVariables replacer(ctx, load_stmt, field_exprs);
      for (int i = 0; OB_SUCC(ret) && i < insert_infos.count(); ++i) {
        ObRawExpr *insert_expr = nullptr;
        ObLoadTableColumnDesc &desc = insert_infos.at(i);
        if (OB_NOT_NULL(desc.expr_value_)) {
          OZ (copier.copy_on_replace(desc.expr_value_, insert_expr, &replacer));
        } else {
          insert_expr = field_exprs.at(desc.array_ref_idx_);
        }
        OZ (insert_exprs.push_back(insert_expr));
      }
    }

    OZ (row_desc.init());

    for (int i = 0; OB_SUCC(ret) && i < field_exprs.count(); i++) {
      if (OB_FAIL(row_desc.add_column(field_exprs.at(i)))) {
      }
    }

    for (int i = 0; OB_SUCC(ret) && i < insert_stmt->get_values_desc().count(); i++) {
      OZ (value_mock_columns.push_back(insert_stmt->get_values_desc().at(i)));
    }

    for (int i = 0; OB_SUCC(ret) && i < column_exprs.count(); i++) {
      OZ (insert_columns.push_back(column_exprs.at(i)));
    }

    if (OB_SUCC(ret)) {
      if (OB_FAIL(ObRawExprUtils::build_calc_tablet_id_expr(*ctx.get_expr_factory(),
                                                          *ctx.get_my_session(),
                                                          load_args.table_id_,
                                                          load_args.part_level_,
                                                          part_expr,
                                                          subpart_expr,
                                                          calc_partid_expr))) {
      } else if (OB_FAIL(ObTransformUtils::replace_exprs(value_mock_columns,
                                                         insert_exprs,
                                                         column_convert_exprs))) {
      } else if (OB_FAIL(ObTransformUtils::replace_expr(insert_columns,
                                                        column_convert_exprs,
                                                        calc_partid_expr))) {
      } else if (OB_FAIL(calc_partid_expr->formalize(ctx.get_my_session()))) {
      }
    }

    if (OB_SUCC(ret)) {
      if (OB_ISNULL(ctx.get_sql_ctx())) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("sql ctx is null", K(ret));
      } else if (OB_FAIL(ObStaticEngineExprCG::gen_expr_with_row_desc(calc_partid_expr,
                                                               row_desc,
                                                               ctx.get_allocator(),
                                                               ctx.get_my_session(),
                                                               ctx.get_sql_ctx()->schema_guard_,
                                                               temp_expr))) {
      } else {
        calc_tablet_id_expr = temp_expr;
      }
    }

    if (OB_SUCC(ret)) {
      if (OB_ISNULL(ctx.get_physical_plan_ctx())) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("plan ctx is null", K(ret));
      } else {
        ctx.get_physical_plan_ctx()->set_autoinc_params(insert_stmt->get_autoinc_params());
      }
    }

    if (OB_SUCC(ret)) {
      bool part_key_has_autoinc = false;
      OZ (insert_stmt->part_key_has_auto_inc(part_key_has_autoinc));
      if (part_key_has_autoinc) {
        calc_tablet_id_expr = NULL;
      }
    }


    LOG_DEBUG("LOAD DATA check insert info",
              K(column_convert_exprs), K(column_exprs), KPC(calc_partid_expr),
              KPC(part_expr), KPC(subpart_expr),
              K(insert_stmt->get_values_vector()),
              K(insert_stmt->get_values_desc()));
  }

  return ret;
}

int ObLoadDataSPImpl::ToolBox::init(ObExecContext &ctx, ObLoadDataStmt &load_stmt)
{
  int ret = OB_SUCCESS;
  const ObLoadArgument &load_args = load_stmt.get_load_arguments();
  const ObDataInFileStruct &file_formats = load_stmt.get_data_struct_in_file();
  const ObLoadDataHint &hint = load_stmt.get_hints();
  bool need_online_osg = false;

  formats.init(file_formats);
  wait_secs_for_mem_release = 0;
  affected_rows = 0;
  insert_rt_sum = 0;
  suffle_rt_sum = 0;
  shuffle_task_count = 0;
  insert_task_count = 0;
  data_frag_buffer_count_limit = 50;
  insert_mode = load_args.dupl_action_;
  load_file_storage = load_args.load_file_storage_;
  ignore_rows = load_args.ignore_rows_;

  ObSQLSessionInfo *session = NULL;
  ObTempExpr *calc_tablet_id_expr = nullptr;

  if (OB_ISNULL(session = ctx.get_my_session()) ||
      OB_ISNULL(ctx.get_sql_ctx())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("session is null", K(ret));
  } else if (OB_FAIL(data_trimer.init(ctx.get_allocator(), formats))) {
  } else if (OB_FAIL(gen_load_table_column_desc(ctx, load_stmt, insert_infos))) {
  } else if (OB_FAIL(ObLoadDataUtils::check_need_opt_stat_gather(ctx, load_stmt, need_online_osg))) {
  } else if (OB_FAIL(gen_insert_columns_names_buff(ctx, load_args,
                                                   insert_infos,
                                                   insert_stmt_head_buff,
                                                   need_online_osg))) {
  } else if (OB_FAIL(data_frag_mgr.init(ctx, load_args.table_id_))) {
  }

  if (OB_SUCC(ret) && OB_FAIL(file_iter.copy(load_args.file_iter_))) {
    LOG_WARN("failed to copy file iter", K(ret));
  }

  if (OB_SUCC(ret)) {
  // init file read param except filename
    file_read_param.file_location_      = load_file_storage;
    // file_read_param.filename_           = load_args.file_name_;
    file_read_param.compression_format_ = load_args.compression_format_;
    file_read_param.packet_handle_      = nullptr;
    if (OB_NOT_NULL(ctx.get_my_session()) && OB_NOT_NULL(ctx.get_my_session()->get_pl_query_sender())) {
      file_read_param.packet_handle_ = &ctx.get_my_session()->get_pl_query_sender()->get_packet_sender();
    }
    file_read_param.session_            = ctx.get_my_session();
    file_read_param.timeout_ts_         = THIS_WORKER.get_timeout_ts();
  }

  OZ (init_file_size(ctx));

  for (int64_t i = 0; OB_SUCC(ret) && i < insert_infos.count(); ++i) {
    const ObLoadTableColumnDesc &desc = insert_infos.at(i);
    if (!desc.is_set_values_ && (ob_is_string_tc(desc.column_type_) || ob_is_enumset_tc(desc.column_type_))) {
      if (OB_FAIL(string_type_column_bitset.add_member(i))) {
      }
    }
  }

  if (OB_SUCC(ret)) {
    void *buf = NULL;
    num_of_file_column = load_stmt.get_field_or_var_list().count();
    num_of_table_column = insert_infos.count();
    if (OB_ISNULL(buf = ob_malloc(ObLoadFileBuffer::MAX_BUFFER_SIZE,
                                         ObMemAttr(ObModIds::OB_SQL_LOAD_DATA)))) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
      LOG_WARN("allocate memory failed", K(ret));
    } else if (FALSE_IT(expr_buffer = new(buf) ObLoadFileBuffer(
                          ObLoadFileBuffer::MAX_BUFFER_SIZE - sizeof(ObLoadFileBuffer)))) {
    }
  }

  if (OB_SUCC(ret)) {
    plan.set_vars(ctx.get_stmt_factory()->get_query_ctx()->variables_);
    ctx.get_my_session()->set_cur_phy_plan(&plan);
    OX(ctx.reference_my_plan(&plan));
    OZ(ctx.init_phy_op(1));


    if (OB_SUCC(ret) && load_args.part_level_ != PARTITION_LEVEL_ZERO) {
      if (OB_FAIL(build_calc_partid_expr(ctx, load_stmt, calc_tablet_id_expr))) {
      }
    }

  }

  if (OB_SUCC(ret)) {
    int64_t hint_batch_size = 0;
    int64_t hint_max_batch_buffer_size = 0;
    ObString hint_batch_buffer_size_str;
    if (OB_FAIL(hint.get_value(ObLoadDataHint::BATCH_SIZE, hint_batch_size))) {
    } else if (0 == hint_batch_size) {
      batch_row_count = DEFAULT_BUFFERRED_ROW_COUNT;
    } else {
      batch_row_count = std::max(static_cast<int64_t>(1), std::min(DEFAULT_BUFFERRED_ROW_COUNT, hint_batch_size));
    }
    if (OB_SUCC(ret)) {
      if (OB_FAIL(hint.get_value(ObLoadDataHint::BATCH_BUFFER_SIZE, hint_batch_buffer_size_str))) {
      } else {
        bool is_valid = false;
        hint_batch_buffer_size_str = hint_batch_buffer_size_str.trim();
        if (!hint_batch_buffer_size_str.empty()) {
          ObCStringHelper helper;
          hint_max_batch_buffer_size = ObConfigCapacityParser::get(helper.convert(hint_batch_buffer_size_str), is_valid);
        }
        if (!is_valid) {
          hint_max_batch_buffer_size = 1L << 30; // 1G
        }
        batch_buffer_size = MAX(ObLoadFileBuffer::MAX_BUFFER_SIZE, hint_max_batch_buffer_size);
      }
    }
  }

  if (OB_SUCC(ret)) {
    int64_t query_timeout = 0;
    if (OB_FAIL(hint.get_value(ObLoadDataHint::QUERY_TIMEOUT, query_timeout))) {
    } else if (0 == query_timeout) {
      if (OB_FAIL(ctx.get_my_session()->get_query_timeout(query_timeout))) {
      } else {
        THIS_WORKER.set_timeout_ts(ctx.get_my_session()->get_query_start_time() + query_timeout);
      }
    } else if (query_timeout > 0) {
      THIS_WORKER.set_timeout_ts(ctx.get_my_session()->get_query_start_time() + query_timeout);
    }
  }

  if (OB_SUCC(ret)) {
    if (OB_FAIL(parser.init(file_formats, num_of_file_column, load_args.file_cs_type_))) {
    }
  }

  if (OB_SUCC(ret)) {
    if (OB_ISNULL(shuffle_handle = OB_NEWx(ObShuffleTaskHandle, (&ctx.get_allocator()),
                                           ctx, data_frag_mgr, string_type_column_bitset))) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
      LOG_WARN("fail to allocate local shuffle handle", K(ret));
    } else if (OB_FAIL(shuffle_handle->expand_buf(batch_buffer_size,
                                                  ObLoadFileBuffer::MAX_BUFFER_SIZE))) {
    } else if (OB_FAIL(shuffle_handle->parser.init(file_formats,
                                                   num_of_file_column,
                                                   load_args.file_cs_type_))) {
    } else if (OB_FAIL(shuffle_handle->generator.set_params(insert_stmt_head_buff,
                                                             load_args.file_cs_type_,
                                                             session->get_sql_mode()))) {
    } else if (OB_FAIL(copy_exprs_for_shuffle_task(ctx, load_stmt, insert_infos,
                                                   shuffle_handle->generator.get_field_exprs(),
                                                   shuffle_handle->generator.get_insert_exprs()))) {
    } else {
      shuffle_handle->calc_tablet_id_expr = calc_tablet_id_expr;
      ObObj *obj_array = static_cast<ObObj *>(
          shuffle_handle->allocator.alloc(sizeof(ObObj) * num_of_file_column));
      if (OB_ISNULL(obj_array)) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_WARN("fail to allocate local shuffle row", K(ret));
      } else {
        for (ObObj *ptr = obj_array; ptr < obj_array + num_of_file_column; ++ptr) {
          new(ptr) ObObj();
          ptr->set_type(ObVarcharType);
          ptr->set_collation_type(load_args.file_cs_type_);
        }
        shuffle_handle->row_in_file.assign(obj_array, num_of_file_column);
      }
    }
  }

  constexpr const char* dict = "0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ";
  constexpr int word_base = 62; //length of dict
  const int64_t file_id_len = 6;
  int64_t cur_ts = ObTimeUtil::current_time();


  if (OB_SUCC(ret)) {
    char *buf = NULL;
    static const char* loadlog_str = "log/obloaddata.log.";
    int64_t pre_len = strlen(loadlog_str);
    int64_t buf_len = file_id_len + pre_len;
    int64_t pos = 0;

    if (OB_ISNULL(buf = static_cast<char*>(ctx.get_allocator().alloc(buf_len)))) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
      LOG_WARN("no memory", K(ret), K(buf_len));
    } else {
      MEMCPY(buf + pos, loadlog_str, pre_len);
      pos += pre_len;
      uint32_t hash_ts = ::murmurhash2(&cur_ts, sizeof(cur_ts), 0);
      for (int i = 0; i < file_id_len && pos < buf_len; ++i) {
        buf[pos++] = dict[hash_ts % word_base];
        hash_ts /= word_base;
      }
    }
    if (OB_SUCC(ret)) {
      log_file_name = ObString(pos, buf);
    }
  }

  if (OB_SUCC(ret)) {
    const int64_t fake_file_size = (file_size > 0) ? file_size : (2 << 30); // use 2G as default in load local mode
    int64_t max_task_count = (fake_file_size / ObLoadFileBuffer::MAX_BUFFER_SIZE + 1) * 2;
    file_buf_row_num.set_attr(ObMemAttr(ObModIds::OB_SQL_LOAD_DATA));
    if (OB_FAIL(file_buf_row_num.reserve(max_task_count))) {
    }
  }

  int64_t buf_len = DEFAULT_BUF_LENGTH;
  bool need_extend = true;
  while (OB_SUCC(ret) && need_extend) {
    char *buf = NULL;
    int64_t pos = 0;
    buf_len *= 2;
    if (OB_ISNULL(buf = static_cast<char*>(ctx.get_allocator().alloc(buf_len)))) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
      LOG_WARN("no memory", K(ret), K(buf_len));
    } else {
      const ObString &cur_query_str = ctx.get_my_session()->get_current_query_string();
      char trace_id_buf[OB_MAX_TRACE_ID_BUFFER_SIZE] = {'\0'};
      OZ (databuff_printf(buf, buf_len, pos,
                          "File name:\t%.*s\n"
                          "Into table:\t%.*s\n"
                          "Batch size:\t%ld\n"
                          "SQL trace:\t%s\n",
                          load_args.file_name_.length(), load_args.file_name_.ptr(),
                          load_args.combined_name_.length(), load_args.combined_name_.ptr(),
                          batch_row_count,
                          ObCurTraceId::get_trace_id_str(trace_id_buf, sizeof(trace_id_buf))
                          ));
      OZ (databuff_printf(buf, buf_len, pos, "Start time:\t"));
      OZ (ObTimeConverter::datetime_to_str(cur_ts,
                                           TZ_INFO(session),
                                           MAX_SCALE_FOR_TEMPORAL,
                                           buf, buf_len, pos, true));
      OZ (databuff_printf(buf, buf_len, pos, "\n"));
      OZ (databuff_printf(buf, buf_len, pos, "Load query: \n%.*s\n",
                                cur_query_str.length(), cur_query_str.ptr()));
      OX (load_info.assign_ptr(buf, pos));
    }
    if (OB_SUCC(ret)) {
      need_extend = false;
    } else {
      if (OB_SIZE_OVERFLOW == ret) {
        ret = OB_SUCCESS;
        need_extend = true;
      }
    }
  }

  if (OB_SUCC(ret)) {
    job_status = nullptr;
    if (OB_ISNULL(job_status = OB_NEWx(ObLoadDataStat, (&ctx.get_allocator())))) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
      LOG_WARN("fail to allocate memory", K(ret));
    } else {
      ObLoadDataGID temp_gid;
      ObLoadDataGID::generate_new_id(temp_gid);

      job_status->job_id_ = temp_gid.id;

      OZ(ob_write_string(job_status->allocator_,
                         load_args.combined_name_, job_status->table_name_));
      OZ(ob_write_string(job_status->allocator_,
                         load_args.file_name_, job_status->file_path_));
      job_status->file_column_ = num_of_file_column;
      job_status->table_column_ = num_of_table_column;
      job_status->batch_size_ = batch_row_count;
      job_status->parallel_ = 1;
      job_status->load_mode_ = static_cast<int64_t>(insert_mode);
      job_status->start_time_ = common::ObTimeUtility::current_time();
      job_status->total_bytes_ = file_size;
      if (OB_FAIL(ObGlobalLoadDataStatMap::getInstance()->register_job(temp_gid, job_status))) {
      } else {
        gid = temp_gid;
      }
    }
  }

  return ret;
}

int ObLoadDataSPImpl::ToolBox::open_file(ObString filename, ObExecContext &ctx)
{
  int ret = OB_SUCCESS;

  // the other params inited in the ToolBox::init
  file_read_param.filename_ = filename;

  if (OB_NOT_NULL(file_reader)) {
    ObFileReader::destroy(file_reader);
    file_reader = nullptr;
  }

  if (OB_FAIL(ObFileReader::open(file_read_param, ctx.get_allocator(), file_reader))) {
  } else {
    read_cursor.read_size_ = 0;
    read_cursor.is_end_file_ = false;
    data_trimer.reset_current_file_line_cnt();
  }
  return ret;
}

int ObLoadDataSPImpl::ToolBox::init_file_size(ObExecContext &ctx)
{
  int ret = OB_SUCCESS;
  if (file_read_param.file_location_ == ObLoadFileLocation::CLIENT_DISK) {
    file_size = -1;
  } else {
    ObString filename;
    file_size = 0;
    while (OB_SUCC(ret) && OB_SUCC(file_iter.get_next_file(filename))) {
      int64_t this_file_size = 0;
      if (OB_FAIL(open_file(filename, ctx))) {
      } else if (OB_ISNULL(file_reader)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("open file return success but got null", KP(file_reader), K(ret));
      } else if (!file_reader->seekable()) {
        file_size = -1;
        ret = OB_ITER_END;
      } else if (OB_FAIL(file_reader->get_file_size(this_file_size))) {
      } else {
        file_size += this_file_size;
      }
    }

    if (OB_ITER_END == ret) {
      ret = OB_SUCCESS;
    }
    file_iter.rewind();
  }

  return ret;
}

} // sql
} // oceanbase
