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

#ifndef OCEANBASE_SQL_SELECTSTMT_H_
#define OCEANBASE_SQL_SELECTSTMT_H_

#include "sql/resolver/expr/ob_raw_expr.h"
#include "lib/string/ob_string.h"
#include "lib/container/ob_array.h"
#include "lib/container/ob_se_array.h"
#include "lib/container/ob_bit_set.h"
#include "lib/container/ob_vector.h"
#include "sql/resolver/dml/ob_dml_stmt.h"
#include "sql/ob_sql_temp_table.h"

namespace oceanbase
{
namespace sql
{
enum SelectTypeAffectFoundRows
{
  AFFECT_FOUND_ROWS,
  NOT_AFFECT_FOUND_ROWS
};

struct SelectItem
{
  SelectItem()
    : expr_(NULL),
      is_real_alias_(false),
      alias_name_(),
      paramed_alias_name_(),
      expr_name_(),
      questions_pos_(),
      params_idx_(),
      neg_param_idx_(),
      esc_str_flag_(false),
      need_check_dup_name_(false),
      implicit_filled_(false),
      is_implicit_added_(false),
      is_hidden_rowid_(false)
  {
  }
  void reset()
  {
    expr_ = NULL;
    is_real_alias_ = false;
    alias_name_.reset();
    paramed_alias_name_.reset();
    expr_name_.reset();
    questions_pos_.reset();
    params_idx_.reset();
    neg_param_idx_.reset();
    esc_str_flag_ = false;
    need_check_dup_name_ = false;
    implicit_filled_ = false;
    is_implicit_added_ = false;
    is_hidden_rowid_ = false;
  }

  void reset_param_const_infos()
  {
    questions_pos_.reset();
    params_idx_.reset();
    neg_param_idx_.reset();
    esc_str_flag_ = false;
    need_check_dup_name_ = false;
  }
  int deep_copy(ObIRawExprCopier &copier,
                const SelectItem &other);
  TO_STRING_KV(N_EXPR, expr_,
               N_IS_ALIAS, is_real_alias_,
               N_ALIAS_NAME, alias_name_,
               N_EXPR_NAME, expr_name_,
               K_(paramed_alias_name),
               K_(questions_pos),
               K_(params_idx),
               K_(esc_str_flag),
               K_(need_check_dup_name),
               K_(implicit_filled),
               K_(is_hidden_rowid));

  ObRawExpr *expr_;
  bool is_real_alias_;
  common::ObString alias_name_;
  common::ObString paramed_alias_name_;
  common::ObString expr_name_;

  common::ObSEArray<int64_t, OB_DEFAULT_SE_ARRAY_COUNT> questions_pos_;
  common::ObSEArray<int64_t, OB_DEFAULT_SE_ARRAY_COUNT> params_idx_;
  common::ObBitSet<> neg_param_idx_;
  // Projection column is a constant string, it needs to mark the escape flag, used for select item constant parameterization
  bool esc_str_flag_;
  // Mark whether to check raw_param, used for select item constant parameterization
  bool need_check_dup_name_;
  // select item is implicit filled in updatable view, to pass base table's column to top view.
  bool implicit_filled_;
  bool is_implicit_added_; //used for temporary table and label security at insert resolver

