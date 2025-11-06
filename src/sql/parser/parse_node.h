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

#ifndef OCEANBASE_SQL_PARSER_PARSE_NODE_H_
#define OCEANBASE_SQL_PARSER_PARSE_NODE_H_

#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdbool.h>
#include <setjmp.h>
#ifdef SQL_PARSER_COMPILATION
#include "ob_sql_mode.h"
#include "ob_item_type.h"
#else
#include "common/sql_mode/ob_sql_mode.h"
#include "objit/common/ob_item_type.h"
#endif

#ifdef __cplusplus
extern "C" {
#endif

#define MAX_ERROR_MSG 1024

struct ObCharsetInfo;

enum SelectParserOffset
{
  PARSE_SELECT_WITH,
  PARSE_SELECT_DISTINCT,
  PARSE_SELECT_SELECT,
  PARSE_SELECT_INTO, //into before from
  PARSE_SELECT_FROM,
  PARSE_SELECT_WHERE,
  PARSE_SELECT_DYNAMIC_SW_CBY, // connect by node or start with node
  PARSE_SELECT_DYNAMIC_CBY_SW, // connect by node or start with node
  PARSE_SELECT_DYNAMIC_GROUP,
  PARSE_SELECT_DYNAMIC_HAVING,
  PARSE_SELECT_NAMED_WINDOWS,
  PARSE_SELECT_SET,
  PARSE_SELECT_FORMER,
  PARSE_SELECT_LATER,
  PARSE_SELECT_ORDER,
  PARSE_SELECT_APPROX,
  PARSE_SELECT_LIMIT,
  PARSE_SELECT_VECTOR_INDEX_PARAMS,
  PARSE_SELECT_FOR_UPD,
  PARSE_SELECT_HINTS,
  PARSE_SELECT_WHEN, // I find that it is no longer used.
  PARSE_SELECT_FETCH,
  PARSE_SELECT_FETCH_TEMP, //use to temporary store fetch clause in parser
  PARSE_SELECT_WITH_CHECK_OPTION,
  PARSE_SELECT_INTO_EXTRA,// ATTENTION!! SELECT_INTO_EXTRA must be the last one
  PARSE_SELECT_MAX_IDX  // = 25, ATTENTION!! adjust malloc_select_node(node, malloc_pool) after adding a new enum value
};

enum GrantParseOffset
{
  PARSE_GRANT_ROLE_LIST,
  PARSE_GRANT_ROLE_GRANTEE,
  PARSE_GRANT_ROLE_OPT_WITH,
  PARSE_GRANT_ROLE_MAX_IDX
};

enum GrantParseSysOffset
{
  PARSE_GRANT_SYS_PRIV_ORACLE_LIST,
  PARSE_GRANT_SYS_PRIV_ORACLE_GRANTEE,
  PARSE_GRANT_SYS_PRIV_ORACLE_OPT_WITH,
  PARSE_GRANT_SYS_PRIV_ORACLE_MAX_IDX
};

enum ParseMode
{
  STD_MODE = 0,
  FP_MODE, /* fast parse, retain hint, and do parameterization */
  MULTI_MODE ,/* multi query ultra-fast parse */
  FP_PARAMERIZE_AND_FILTER_HINT_MODE,/*Filter out hint, and do parameterization*/
  FP_NO_PARAMERIZE_AND_FILTER_HINT_MODE,/*Filter out hint, and do not parameterize*/
  TRIGGER_MODE, /* treat ':xxx' as identifier */
  DYNAMIC_SQL_MODE, /*Parse dynamic SQL, :idx and :identifier need to be determined whether to check the placeholder name based on the statement type*/
  DBMS_SQL_MODE,
  UDR_SQL_MODE,
  INS_MULTI_VALUES,
};

typedef struct
{
  int err_code_;
  char err_msg_[MAX_ERROR_MSG];
} ErrStat;

struct _ParseNode;

typedef struct _ObStmtLoc
{
  int first_column_;
  int last_column_;
  int first_line_;
  int last_line_;
} ObStmtLoc;

enum UdtUdfType
{
  UDT_UDF_UNKNOWN,
  UDT_UDF_CONS = 1,
  UDT_UDF_MEMBER = 2,
  UDT_UDF_STATIC = 4,
  UDT_UDF_MAP = 8,
  UDT_UDF_ORDER = 16,
};

typedef struct _ParseNode
{
  ObItemType type_;
  int32_t num_child_;   /* attributes for non-terninal node, which has children */
  int16_t param_num_; // record the number of constants in the original text corresponding to this node, temporarily only used by T_CAST_ARGUMENT
  union {
    uint32_t flag_;
    struct {
      uint32_t is_neg_ : 1;// Record whether the parent node of the constant node is a T_OP_NEG node, 1 indicates yes, 0 indicates no
      uint32_t is_hidden_const_ : 1; // 1 indicates that a certain constant can be recognized by normal parse but not by fast parse, 0 indicates that both can recognize it.
      uint32_t is_tree_not_param_ :1; //1 indicates that the node and its sub-nodes cannot be parameterized, 0 indicates no such restriction
      uint32_t length_semantics_  :2; //2 for oracle [char|varbinary] (n b [bytes|char])
      uint32_t is_val_paramed_item_idx_ :1; // Are the values of T_PROJECT_STRING indices in the select_item_param_infos array?
      uint32_t is_copy_raw_text_ : 1; // Whether to refill the raw_text_ of constant nodes, used for parameterization of constant parameters in select items
      uint32_t is_column_varchar_ : 1; // Is the projection column a constant string, used for select item constant parameterization
      uint32_t is_trans_from_minus_: 1; // Whether the negative constant node is transformed from a minus operation, e.g., 1 - 2, the lexical stage will generate a -2
      uint32_t is_assigned_from_child_: 1; // Is the constant node assigned from a child node, used for handling int64_min
      uint32_t is_num_must_be_pos_: 1; //
      uint32_t is_date_unit_ : 1; // 1 indicates it is a date unit constant, which needs to be reversed to a string during reverse parsing
      uint32_t is_literal_bool_ : 1; // indicate node is a literal TRUE/FALSE
      uint32_t is_empty_ : 1; // indicates whether the node is default, 1 means default, 0 means not default, used in opt_asc_desc node
      uint32_t is_multiset_ : 1; // for cast(multiset(...) as ...)
      uint32_t is_forbid_anony_parameter_ : 1; // 1 indicates prohibiting anonymous block parameterization
      uint32_t is_input_quoted_ : 1; // indicate name_ob input whether with double quote
      uint32_t is_forbid_parameter_ : 1; //1 indicate forbid parameter
      uint32_t is_default_literal_expression_ : 1; // 1 indicate in default literal expression, "DEFAULT NOW()"
      uint32_t reserved_;
    };
  };
  /* attributes for terminal node, it is real value */
  /* Numeric type nodes will use value_ to store their value, but for string and decimal types, str_value is used to store the string pointer,
   * str_len_ indicates the length of the string, do not use strlen(str_value_) to get the value of str_value_, because str_value_ does not guarantee to be null-terminated.
   * In addition, why not make value_ and str_len_ a union? This is because when parsing
   * a numeric type, not only do we need to store its value, we also need to store its original string, for example: select
   * 1111; in this statement, we not only have to store the int value, but also the '1111' string*/
  union {
    int64_t value_;
    int32_t int32_values_[2];
    int16_t int16_values_[4];
  };
  const char *str_value_;
  int64_t str_len_;
  union {
    int64_t pl_str_off_; // pl layer, record the start offset of str in the original string
    int64_t sql_str_off_; // sql layer, record the start offset of str in the original string
  };

  /* Used to store text strings lost after special processing in the lexical phase eg: NULL, Date '2010-10-11',
   * this text string, after fast parse parameterization, if the parameter is part of the stmtkey in the plan cache,
   * then the original text string needs to be used instead of the value after losing the text string, otherwise it will lead to plan cache mismatch
   * */
  const char *raw_text_;
  int64_t text_len_;
  int64_t pos_; // record ? position in sql with ?

  struct _ParseNode **children_; /* attributes for non-terminal node, which has children */
  ObStmtLoc stmt_loc_; // temporarily place here, to be moved to parse_stmt_node.h later
  union {
    int64_t raw_param_idx_; // Constant node index in fp_result.raw_params_
    int64_t raw_sql_offset_; // constant node character offset in sql
  };

#ifdef SQL_PARSER_COMPILATION
  int token_off_;
  int token_len_;
#endif
} ParseNode;

struct _ParamList;

typedef struct _ParamList
{
  ParseNode *node_;
  struct _ParamList *next_;
} ParamList;
// External dependency object type for parser use
enum RefType
{
  REF_REL = 0,
  REF_PROC,
  REF_FUNC,
};
// External dependency object linked list
typedef struct _RefObjList
{
  enum RefType type_;
  ParseNode *node_;
  struct _RefObjList *next_;
} RefObjList;
// Parse the attribute set needed when parsing SQL statements in PL
typedef struct _PLParseInfo
{
  bool is_pl_parse_;//Used to indicate whether the current parser logic is a PLParse call
  bool is_pl_parse_expr_; // used to indicate whether the current parser logic is parsing the expr of PLParser
  bool is_forbid_pl_fp_;
  bool is_inner_parse_;
  int last_pl_symbol_pos_; // the end position of the last pl variable
  bool is_parse_dynamic_sql_;
  int plsql_line_;
  /*for mysql pl*/
  void *pl_ns_; //ObPLBlockNS
  RefObjList *ref_object_nodes_; // dependency object list head
  RefObjList *tail_ref_object_node_; // tail of dependency object list
} PLParseInfo;
// Discuss with @Ruidian, the definition here will be changed to dynamic later, define 128 for now
#define MAX_QUESTION_MARK 128

typedef struct _ObQuestionMarkCtx
{
  char **name_;
  int count_;
  int capacity_;
  bool by_ordinal_;
  bool by_name_;
  bool by_defined_name_;
} ObQuestionMarkCtx;


// record the minus status while parsing the sql
// for example, 'select - -1 from dual'
// when parser sees the first '-', pos_ = 7, raw_sql_offset = 7, has_minus_ = true, is_cur_numeric_ = false
// after seeing the second '-', members are reseted, pos_ = 9, raw_sql_offset_ = 9, has_minus_ = true,  is_cur_numeric = false
// after seeing '1', is_cur_numeric = true, then param node '-1' is returned
typedef struct _ObMinusStatuCtx
{
  int pos_; // The position where negative numbers appear in the parameterized SQL
  int raw_sql_offset_; // The position where the minus sign appears in the original sql
  bool has_minus_; // retain the state of the minus sign, when encountering a numeric type, the lexer returns a negative number node
  bool is_cur_numeric_; // Is the current constant node a numeric node
} ObMinusStatusCtx;

#ifdef SQL_PARSER_COMPILATION
// for comment_list_ in ParseResult
typedef struct TokenPosInfo
{
  int token_off_;
  int token_len_;
} TokenPosInfo;
#endif
// External dependency object linked list
typedef struct _ParenthesesOffset
{
  int left_parentheses_;
  int right_parentheses_;
  struct _ParenthesesOffset *next_;
} ParenthesesOffset;

typedef struct _ParseNodeOptParens {
  struct _ParseNode *select_node_;
  bool is_parenthesized_;
} ParseNodeOptParens;

//dml base runtime context definition
typedef struct _InsMultiValuesResult
{
  ParenthesesOffset *ref_parentheses_;
  ParenthesesOffset *tail_parentheses_;
  int values_col_;
  int values_count_;
  int on_duplicate_pos_; // the start position of on duplicate key in insert ... on duplicate key update statement
  int ret_code_;
} InsMultiValuesResult;


typedef struct
{
  void *yyscan_info_;
  const char *input_sql_;
  int input_sql_len_;
  int param_node_num_;
  int token_num_;
  void *malloc_pool_; // ObIAllocator
  ObQuestionMarkCtx question_mark_ctx_;
  ObSQLMode sql_mode_;
  const struct ObCharsetInfo *charset_info_; //client charset
  const struct ObCharsetInfo *charset_info_oracle_db_; //oracle DB charset
  ParamList *param_nodes_;
  ParamList *tail_param_node_;
  struct {
    uint32_t has_encount_comment_              : 1;
    uint32_t is_fp_                            : 1;
    uint32_t is_multi_query_                   : 1;
    uint32_t is_ignore_hint_                   : 1;//used for outline
    uint32_t is_ignore_token_                  : 1;//used for outline
    uint32_t need_parameterize_                : 1;//used for outline, to support signature of outline can contain hint
    uint32_t in_q_quote_                       : 1;
    uint32_t is_for_trigger_                   : 1;
    uint32_t is_dynamic_sql_                   : 1;
    uint32_t is_dbms_sql_                      : 1;
    uint32_t is_batched_multi_enabled_split_   : 1;
    uint32_t is_not_utf8_connection_           : 1;
    uint32_t may_bool_value_                   : 1; // used for true/false in sql parser
    uint32_t is_include_old_new_in_trigger_    : 1;
    uint32_t is_normal_ps_prepare_             : 1;
    uint32_t is_multi_values_parser_           : 1;
    uint32_t is_for_udr_                       : 1;
    uint32_t is_for_remap_                     : 1;
    uint32_t contain_sensitive_data_           : 1;
    uint32_t may_contain_sensitive_data_       : 1;
    uint32_t is_external_table_                : 1;
    uint32_t is_returning_                     : 1;
    uint32_t is_into_cluster_                  : 1;
    uint32_t is_oracle_compat_groupby_         : 1; // true if has rollup/cube/grouping sets in mysql mode
  };

  ParseNode *result_tree_;
  jmp_buf *jmp_buf_;//handle fatal error
  int extra_errno_;
  char *error_msg_;
  int start_col_;
  int end_col_;
  int line_;
  int yycolumn_;
  int yylineno_;
  char *tmp_literal_;
  /* for multi query fast parse (split queries) */
  char *no_param_sql_;
  int no_param_sql_len_;
  int no_param_sql_buf_len_;
  /*for pl*/
  PLParseInfo pl_parse_info_;
  /*for  q-quote*/
  ObMinusStatusCtx minus_ctx_; // for fast parser to parse negative value
  int64_t last_escape_check_pos_;  // A temporary variable when parsing quoted string%parse-param, to handle escape character issues encountered when connecting with the gbk character set
  int connection_collation_;//connection collation
  bool mysql_compatible_comment_; //whether the parser is parsing "/*! xxxx */"
  bool enable_compatible_comment_;
  int semicolon_start_col_;

  InsMultiValuesResult *ins_multi_value_res_;


#ifdef SQL_PARSER_COMPILATION
  TokenPosInfo *comment_list_;
  int comment_cnt_;
  int comment_cap_;
  int realloc_cnt_;
  bool stop_add_comment_;
#endif
} ParseResult;

typedef struct _ObFastParseCtx
{
  bool is_fp_;
} ObFastParseCtx;

typedef enum ObSizeUnitType
{
  SIZE_UNIT_TYPE_INVALID = -1,
  SIZE_UNIT_TYPE_K,
  SIZE_UNIT_TYPE_M,
  SIZE_UNIT_TYPE_G,
  SIZE_UNIT_TYPE_T,
  SIZE_UNIT_TYPE_P,
  SIZE_UNIT_TYPE_E,
  SIZE_UNIT_TYPE_MAX
} ObSizeUnitType;

extern int parse_init(ParseResult *p);
extern int parse_terminate(ParseResult *p);
extern int parse_sql(ParseResult *p, const char *pszSql, size_t iLen);
extern void destroy_tree(ParseNode *pRoot);
extern unsigned char escaped_char(unsigned char c, int *with_back_slash);
extern char *str_tolower(char *buff, int64_t len);
extern char *str_toupper(char *buff, int64_t len);
extern int64_t str_remove_space(char *buff, int64_t len);
//extern int64_t ob_parse_string(const char *src, char *dest, int64_t len, int quote_type);

extern ParseNode *new_node(void *malloc_pool, ObItemType type, int num);
extern ParseNode *new_non_terminal_node(void *malloc_pool, ObItemType node_tag, int num, ...);
extern ParseNode *new_terminal_node(void *malloc_pool, ObItemType type);
extern ParseNode *new_list_node(void *malloc_pool, ObItemType node_tag, int capacity, int num, ...);

extern int obpl_parser_check_stack_overflow();
extern int check_mem_status();
extern int try_check_mem_status(int64_t check_try_times);

int get_deep_copy_size(const ParseNode *node, int64_t *size);
int deep_copy_parse_node_base(void *malloc_pool, const ParseNode *src_node, ParseNode *dst_node);
int deep_copy_parse_node(void *malloc_pool, const ParseNode *src, ParseNode *dst);

/// convert x'42ab' to binary string
void ob_parse_binary(const char *src, int64_t len, char* dest);
int64_t ob_parse_binary_len(int64_t len);

// convert b'10010110' to binary string
// @pre dest buffer is enough
void ob_parse_bit_string(const char* src, int64_t len, char* dest);
int64_t ob_parse_bit_string_len(int64_t len);

// calculate hash value of syntax tree recursively
// @param [in] node         syntax tree root
// @return                  hash value of syntax tree
extern uint64_t parsenode_hash(const ParseNode *node, int *ret);
// compare syntax tree recursively
// @param [in] node1        first syntax tree
// @param [in] node2        second syntax tree
extern bool parsenode_equal(const ParseNode *node1, const ParseNode *node2, int *ret);

extern int64_t get_question_mark(ObQuestionMarkCtx *ctx, void *malloc_pool, const char *name);
extern int64_t get_question_mark_by_defined_name(ObQuestionMarkCtx *ctx, const char *name);
extern int64_t get_need_reserve_capacity(int64_t n);
extern ParseNode *push_back_child(void *malloc_pool, int *error_code, ParseNode *left_node, ParseNode *node);
extern ParseNode *push_front_child(void *malloc_pool, int *error_code, ParseNode *right_node, ParseNode *node);
extern ParseNode *append_child(void *malloc_pool, int *error_code, ParseNode *left_node, ParseNode *right_node);
extern ParseNode *adjust_inner_join_inner(int *error_code, ParseNode *inner_join, ParseNode *table_node);
extern ParseNodeOptParens *new_parse_node_opt_parens(void *malloc_pool);

// compare ParseNode str_value_ to pattern
// @param [in] node        ParseNode
// @param [in] pattern     pattern_str
// @param [in] pat_len     length of pattern
extern bool nodename_equal(const ParseNode *node, const char *pattern, int64_t pat_len);

#define OB_NODE_CAST_TYPE_IDX 0
#define OB_NODE_CAST_COLL_IDX 1
#define OB_NODE_CAST_N_PREC_IDX 2
#define OB_NODE_CAST_N_SCALE_IDX 3
#define OB_NODE_CAST_NUMBER_TYPE_IDX 1
#define OB_NODE_CAST_C_LEN_IDX 1
#define OB_NODE_CAST_GEO_TYPE_IDX 1
#define OB_NODE_CAST_CS_LEVEL_IDX 2

typedef enum ObNumberParseType
{
  NPT_PERC_SCALE = 0,
  NPT_STAR_SCALE,
  NPT_STAR,
  NPT_PERC,
  NPT_EMPTY,
} ObNumberParseType;

#ifndef SQL_PARSER_COMPILATION
bool check_stack_overflow_c();
// Find the interface for external pl variables, get the index of the variable in the external symbol table, defined in ob_pl_stmt.cpp
int lookup_pl_symbol(const void *pl_ns, const char *symbol, size_t len, int64_t *find_idx);
#endif

typedef struct _ParserLinkNode
{
  struct _ParserLinkNode *next_;
  struct _ParserLinkNode *prev_;
  void *val_;
} ParserLinkNode;

ParserLinkNode *new_link_node(void *malloc);

typedef enum ObTranslateCharset
{
  TRANSLATE_CHAR_CS = 0,
  TRANSLATE_NCHAR_CS = 1,
} ObTranslateCharset;

#ifdef __cplusplus
}
#endif

#endif //OCEANBASE_SQL_PARSER_PARSE_NODE_H_
