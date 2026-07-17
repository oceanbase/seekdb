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
 
#ifndef OB_SHARED_EXPR_RESOLVER_H
#define OB_SHARED_EXPR_RESOLVER_H

#include "sql/resolver/expr/ob_raw_expr.h"

namespace oceanbase {
namespace sql {

struct JoinedTable;
struct TableItem;
struct ObQuestionmarkEqualCtx : public ObExprEqualCheckContext
{
  ObQuestionmarkEqualCtx(bool need_check_deterministic = false):
    ObExprEqualCheckContext(need_check_deterministic)
  {
    override_const_compare_ = true;
  }
  
  bool compare_const(const ObConstRawExpr &left, 
                     const ObConstRawExpr &right);
  
  ObSEArray<ObPCParamEqualInfo, 4> equal_pairs_;

private:
  DISABLE_COPY_ASSIGN(ObQuestionmarkEqualCtx);
};

struct ObRawExprEntry
{
  ObRawExprEntry()  :expr_(NULL), stmt_scope_(0), hash_code_(0)
  { }

  ObRawExprEntry(ObRawExpr *expr, uint64_t scope_id, uint64_t hash_code)
    : expr_(expr), stmt_scope_(scope_id), hash_code_(hash_code)
  {}

  bool compare(const ObRawExprEntry &node,
               ObQuestionmarkEqualCtx &cmp_ctx) const;
  
  TO_STRING_KV(K_(hash_code), K_(expr));
  
  ObRawExpr *expr_;
  uint64_t stmt_scope_;
  uint64_t hash_code_;
};

class ObSharedExprResolver
{
public:
  ObSharedExprResolver(ObQueryCtx *query_ctx)
    : allocator_("MergeSharedExpr"),
      scope_id_(0),
      query_ctx_(query_ctx),
      disable_share_const_level_(0)
  {}
  
  virtual ~ObSharedExprResolver();

  int get_shared_instance(ObRawExpr *expr,
                          ObRawExpr *&shared_expr,
                          bool &is_new,
                          bool &disable_share_expr);

  int add_new_instance(ObRawExprEntry &entry);
  
  uint64_t get_scope_id() const { return scope_id_; }
  
  void set_new_scope() { scope_id_ ++; }
  
  void revert_scope() { scope_id_ = 0; }
  
  uint64_t hash_expr_tree(ObRawExpr *expr, uint64_t hash_code, const bool is_root = true);

private:

  int inner_get_shared_expr(ObRawExprEntry &entry,
                            ObRawExpr *&new_expr);
  inline bool is_blacklist_share_expr(const ObRawExpr &expr)
  {
    return expr.is_column_ref_expr() ||
           expr.is_aggr_expr() ||
           expr.is_win_func_expr() ||
           expr.is_query_ref_expr() ||
           expr.is_exec_param_expr() ||
           expr.is_pseudo_column_expr() ||
           expr.get_expr_type() == T_OP_ROW ||
           expr.get_expr_type() == T_QUESTIONMARK ||
           expr.is_var_expr();
  }
  inline bool is_blacklist_share_child(const ObRawExpr &expr)
  {
    return expr.is_aggr_expr() 
           || expr.is_win_func_expr() 
           || T_OP_CASE == expr.get_expr_type()
           || T_OP_ROW == expr.get_expr_type();
  }
  inline bool is_blacklist_share_const(const ObRawExpr &expr)
  {
    return T_OP_OR == expr.get_expr_type() || T_OP_AND == expr.get_expr_type();
  }
private:
  typedef ObSEArray<ObRawExprEntry, 1> SharedExprs;

  ObArenaAllocator allocator_;
  hash::ObHashMap<uint64_t, SharedExprs *> shared_expr_map_;

  uint64_t scope_id_;
  
  ObQueryCtx *query_ctx_;
  int64_t disable_share_const_level_;
};

}
}
#endif // OB_SHARED_EXPR_RESOLVER_H