  bool is_hidden_rowid_;
};

struct ObSelectIntoItem
{
  ObSelectIntoItem()
      : into_type_(),
        outfile_name_(),
        field_str_(),
        line_str_(),
        user_vars_(),
        pl_vars_(),
        closed_cht_(),
        is_optional_(DEFAULT_OPTIONAL_ENCLOSED),
        is_single_(DEFAULT_SINGLE_OPT),
        max_file_size_(DEFAULT_MAX_FILE_SIZE),
        escaped_cht_(),
        file_partition_expr_(NULL),
        buffer_size_(DEFAULT_BUFFER_SIZE),
        external_properties_()
  {
    field_str_.set_varchar(DEFAULT_FIELD_TERM_STR);
    field_str_.set_collation_type(ObCharset::get_system_collation());
    line_str_.set_varchar(DEFAULT_LINE_TERM_STR);
    line_str_.set_collation_type(ObCharset::get_system_collation());
    escaped_cht_.meta_.set_char();
    escaped_cht_.set_char_value(&DEFAULT_FIELD_ESCAPED_CHAR, 1);
    escaped_cht_.set_collation_type(ObCharset::get_system_collation());
    closed_cht_.meta_.set_char();
    closed_cht_.set_char_value(NULL, 0);
    closed_cht_.set_collation_type(ObCharset::get_system_collation());
    cs_type_ = ObCharset::get_system_collation();
  }
  int assign(const ObSelectIntoItem &other) {
    into_type_ = other.into_type_;
    outfile_name_ = other.outfile_name_;
    field_str_ = other.field_str_;
    line_str_ = other.line_str_;
    closed_cht_ = other.closed_cht_;
    is_optional_ = other.is_optional_;
    is_single_ = other.is_single_;
    max_file_size_ = other.max_file_size_;
    escaped_cht_ = other.escaped_cht_;
    cs_type_ = other.cs_type_;
    file_partition_expr_ = other.file_partition_expr_;
    buffer_size_ = other.buffer_size_;
    external_properties_ = other.external_properties_;
    return user_vars_.assign(other.user_vars_);
  }
  int deep_copy(ObIAllocator &allocator,
                ObIRawExprCopier &copier,
                const ObSelectIntoItem &other);
  TO_STRING_KV(K_(into_type),
               K_(outfile_name),
               K_(field_str),
               K_(line_str),
               K_(closed_cht),
               K_(is_optional),
               K_(is_single),
               K_(max_file_size),
               K_(escaped_cht),
               K_(cs_type),
               N_EXPR, file_partition_expr_,
               K_(buffer_size),
               K_(external_properties));
  ObItemType into_type_;
  common::ObObj outfile_name_;
  common::ObObj field_str_; // field terminated str
  common::ObObj line_str_; // line terminated str
  common::ObSEArray<common::ObString, 16> user_vars_; // user variables
  common::ObSEArray<sql::ObRawExpr*, 16> pl_vars_; // pl variables
  common::ObObj closed_cht_; // all fields, "123","ab"
  bool is_optional_; //  for string, closed character, such as "aa"
  bool is_single_;
  int64_t max_file_size_;
  common::ObObj escaped_cht_;
  common::ObCollationType cs_type_;
  sql::ObRawExpr* file_partition_expr_;
  int64_t buffer_size_;
  common::ObString external_properties_;

  static const char* const DEFAULT_FIELD_TERM_STR;
  static const char* const DEFAULT_LINE_TERM_STR;
  static const char DEFAULT_FIELD_ENCLOSED_CHAR;
  static const bool DEFAULT_OPTIONAL_ENCLOSED;
  static const bool DEFAULT_SINGLE_OPT;
  static const int64_t DEFAULT_MAX_FILE_SIZE;
  static const int64_t DEFAULT_BUFFER_SIZE;
  static const char DEFAULT_FIELD_ESCAPED_CHAR;
};

struct ObGroupbyExpr
{
  ObGroupbyExpr()
    : groupby_exprs_()
  {
  }
  int assign(const ObGroupbyExpr& other) {
    return groupby_exprs_.assign(other.groupby_exprs_);
  }

  TO_STRING_KV("grouping sets groupby expr", groupby_exprs_);
  common::ObSEArray<sql::ObRawExpr*, 8, common::ModulePageAllocator, true> groupby_exprs_;
};

struct ForUpdateDMLInfo
{
  ForUpdateDMLInfo()
  : table_id_(OB_INVALID_ID),
    base_table_id_(OB_INVALID_ID),
    ref_table_id_(OB_INVALID_ID),
    rowkey_cnt_(0),
    is_nullable_(false),
    for_update_wait_us_(-1),
    skip_locked_(false)
  {}
  TO_STRING_KV(K_(table_id),
               K_(base_table_id),
               K_(ref_table_id),
               K_(rowkey_cnt),
               K_(unique_column_ids),
               K_(is_nullable),
               K_(for_update_wait_us),
               K_(skip_locked));
  uint64_t table_id_;       // view table id
  uint64_t base_table_id_;  // for update base table id
  uint64_t ref_table_id_;   // base table ref id
  int64_t rowkey_cnt_;
  common::ObSEArray<uint64_t, 2, common::ModulePageAllocator, true> unique_column_ids_;
  bool is_nullable_;
  int64_t for_update_wait_us_;
  bool skip_locked_;
};

}

namespace common
{
template <>
struct ob_vector_traits<oceanbase::sql::SelectItem>
{
  typedef oceanbase::sql::SelectItem *pointee_type;
  typedef oceanbase::sql::SelectItem value_type;
  typedef const oceanbase::sql::SelectItem const_value_type;
  typedef value_type *iterator;
  typedef const value_type *const_iterator;
  typedef int32_t difference_type;
};

template <>
struct ob_vector_traits<oceanbase::sql::FromItem>
{
  typedef oceanbase::sql::FromItem *pointee_type;
  typedef oceanbase::sql::FromItem value_type;
  typedef const oceanbase::sql::FromItem const_value_type;
  typedef value_type *iterator;
  typedef const value_type *const_iterator;
  typedef int32_t difference_type;
};
}

namespace sql
{
class ObSelectStmt : public ObDMLStmt
{
public:
  enum SetOperator
  {
    NONE = 0,
    UNION,
    INTERSECT,
    EXCEPT,
    RECURSIVE,
    SET_OP_NUM,
  };

