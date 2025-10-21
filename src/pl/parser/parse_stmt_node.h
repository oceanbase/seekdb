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

#ifndef OCEANBASE_SRC_PL_PARSER_PARSE_STMT_NODE_H_
#define OCEANBASE_SRC_PL_PARSER_PARSE_STMT_NODE_H_
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdbool.h>
#include <setjmp.h>
#include "sql/parser/parse_node.h"
#ifdef __cplusplus
extern "C" {
#endif

typedef struct _ObParseErrorInfo
{
  int error_code_;
  ObStmtLoc stmt_loc_;
} ObParseErrorInfo;

//typedef struct _ObStmtNodeTree
//{
//  ObParseErrorInfo *error_info_;
//  ParseNode *result_tree_;
//} ObStmtNodeTree;
typedef ParseNode ObStmtNodeTree;

typedef struct _ObScannerCtx
{
  void *yyscan_info_;
  char *tmp_literal_;
  int tmp_literal_len_;
  ObSQLMode sql_mode_;
  int token_len_;
  int first_column_;
  int sql_start_loc;
  int sql_end_loc;
} ObScannerCtx;

typedef struct _ObParseCtx
{
  void *mem_pool_; // ObIAllocator
  ObStmtNodeTree *stmt_tree_;
  ObScannerCtx scanner_ctx_;
  jmp_buf jmp_buf_; //handle fatal error
  int global_errno_;
  char global_errmsg_[MAX_ERROR_MSG];
  ObParseErrorInfo *cur_error_info_;
  const char *stmt_str_;
  int stmt_len_;
  const char *orig_stmt_str_;
  int orig_stmt_len_;
  ObQuestionMarkCtx question_mark_ctx_;//used to record all the question marks in the entire anonymous
  int comp_mode_;
  bool is_not_utf8_connection_;
  const struct ObCharsetInfo *charset_info_;
  const struct ObCharsetInfo *charset_info_oracle_db_;
  int64_t last_escape_check_pos_;  // A temporary variable used during quoted string parsing to handle escape characters encountered when connecting with the gbk character set
  int connection_collation_;
  bool mysql_compatible_comment_; //whether the parser is parsing "/*! xxxx */"
  int copied_pos_;
  char *no_param_sql_;
  int no_param_sql_len_;
  int no_param_sql_buf_len_;
  int param_node_num_;
  ParamList *param_nodes_;
  ParamList *tail_param_node_;
  struct
  {
    uint32_t is_inner_parse_:1;   //is inner parser, not from the user's call
    uint32_t is_for_trigger_:1;
    uint32_t is_dynamic_:1; //whether it comes from dynamic sql
    uint32_t is_for_preprocess_:1;
    uint32_t is_include_old_new_in_trigger_:1; // indicates whether include :old/:new/:parent in trigger body
    uint32_t in_q_quote_:1;
    uint32_t is_pl_fp_  :1;
    uint32_t is_forbid_anony_parameter_ : 1; // 1 indicates that anonymous block parameterization is forbidden
    uint32_t need_switch_to_wrap_ : 1; // 1 indicates that the parser needs to switch to <wrap_begin>
    uint32_t contain_sensitive_data_ : 1; // 1 indicates whether contains sensitive data like create_ai_endpoint
    uint32_t reserved_:22;
  };
} ObParseCtx;

#ifdef __cplusplus
}
#endif
#endif /* OCEANBASE_SRC_PL_PARSER_PARSE_STMT_NODE_H_ */
