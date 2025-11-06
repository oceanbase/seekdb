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

#define USING_LOG_PREFIX PL
#include "ob_pl_parser.h"
#include "pl/ob_pl_resolver.h"
#include "sql/parser/parse_malloc.h"


#ifdef __cplusplus
extern "C" {
#endif
extern int obpl_parser_init(ObParseCtx *parse_ctx);
extern int obpl_parser_parse(ObParseCtx *parse_ctx);
extern ParseNode *merge_tree(void *malloc_pool, int *fatal_error, ObItemType node_tag, ParseNode *source_tree);
int obpl_parser_check_stack_overflow() {
  int ret = OB_SUCCESS;
  bool is_overflow = true;
  if (OB_FAIL(check_stack_overflow(is_overflow))) {
    LOG_WARN("failed to check stack overflow status", K(ret));
  }
  return is_overflow;
}
#ifdef __cplusplus
}
#endif

namespace oceanbase
{
using namespace common;
namespace pl
{

#define ISSPACE(c) ((c) == ' ' || (c) == '\n' || (c) == '\r' || (c) == '\t' || (c) == '\f' || (c) == '\v')
int ObPLParser::fast_parse(const ObString &query,
                      ParseResult &parse_result)
{
  ACTIVE_SESSION_FLAG_SETTER_GUARD(in_pl_parse);
  int ret = OB_SUCCESS;
  // Remove spaces at the end of the SQL statement
  int64_t len = query.length();
  while (len > 0 && ISSPACE(query[len - 1])) {
    --len;
  }
  const ObString stmt_block(len, query.ptr());
  ObParseCtx parse_ctx;
  memset(&parse_ctx, 0, sizeof(ObParseCtx));
  parse_ctx.global_errno_ = OB_SUCCESS;
  parse_ctx.is_pl_fp_ = true;
  parse_ctx.mem_pool_ = &allocator_;
  parse_ctx.stmt_str_ = stmt_block.ptr();
  parse_ctx.stmt_len_ = stmt_block.length();
  parse_ctx.orig_stmt_str_ = query.ptr();
  parse_ctx.orig_stmt_len_ = query.length();
  parse_ctx.comp_mode_ = false;
  parse_ctx.is_for_trigger_ = 0;
  parse_ctx.is_dynamic_ = 0;
  parse_ctx.is_inner_parse_ = 1;
  parse_ctx.charset_info_ = ObCharset::get_charset(charsets4parser_.string_collation_);
  parse_result.charset_info_oracle_db_ = ObCharset::is_valid_collation(charsets4parser_.nls_collation_) ?
          ObCharset::get_charset(charsets4parser_.nls_collation_) : NULL;
  parse_ctx.is_not_utf8_connection_ = ObCharset::is_valid_collation(charsets4parser_.string_collation_) ?
        (ObCharset::charset_type_by_coll(charsets4parser_.string_collation_) != CHARSET_UTF8MB4) : false;
  parse_ctx.connection_collation_ = charsets4parser_.string_collation_;
  parse_ctx.mysql_compatible_comment_ = false;
  int64_t new_length = stmt_block.length() + 1;
  char *buf = (char *)parse_malloc(new_length, parse_ctx.mem_pool_);
  if (OB_UNLIKELY(NULL == buf)) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    LOG_ERROR("no memory for parser");
  } else {
    parse_ctx.no_param_sql_ = buf;
    parse_ctx.no_param_sql_buf_len_ = new_length;
  }
  if (OB_SUCC(ret)) {
    if (OB_FAIL(parse_stmt_block(parse_ctx, parse_result.result_tree_))) {
      if (OB_ERR_PARSE_SQL == ret) {
        int err_len = 0;
        const char *err_str = "", *global_errmsg = "";
        int err_line = 0;
        if (parse_ctx.cur_error_info_ != NULL) {
          int first_column = parse_ctx.cur_error_info_->stmt_loc_.first_column_;
          int last_column = parse_ctx.cur_error_info_->stmt_loc_.last_column_;
          err_len = last_column - first_column + 1;
          err_str = parse_ctx.stmt_str_ + first_column;
          err_line = parse_ctx.cur_error_info_->stmt_loc_.last_line_ + 1;
          global_errmsg = parse_ctx.global_errmsg_;
        }
        ObString stmt(MIN(MAX_PRINT_LEN, parse_ctx.stmt_len_), parse_ctx.stmt_str_);
        LOG_WARN("failed to parse pl stmt",
                K(ret), K(err_line), K(global_errmsg), K(stmt));
        LOG_USER_ERROR(OB_ERR_PARSE_SQL, ob_errpkt_strerror(OB_ERR_PARSER_SYNTAX, false),
                      err_len, err_str, err_line);
      } else {
        LOG_WARN("failed to parse pl stmt", K(ret));
      }
    } else {
      int64_t buf_remain_len = parse_ctx.no_param_sql_buf_len_ - parse_ctx.no_param_sql_len_;
      int64_t copy_len = parse_ctx.stmt_len_ - parse_ctx.copied_pos_;
      if (buf_remain_len < copy_len) {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("Can not memmove due to remain buf len less than copy len",
                K(ret), K(buf_remain_len), K(copy_len));
      } else {
        memmove(parse_ctx.no_param_sql_ + parse_ctx.no_param_sql_len_,
                      parse_ctx.stmt_str_ + parse_ctx.copied_pos_, 
                      copy_len);
        parse_ctx.no_param_sql_len_ += copy_len;
        parse_result.no_param_sql_ = parse_ctx.no_param_sql_;
        parse_result.no_param_sql_len_ = parse_ctx.no_param_sql_len_;
        parse_result.no_param_sql_buf_len_ = parse_ctx.no_param_sql_buf_len_;
        parse_result.param_node_num_ = parse_ctx.param_node_num_;
        parse_result.param_nodes_ = parse_ctx.param_nodes_;
        parse_result.tail_param_node_ = parse_ctx.tail_param_node_;
        parse_result.contain_sensitive_data_ = parse_ctx.contain_sensitive_data_;
      }
    }
  }
  return ret;
}

int ObPLParser::parse(const ObString &stmt_block,
                      const ObString &orig_stmt_block, // for preprocess
                      ParseResult &parse_result,
                      bool is_inner_parse)
{
  ACTIVE_SESSION_FLAG_SETTER_GUARD(in_pl_parse);
  int ret = OB_SUCCESS;
  bool is_include_old_new_in_trigger = false;
  bool contain_sensitive_data = false;
  ObQuestionMarkCtx question_mark_ctx;
  if (OB_FAIL(parse_procedure(stmt_block,
                              orig_stmt_block,
                              parse_result.result_tree_,
                              question_mark_ctx,
                              parse_result.is_for_trigger_,
                              parse_result.is_dynamic_sql_,
                              is_inner_parse,
                              is_include_old_new_in_trigger,
                              contain_sensitive_data))) {
    if (OB_SIZE_OVERFLOW != ret) {
      LOG_WARN("parse stmt block failed", K(ret), K(ObString(MIN(MAX_PRINT_LEN, stmt_block.length()) ,stmt_block.ptr())),
                                                  K(ObString(MIN(MAX_PRINT_LEN, orig_stmt_block.length()) ,orig_stmt_block.ptr())));
    } else {
      LOG_WARN("parse stmt block failed", K(ret));
    }
  } else if (OB_ISNULL(parse_result.result_tree_)) {
    ret = OB_ERR_PARSE_SQL;
    LOG_WARN("result tree is NULL", K(stmt_block), K(ret));
  } else {
    parse_result.question_mark_ctx_ = question_mark_ctx;
    parse_result.input_sql_ = stmt_block.ptr();
    parse_result.input_sql_len_ = stmt_block.length();
    parse_result.no_param_sql_ = const_cast<char*>(stmt_block.ptr());
    parse_result.no_param_sql_len_ = stmt_block.length();
    parse_result.end_col_ = stmt_block.length();
    parse_result.is_include_old_new_in_trigger_ = is_include_old_new_in_trigger;
    parse_result.contain_sensitive_data_ = contain_sensitive_data;
  }
  return ret;
}

int ObPLParser::parse_procedure(const ObString &stmt_block,
                                const ObString &orig_stmt_block,
                                ObStmtNodeTree *&multi_stmt,
                                ObQuestionMarkCtx &question_mark_ctx,
                                bool is_for_trigger,
                                bool is_dynamic,
                                bool is_inner_parse,
                                bool &is_include_old_new_in_trigger,
                                bool &contain_sensitive_data)
{
  int ret = OB_SUCCESS;
  ObParseCtx parse_ctx;
  memset(&parse_ctx, 0, sizeof(ObParseCtx));
  parse_ctx.global_errno_ = OB_SUCCESS;
  parse_ctx.mem_pool_ = &allocator_;
  parse_ctx.stmt_str_ = stmt_block.ptr();
  parse_ctx.stmt_len_ = stmt_block.length();
  parse_ctx.orig_stmt_str_ = orig_stmt_block.ptr();
  parse_ctx.orig_stmt_len_ = orig_stmt_block.length();
  parse_ctx.comp_mode_ = false;
  parse_ctx.is_for_trigger_ = is_for_trigger ? 1 : 0;
  parse_ctx.is_dynamic_ = is_dynamic ? 1 : 0;
  parse_ctx.is_inner_parse_ = is_inner_parse ? 1 : 0;
  parse_ctx.charset_info_ = ObCharset::get_charset(charsets4parser_.string_collation_);
  parse_ctx.is_not_utf8_connection_ = ObCharset::is_valid_collation(charsets4parser_.string_collation_) ?
        (ObCharset::charset_type_by_coll(charsets4parser_.string_collation_) != CHARSET_UTF8MB4) : false;
  parse_ctx.connection_collation_ = charsets4parser_.string_collation_;
  parse_ctx.scanner_ctx_.sql_mode_ = sql_mode_;

  ret = parse_stmt_block(parse_ctx, multi_stmt);
  if (OB_ERR_PARSE_SQL == ret) {
    int err_len = 0;
    const char *err_str = "", *global_errmsg = "";
    int err_line = 0;
    if (parse_ctx.cur_error_info_ != NULL) {
      int first_column = parse_ctx.cur_error_info_->stmt_loc_.first_column_;
      int last_column = parse_ctx.cur_error_info_->stmt_loc_.last_column_;
      if (0 <= first_column && 0 <= last_column && last_column >= first_column && last_column < parse_ctx.stmt_len_) {
        err_len = last_column - first_column + 1;
        err_str = parse_ctx.stmt_str_ + first_column;
        if (parse_ctx.is_not_utf8_connection_) {
          char *dst_str = NULL;
          uint errors = 0;
          size_t dst_len = err_len * 4;
          size_t out_len = 0;
          if (OB_ISNULL(dst_str = static_cast<char *>(allocator_.alloc(dst_len + 1)))) {
            ret = OB_ALLOCATE_MEMORY_FAILED;
            LOG_WARN("failed to allocate string buffer", K(ret), K(dst_len + 1));
          } else {
            out_len = static_cast<int64_t>(ob_convert(dst_str, dst_len, &ob_charset_utf8mb4_bin, err_str, err_len, parse_ctx.charset_info_, false, '?', &errors));
            if (0 != errors) {
              // The OB_ERR_INCORRECT_STRING_VALUE error code returned after convet fails will cause disconnection.
              // Therefore, the error code is not changed here and the OB_ERR_PARSE_SQL error is still returned. Only the log is printed.
              LOG_WARN("ob_convert failed", K(ret), K(errors), K( parse_ctx.charset_info_), K(ObString(err_len, err_str)));
            } else {
              dst_str[out_len] = '\0';
              err_str = dst_str;
              err_len = out_len;
            }
          }
        }
      } else {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("unexpected first column or last column", K(ret), K(first_column), K(last_column), K(parse_ctx.stmt_len_));
      }
      err_line = parse_ctx.cur_error_info_->stmt_loc_.last_line_ + 1;
      global_errmsg = parse_ctx.global_errmsg_;
    }
    ObString stmt(MIN(MAX_PRINT_LEN, parse_ctx.stmt_len_), parse_ctx.stmt_str_);
    LOG_WARN("failed to parser pl stmt",
             K(ret), K(err_line), K(global_errmsg), K(stmt));
    LOG_USER_ERROR(OB_ERR_PARSE_SQL, ob_errpkt_strerror(OB_ERR_PARSER_SYNTAX, false),
                   err_len, err_str, err_line);
  } else if (parse_ctx.mysql_compatible_comment_) {
    ret = OB_ERR_PARSE_SQL;
    LOG_WARN("the sql is invalid", K(ret), K(ObString(MIN(MAX_PRINT_LEN, stmt_block.length()) ,stmt_block.ptr())));
  } else {
    question_mark_ctx = parse_ctx.question_mark_ctx_;
    is_include_old_new_in_trigger = parse_ctx.is_include_old_new_in_trigger_;
    contain_sensitive_data = parse_ctx.contain_sensitive_data_;
  }
  return ret;
}

int ObPLParser::parse_routine_body(const ObString &routine_body,
                                   ObStmtNodeTree *&routine_stmt,
                                   bool is_for_trigger,
                                   bool need_unwrap)
{
  ACTIVE_SESSION_FLAG_SETTER_GUARD(in_pl_parse);
  int ret = OB_SUCCESS;
  int32_t prefix_len = 0;
  char *buf = NULL;
  const char *prefix = "^ ";  // something totally different from SQL style
  prefix_len = static_cast<int32_t>(strlen(prefix));
  buf = static_cast<char*>(allocator_.alloc(routine_body.length() + prefix_len));
  if (OB_UNLIKELY(NULL == buf)) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    LOG_ERROR("failed to allocate string buffer", "buf_len", routine_body.length() + prefix_len);
  } else {
    MEMCPY(buf, prefix, prefix_len);
    MEMCPY(buf + prefix_len, routine_body.ptr(), routine_body.length());
  }