  static const char *set_operator_str(SetOperator op) {
    static const char *set_operator_name[SET_OP_NUM + 1] =
    {
      "none",
      "union",
      "intersect",
      "except",
      "recursive",
      "unknown",
    };
    static const char *set_operator_name_oracle[SET_OP_NUM + 1] =
    {
      "none",
      "union",
      "intersect",
      "minus",
      "unknown",
      "unknown",
    };
    return set_operator_name[op];
  }

  class ObShowStmtCtx
  {
  public:
    ObShowStmtCtx()
        : is_from_show_stmt_(false),
          global_scope_(false),
          tenant_id_(common::OB_INVALID_ID),
          show_database_id_(common::OB_INVALID_ID),
          show_table_id_(common::OB_INVALID_ID),
          grants_user_id_(common::OB_INVALID_ID),
          show_seed_(false)
    {}
    virtual ~ObShowStmtCtx() {}

    void assign(const ObShowStmtCtx &other) {
      is_from_show_stmt_ = other.is_from_show_stmt_;
      global_scope_ = other.global_scope_;
      tenant_id_ = other.tenant_id_;
      show_database_id_ = other.show_database_id_;
      show_table_id_ = other.show_table_id_;
      grants_user_id_ = other.grants_user_id_;
      show_seed_ = other.show_seed_;
    }

    bool      is_from_show_stmt_; // whether it is converted from a show statement
    bool      global_scope_;
    uint64_t  tenant_id_;
    uint64_t  show_database_id_;
    uint64_t  show_table_id_; // ex: show columns from t1, and show_table_id_ is the table id of t1
    uint64_t grants_user_id_; // for show grants
    bool show_seed_; // for show seed parameter

    TO_STRING_KV(K_(is_from_show_stmt),
                 K_(global_scope),
                 K_(tenant_id),
                 K_(show_database_id),
                 K_(show_table_id),
                 K_(grants_user_id),
                 K_(show_seed));
  };

  ObSelectStmt();
  virtual ~ObSelectStmt();
  int assign(const ObSelectStmt &other);
  
  virtual int iterate_stmt_expr(ObStmtExprVisitor &vistor) override;
  
