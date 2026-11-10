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

#ifndef OCEANBASE_EXPR_OB_INFIX_EXPRESSION_H_
#define OCEANBASE_EXPR_OB_INFIX_EXPRESSION_H_

#include "sql/engine/expr/ob_postfix_expression.h"
#include "lib/utility/ob_unify_serialize.h"
#include "lib/utility/ob_print_utils.h"

namespace oceanbase
{
namespace sql
{

class ObInfixExprItem : public ObPostExprItem
{
  OB_UNIS_VERSION(1);
public:
  ObInfixExprItem()
      : ObPostExprItem(),
        param_idx_(0), param_num_(0), param_lazy_eval_(false),
        is_boolean_(false)
  {
  }

  uint16_t get_param_idx() const { return param_idx_; }
  uint16_t get_param_num() const { return param_num_; }
  bool is_param_lazy_eval() const { return param_lazy_eval_; }

  void set_param_idx(const uint16_t idx) { param_idx_ = idx; }
  void set_param_num(const uint16_t num) { param_num_ = num; }
  void set_param_lazy_eval() { param_lazy_eval_ = true; }
  void set_is_boolean(bool is_boolean) { is_boolean_ = is_boolean; }
  bool is_boolean() const { return is_boolean_; }

  // deep copy self with allocator
  int deep_copy(common::ObIAllocator &alloc, const bool only_obj = false);

  INHERIT_TO_STRING_KV("parent", ObPostExprItem,
      K(param_idx_), K(param_num_), K(param_lazy_eval_));

protected:
  // index of first parameter
  uint16_t param_idx_;
  // number of parameter
  uint16_t param_num_;
  // parameters are evaluated by expression
  bool param_lazy_eval_;
  bool is_boolean_;
};

// Infix expression, expr stored in %exprs_, expr's child stored in
// ObInfixExprItem::param_idx_ position.
//
// e.g.:
//     a * b + c / d
// exprs_ will be: + * / a b c d
//
class ObInfixExpression
{
  OB_UNIS_VERSION(1);
public:

  typedef ObSqlFixedArray<ObInfixExprItem> ExprItemArray;

  ObInfixExpression(common::ObIAllocator &alloc, const int64_t expr_cnt);
  ~ObInfixExpression();

  void reset();
  int assign(const ObInfixExpression &other);

  int set_item_count(const int64_t count);
  // add item to %exprs_, only %item.get_obj() deep copied
  int add_expr_item(const ObInfixExprItem &item);



  int generate_idx_for_regexp_ops(int16_t &cur_regexp_op_count);

  ExprItemArray &get_exprs() { return exprs_; }
  const ExprItemArray &get_exprs() const { return exprs_; }

  OB_INLINE bool is_empty() const { return 0 == exprs_.count(); }
  bool is_equijoin_cond(int64_t &c1, int64_t &c2,
      common::SqlValueType &cmp_type, common::ObCollationType &cmp_cs_type,
      bool &is_null_safe) const;




  TO_STRING_KV(K_(exprs));

private:


private:
  common::ObIAllocator &alloc_;
  ExprItemArray exprs_;
  // output_column_count_ ?

  DISALLOW_COPY_AND_ASSIGN(ObInfixExpression);
};

} // end namespace sql
} // end namespace oceanbase

#endif // OCEANBASE_EXPR_OB_INFIX_EXPRESSION_H_