  if (OB_SUCC(ret)) {
    ObParseCtx parse_ctx;
    memset(&parse_ctx, 0, sizeof(ObParseCtx));
    parse_ctx.global_errno_ = OB_SUCCESS;
    parse_ctx.mem_pool_ = &allocator_;
    parse_ctx.stmt_str_ = buf;
    parse_ctx.stmt_len_ = routine_body.length() + prefix_len;
    parse_ctx.orig_stmt_str_ = buf;
    parse_ctx.orig_stmt_len_ = routine_body.length() + prefix_len;
    parse_ctx.is_inner_parse_ = 1;
    parse_ctx.is_for_trigger_ = is_for_trigger ? 1 : 0;
    parse_ctx.comp_mode_ = false;
    parse_ctx.charset_info_ = ObCharset::get_charset(charsets4parser_.string_collation_);
    parse_ctx.charset_info_oracle_db_ = ObCharset::is_valid_collation(charsets4parser_.nls_collation_) ?
          ObCharset::get_charset(charsets4parser_.nls_collation_) : NULL;
    parse_ctx.is_not_utf8_connection_ = ObCharset::is_valid_collation(charsets4parser_.string_collation_) ?
          (ObCharset::charset_type_by_coll(charsets4parser_.string_collation_) != CHARSET_UTF8MB4) : false;
    parse_ctx.connection_collation_ = charsets4parser_.string_collation_;
    parse_ctx.scanner_ctx_.sql_mode_ = sql_mode_;

    if (OB_FAIL(parse_stmt_block(parse_ctx, routine_stmt))) {
      LOG_WARN("failed to parse stmt block", K(ret));
    }
  }
  return ret;
}