  int update_stmt_table_id(ObIAllocator *allocator, const ObSelectStmt &other);
  int64_t get_select_item_size() const { return select_items_.count(); }
  int64_t get_group_expr_size() const { return group_exprs_.count(); }
  int64_t get_rollup_expr_size() const { return rollup_exprs_.count(); }
  int64_t get_rollup_dir_size() const { return rollup_directions_.count(); }
  int64_t get_aggr_item_size() const { return agg_items_.count(); }
  int64_t get_having_expr_size() const { return having_exprs_.count(); }
  void set_recursive_union(bool is_recursive_union) { is_recursive_cte_ = is_recursive_union; }
  void assign_distinct() { is_distinct_ = true; }
  void assign_all() { is_distinct_ = false; }
  void assign_set_op(SetOperator op) { set_op_ = op; }
  void assign_set_distinct() { is_set_distinct_ = true; }
  void assign_set_all() { is_set_distinct_ = false; }
  void set_is_from_show_stmt(bool is_from_show_stmt) { show_stmt_ctx_.is_from_show_stmt_ = is_from_show_stmt; }
  void set_global_scope(bool global_scope) { show_stmt_ctx_.global_scope_ = global_scope; }
  void set_tenant_id(uint64_t tenant_id) { show_stmt_ctx_.tenant_id_ = tenant_id; }
  void set_show_seed(bool show_seed) { show_stmt_ctx_.show_seed_ = show_seed; }
  void set_show_database_id(uint64_t show_database_id) { show_stmt_ctx_.show_database_id_ = show_database_id; }
  void set_show_table_id(uint64_t show_table_id) { show_stmt_ctx_.show_table_id_ = show_table_id; }
  void set_show_grants_user_id(uint64_t user_id) { show_stmt_ctx_.grants_user_id_ = user_id; }
  void set_select_into(ObSelectIntoItem *into_item) { into_item_ = into_item; }
  uint64_t get_show_grants_user_id() { return show_stmt_ctx_.grants_user_id_; }
  int check_alias_name(ObStmtResolver &ctx, const common::ObString &alias) const;
  int check_using_column(ObStmtResolver &ctx, const common::ObString &column_name) const;
  bool get_global_scope() const { return show_stmt_ctx_.global_scope_; }
  uint64_t get_tenant_id() const { return show_stmt_ctx_.tenant_id_; }
  bool get_show_seed() const { return show_stmt_ctx_.show_seed_; }
  uint64_t get_show_database_id() const { return show_stmt_ctx_.show_database_id_; }
  uint64_t get_show_table_id() const { return show_stmt_ctx_.show_table_id_; }
  bool is_distinct() const { return is_distinct_; }
  bool is_recursive_union() const { return is_recursive_cte_;}
  bool is_set_distinct() const { return is_set_distinct_; }
  bool is_from_show_stmt() const { return show_stmt_ctx_.is_from_show_stmt_; }
  // view
  void set_is_view_stmt(bool is_view_stmt, uint64_t view_ref_id)
  { is_view_stmt_ = is_view_stmt; view_ref_id_ = view_ref_id; }
  bool is_view_stmt() const { return is_view_stmt_; }
  uint64_t get_view_ref_id() const { return view_ref_id_; }
  bool has_select_into() const { return into_item_ != NULL; }
  bool is_select_into_outfile() const { return has_select_into() &&
                                               into_item_->into_type_ == T_INTO_OUTFILE; }
  // check if the stmt is a Select-Project-Join(SPJ) query
  bool is_spj() const;
  // is_spj + normal group by or scalar group by
  bool is_spjg() const;

  ObRawExpr *get_expr(uint64_t expr_id);
  inline bool is_single_table_stmt() const { return (1 == get_table_size()
                                                   && 1 == get_from_item_size()); }
  inline bool has_group_by() const { return get_group_expr_size() > 0 ||
                                            get_rollup_expr_size() > 0 ||
                                            get_aggr_item_size() > 0; }
  inline bool is_scala_group_by() const { return get_group_expr_size() == 0 &&
                                                 get_rollup_expr_size() == 0 &&
                                                 get_aggr_item_size() > 0; }

  inline bool has_recursive_cte() const { return is_recursive_cte_; }
  inline void set_expanded_mview(bool is_expanded_mview) { is_expanded_mview_ = is_expanded_mview; }
  inline bool is_expanded_mview() const { return is_expanded_mview_; }
  void set_has_reverse_link(bool has_reverse_link) { has_reverse_link_ = has_reverse_link; }
  bool has_reverse_link() const { return has_reverse_link_; }
  // return single row
  inline bool is_single_set_query() const { return get_aggr_item_size() > 0 &&
                                                   group_exprs_.empty() &&
                                                   rollup_exprs_.empty(); }
  inline bool has_rollup() const { return get_rollup_expr_size() > 0; };
  inline bool has_order_by() const { return (get_order_item_size() > 0); }
  inline bool has_distinct() const { return is_distinct(); }
  inline bool has_having() const { return (get_having_expr_size() > 0); }
  inline bool has_rollup_dir() const { return (get_rollup_dir_size() > 0); }
  SetOperator get_set_op() const { return set_op_; }
  bool has_hidden_rowid() const;
  virtual int clear_sharable_expr_reference() override;
  virtual int remove_useless_sharable_expr(ObRawExprFactory *expr_factory,
                                           ObSQLSessionInfo *session_info,
                                           bool explicit_for_col) override;

  const SelectItem &get_select_item(int64_t index) const { return select_items_[index]; }
  SelectItem &get_select_item(int64_t index) { return select_items_[index]; }
  common::ObIArray<SelectItem> &get_select_items() { return select_items_; }
  const common::ObIArray<SelectItem> &get_select_items() const { return select_items_; }
  int get_select_exprs(ObIArray<ObRawExpr*> &select_exprs);
  int get_select_exprs(ObIArray<ObRawExpr*> &select_exprs) const;
  int get_select_exprs_without_lob(ObIArray<ObRawExpr*> &select_exprs) const;
  const common::ObIArray<ObAggFunRawExpr*> &get_aggr_items() const { return agg_items_; }
  common::ObIArray<ObAggFunRawExpr*> &get_aggr_items() { return agg_items_; }
  ObAggFunRawExpr *get_aggr_item(int64_t index) const { return agg_items_[index]; }
  ObAggFunRawExpr *get_aggr_item(int64_t index) { return agg_items_[index]; }
  common::ObIArray<ObRawExpr*> &get_having_exprs() { return having_exprs_; }
  const common::ObIArray<ObRawExpr*> &get_having_exprs() const { return having_exprs_; }
  int recursive_get_expr(ObRawExpr *expr,
                         common::ObIArray<ObRawExpr *> &exprs,
                         ObExprInfoFlag target_flag,
                         ObExprInfoFlag search_flag) const;
  const common::ObIArray<ObRawExpr*> &get_group_exprs() const { return group_exprs_; }
  common::ObIArray<ObRawExpr *> &get_group_exprs() { return group_exprs_; }
  const common::ObIArray<ObRawExpr*> &get_rollup_exprs() const { return rollup_exprs_; }
  common::ObIArray<ObRawExpr *> &get_rollup_exprs() { return rollup_exprs_; }
  const common::ObIArray<ObOrderDirection> &get_rollup_dirs() const { return rollup_directions_; }
  common::ObIArray<ObOrderDirection> &get_rollup_dirs() { return rollup_directions_; }
  ObSelectIntoItem* get_select_into() const { return into_item_; }
  int add_group_expr(ObRawExpr* expr) { return group_exprs_.push_back(expr); }
  int add_rollup_expr(ObRawExpr* expr) { return rollup_exprs_.push_back(expr); }
  int add_rollup_dir(ObOrderDirection dir) { return rollup_directions_.push_back(dir); }
  int add_agg_item(ObAggFunRawExpr &agg_expr)
  {
    agg_expr.set_explicited_reference();
    return agg_items_.push_back(&agg_expr);
  }
  int add_having_expr(ObRawExpr *expr) { return having_exprs_.push_back(expr); }
  bool has_for_update() const;
  bool is_skip_locked() const;
  common::ObIArray<ObColumnRefRawExpr*> &get_for_update_columns() { return for_update_columns_; }
  const common::ObIArray<ObColumnRefRawExpr *> &get_for_update_columns() const { return for_update_columns_; }
  bool contain_ab_param() const { return contain_ab_param_; }
  void set_ab_param_flag(bool contain_ab_param) { contain_ab_param_ = contain_ab_param; }
  virtual bool is_affect_found_rows() const
  {
    bool ret = false;
    if (select_type_ == AFFECT_FOUND_ROWS) {
      ret = true;
    } else {
      ret = false;
    }
    return ret;
  }
  virtual bool has_link_table() const
  {
    bool bret = ObDMLStmt::has_link_table();
    for (int64_t i = 0; !bret && i < set_query_.count(); i++) {
      if (OB_NOT_NULL(set_query_.at(i))) {
        bret = set_query_.at(i)->has_link_table();
      }
    }
    return bret;
  }
  void set_select_type(SelectTypeAffectFoundRows type) { select_type_ = type; }
  int check_having_ident(ObStmtResolver &ctx,
                         common::ObString &column_name,
                         TableItem *table_item,
                         ObColumnRefRawExpr &ret_expr) const;
  int add_select_item(SelectItem &item);
  void clear_select_item() { select_items_.reset(); }
  void clear_aggr_item() { agg_items_.reset(); }
  int check_aggr_and_winfunc(ObRawExpr &expr);
  //@hualong unused code
  //static  const char *get_set_op_type_str(SetOperator set_op);
  DECLARE_VIRTUAL_TO_STRING;
  int do_to_string(char *buf, const int64_t buf_len, int64_t &pos) const;
  /**
   * compare with another select stmt
   * all members must be equal
   * @param[in] stmt                another select stmt
   * @return                        true equal, false not equal
   */
  bool equals(const ObSelectStmt &stmt);
  int check_and_get_same_aggr_item(ObRawExpr *expr, ObAggFunRawExpr *&same_aggr);
  int get_same_win_func_item(const ObRawExpr *expr, ObWinFunRawExpr *&win_expr);
  void set_match_topk(bool is_match) { is_match_topk_ = is_match; }
  bool is_match_topk() const { return is_match_topk_; }
  bool is_set_stmt() const { return NONE != set_op_; }
  int get_child_stmt_size(int64_t &child_size) const;
  int get_child_stmts(common::ObIArray<ObSelectStmt*> &child_stmts) const;
  int set_child_stmt(const int64_t child_num, ObSelectStmt* child_stmt);
  virtual int get_from_subquery_stmts(common::ObIArray<ObSelectStmt*> &child_stmts,
                                      bool contain_lateral_table = true) const override;
  const common::ObIArray<ObWinFunRawExpr *> &get_window_func_exprs() const { return win_func_exprs_; };
  common::ObIArray<ObWinFunRawExpr *> &get_window_func_exprs() { return win_func_exprs_; };
  bool has_window_function() const { return win_func_exprs_.count() != 0; }
  int add_window_func_expr(ObWinFunRawExpr *expr);
  const ObWinFunRawExpr* get_window_func_expr(int64_t i) const { return win_func_exprs_.at(i); }
  ObWinFunRawExpr* get_window_func_expr(int64_t i) { return win_func_exprs_.at(i); }
  int64_t get_window_func_count() const { return win_func_exprs_.count(); }
  int remove_window_func_expr(ObWinFunRawExpr *expr);
  const common::ObIArray<ObRawExpr *> &get_qualify_filters() const { return qualify_filters_; };
  common::ObIArray<ObRawExpr *> &get_qualify_filters() { return qualify_filters_; };
  int64_t get_qualify_filters_count() const { return qualify_filters_.count(); };
  bool has_window_function_filter() const { return qualify_filters_.count() != 0; }
  int set_qualify_filters(common::ObIArray<ObRawExpr *> &exprs);
  void set_children_swapped() { children_swapped_ = true; }
  bool get_children_swapped() const { return children_swapped_; }