int ObPLParser::parse_package(const ObString &package,
                              ObStmtNodeTree *&package_stmt,
                              const ObDataTypeCastParams &dtc_params,
                              share::schema::ObSchemaGetterGuard *schema_guard,
                              bool is_for_trigger,
                              const ObTriggerInfo *trg_info,
                              bool need_unwrap)
{
  ACTIVE_SESSION_FLAG_SETTER_GUARD(in_pl_parse);
  int ret = OB_SUCCESS;
  ObParseCtx parse_ctx;
  memset(&parse_ctx, 0, sizeof(ObParseCtx));
  parse_ctx.global_errno_ = OB_SUCCESS;
  parse_ctx.mem_pool_ = &allocator_;
  parse_ctx.stmt_str_ = package.ptr();
  parse_ctx.stmt_len_ = package.length();
  parse_ctx.orig_stmt_str_ = package.ptr();
  parse_ctx.orig_stmt_len_ = package.length();
  parse_ctx.comp_mode_ = false;
  parse_ctx.is_inner_parse_ = 1;
  parse_ctx.is_for_trigger_ = is_for_trigger ? 1 : 0;
  parse_ctx.charset_info_ = ObCharset::get_charset(charsets4parser_.string_collation_);
  parse_ctx.charset_info_oracle_db_ = ObCharset::is_valid_collation(charsets4parser_.nls_collation_) ?
        ObCharset::get_charset(charsets4parser_.nls_collation_) : NULL;
  parse_ctx.is_not_utf8_connection_ = ObCharset::is_valid_collation(charsets4parser_.string_collation_) ?
        (ObCharset::charset_type_by_coll(charsets4parser_.string_collation_) != CHARSET_UTF8MB4) : false;
  parse_ctx.connection_collation_ = charsets4parser_.string_collation_;
  parse_ctx.scanner_ctx_.sql_mode_ = sql_mode_;

  if (OB_FAIL(parse_stmt_block(parse_ctx, package_stmt))) {
    LOG_WARN("failed to parse stmt block", K(ret));
  }
  return ret;
}