  virtual int check_table_be_modified(uint64_t ref_table_id, bool& is_modified) const override;

  // check aggregation has distinct or group concat e.g.:
  //  count(distinct c1)
  //  group_concat(c1 order by c2))
  bool has_distinct_or_concat_agg() const;
  virtual int get_equal_set_conditions(ObIArray<ObRawExpr *> &conditions,
                                       const bool is_strict,
                                       const bool check_having = false) const override;
  int create_select_list_for_set_stmt(ObRawExprFactory &expr_factory);

  int deep_copy_stmt_struct(ObIAllocator &allocator,
                            ObRawExprCopier &expr_copier,
                            const ObDMLStmt &other) override;
  bool check_is_select_item_expr(const ObRawExpr *expr) const;
  bool contain_nested_aggr() const;

  int get_set_stmt_size(int64_t &size) const;
  common::ObIArray<ObSelectStmt*> &get_set_query() { return set_query_; }
  const common::ObIArray<ObSelectStmt*> &get_set_query() const { return set_query_; }
  int add_set_query(ObSelectStmt* stmt)  { return set_query_.push_back(stmt); }
  int set_set_query(const int64_t index, ObSelectStmt *stmt);
  inline ObSelectStmt* get_set_query(const int64_t index) const
  { return OB_LIKELY(index >= 0 && index < set_query_.count()) ? set_query_.at(index) : NULL; }
  inline ObSelectStmt* get_set_query(const int64_t index)
  { return const_cast<ObSelectStmt *>(static_cast<const ObSelectStmt&>(*this).get_set_query(index)); }
  const ObSelectStmt* get_real_stmt() const;
  ObSelectStmt* get_real_stmt()
  { return const_cast<ObSelectStmt *>(static_cast<const ObSelectStmt&>(*this).get_real_stmt()); }
  share::schema::ViewCheckOption get_check_option() const { return check_option_; }
  void set_check_option(share::schema::ViewCheckOption check_option) { check_option_ = check_option; }
  // this function will only be called while resolving with clause.
  bool has_external_table() const;
  int get_pure_set_exprs(ObIArray<ObRawExpr*> &pure_set_exprs) const;
  static ObRawExpr* get_pure_set_expr(ObRawExpr *expr);
  void set_select_straight_join(bool flag) { is_select_straight_join_ = flag; }
  bool is_select_straight_join() const { return is_select_straight_join_; }
  virtual int check_is_simple_lock_stmt(bool &is_valid) const override;
  inline bool is_implicit_distinct() const { return is_implicit_distinct_; }
  virtual int formalize_implicit_distinct() override;
  virtual int check_from_dup_insensitive(bool &is_from_dup_insens) const override;
  int is_duplicate_insensitive_aggregation(bool &is_dup_insens_aggr) const;
  bool is_implicit_distinct_allowed() const;
  inline void set_implicit_distinct(bool v) { is_implicit_distinct_ = v; }
  inline void reset_implicit_distinct() { is_implicit_distinct_ = false; }
  int is_query_deterministic(bool &is_deterministic) const;
  inline bool is_oracle_compat_groupby() const {return is_oracle_compat_groupby_; }
  inline void set_is_oracle_compat_groupby(bool v) { is_oracle_compat_groupby_ = v; }

private:
  SetOperator set_op_;
  /* these var is only used for recursive union */
  bool is_recursive_cte_;
  /* These fields are only used by normal select */
  bool is_distinct_;
  // Used for the sort column specified in the search by clause of the cte recursive statement
  common::ObSEArray<SelectItem, 16, common::ModulePageAllocator, true> select_items_;
  common::ObSEArray<ObRawExpr*, 8, common::ModulePageAllocator, true> group_exprs_;
  common::ObSEArray<ObRawExpr*, 8, common::ModulePageAllocator, true> rollup_exprs_;
  common::ObSEArray<ObRawExpr*, 8, common::ModulePageAllocator, true> having_exprs_;
  common::ObSEArray<ObAggFunRawExpr*, 8, common::ModulePageAllocator, true> agg_items_;
  common::ObSEArray<ObWinFunRawExpr*, 8, common::ModulePageAllocator, true> win_func_exprs_;
  //a child set of the filters in the parent stmts, only used for partition topn sort
  common::ObSEArray<ObRawExpr*, 8, common::ModulePageAllocator, true> qualify_filters_;
  // This structure is only meaningful in the rollup statement under mysql mode. For example, the following statement
  //select a,b,sum(d) from t group by a desc, b asc with rollup.
  common::ObSEArray<ObOrderDirection, 8, common::ModulePageAllocator, true> rollup_directions_;

  // for oracle mode only, for stmt print only
  common::ObSEArray<ObColumnRefRawExpr*, 4, common::ModulePageAllocator, true> for_update_columns_;

  /* These fields are only used by set select */
  bool is_set_distinct_;
  /* for set stmt child stmt*/
  common::ObSEArray<ObSelectStmt*, 2, common::ModulePageAllocator, true> set_query_;

  /* for show statment*/
  ObShowStmtCtx show_stmt_ctx_;
  // view
  bool is_view_stmt_; //for view privilege check
  uint64_t view_ref_id_;
  SelectTypeAffectFoundRows select_type_;
  ObSelectIntoItem *into_item_; // select .. into outfile/dumpfile/var_name
  bool is_match_topk_;
  // A set operator B -> B set operator A，children_swapped_ will be
  // set to true.
  bool children_swapped_;
  share::schema::ViewCheckOption check_option_;
  bool contain_ab_param_;
  bool has_reverse_link_;
  bool is_expanded_mview_;
  //denote if the query option 'STRAIGHT_JOIN' has been specified
  bool is_select_straight_join_;
  // denote if the duplicate value of this stmt will not change the query result
  // optimizer can assign or remove DISTINCT for this stmt 
  bool is_implicit_distinct_;
  bool is_oracle_compat_groupby_; // true if has rollup/cube/grouping sets in mysql mode
};
}
}
#endif //OCEANBASE_SQL_SELECTSTMT_H_