int ObPLParser::parse_stmt_block(ObParseCtx &parse_ctx, ObStmtNodeTree *&multi_stmt)
{
  int ret = OB_SUCCESS;
  if (0 != obpl_parser_init(&parse_ctx)) {
    ret = OB_ERR_PARSER_INIT;
    LOG_WARN("failed to initialized parser", K(ret));
  } else if (OB_FAIL(obpl_parser_parse(&parse_ctx))) {
    if (OB_ERR_PARSE_SQL == ret && parse_ctx.is_for_preprocess_) {
      LOG_INFO("meet condition syntax, try preparse for condition compile",
               K(ret), K(parse_ctx.is_for_preprocess_));
      ObParseCtx pre_parse_ctx;
      memset(&pre_parse_ctx, 0, sizeof(ObParseCtx));
      pre_parse_ctx.global_errno_ = OB_SUCCESS;
      pre_parse_ctx.mem_pool_ = parse_ctx.mem_pool_;
      pre_parse_ctx.stmt_str_ = parse_ctx.stmt_str_;
      pre_parse_ctx.stmt_len_ = parse_ctx.stmt_len_;
      pre_parse_ctx.orig_stmt_str_ = parse_ctx.stmt_str_;
      pre_parse_ctx.orig_stmt_len_ = parse_ctx.stmt_len_;
      pre_parse_ctx.comp_mode_ = parse_ctx.comp_mode_;
      pre_parse_ctx.is_inner_parse_ = parse_ctx.is_inner_parse_;
      pre_parse_ctx.is_for_trigger_ = parse_ctx.is_for_trigger_;
      pre_parse_ctx.is_for_preprocess_ = true;
      pre_parse_ctx.connection_collation_ = parse_ctx.connection_collation_;
      pre_parse_ctx.scanner_ctx_.sql_mode_ = parse_ctx.scanner_ctx_.sql_mode_;
      pre_parse_ctx.contain_sensitive_data_ = parse_ctx.contain_sensitive_data_;
      if (0 != obpl_parser_init(&pre_parse_ctx)) {
        ret = OB_ERR_PARSER_INIT;
        LOG_WARN("failed to initialized parser", K(ret));
      } else if (OB_FAIL(obpl_parser_parse(&pre_parse_ctx))) {
        LOG_WARN("failed to preparse", K(ret));
      } else {
        OX (multi_stmt = pre_parse_ctx.stmt_tree_);
      }
    }
    if (OB_FAIL(ret)) {
      if (OB_SIZE_OVERFLOW != ret) {
        LOG_WARN("failed to parse the statement",
               K_(parse_ctx.global_errmsg),
               K_(parse_ctx.global_errno),
               K_(parse_ctx.is_for_trigger),
               K_(parse_ctx.is_dynamic),
               K_(parse_ctx.is_for_preprocess),
               "orig_stmt_str", ObString(MIN(MAX_PRINT_LEN, parse_ctx.orig_stmt_len_), parse_ctx.orig_stmt_str_),
               "stmt_str", ObString(MIN(MAX_PRINT_LEN, parse_ctx.stmt_len_), parse_ctx.stmt_str_));
      } else {
        LOG_WARN("failed to parse the statement", K_(parse_ctx.global_errno));
      }
      if (OB_NOT_SUPPORTED == ret) {
        LOG_USER_ERROR(OB_NOT_SUPPORTED, parse_ctx.global_errmsg_);
      } else if (OB_ERR_NON_INT_LITERAL == ret) {
       LOG_USER_ERROR(OB_ERR_NON_INT_LITERAL, static_cast<int32_t>(strlen(parse_ctx.global_errmsg_)), parse_ctx.global_errmsg_);
      }
      parse_ctx.stmt_tree_ = merge_tree(parse_ctx.mem_pool_, &(parse_ctx.global_errno_), T_STMT_LIST, parse_ctx.stmt_tree_);
      multi_stmt = parse_ctx.stmt_tree_;
    }
  } else {
    multi_stmt = parse_ctx.stmt_tree_;
  }
  return ret;
}



}  // namespace pl
}  // namespace oceanbase
