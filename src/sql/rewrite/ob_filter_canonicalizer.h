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

#ifndef OCEANBASE_SQL_REWRITE_OB_FILTER_CANONICALIZER_H
#define OCEANBASE_SQL_REWRITE_OB_FILTER_CANONICALIZER_H

#include "common/object/ob_object.h"
#include "sql/resolver/expr/ob_raw_expr.h"

namespace oceanbase
{
namespace sql
{

/**
 * @brief Bring logically equivalent filter expressions into the same shape.
 *
 * The optimizer estimates the selectivity of a filter from the syntactic form
 * of its expression, and the vector-index plan then picks one of several ANN
 * execution strategies from that single selectivity number.  Two filters that
 * select exactly the same rows therefore have to end up with the same
 * selectivity, otherwise they can be routed to different ANN strategies and
 * show very different latencies.
 *
 * The rewrite rules that already exist remove duplicate predicates and pull
 * common factors out of an OR chain, but they compare expressions
 * syntactically.  This canonicalizer adds the semantic part: it rewrites an
 * IN list over a column into the equivalent range, so that
 *
 *   col IN (1, 2, 3, ..., 300)
 *
 * and
 *
 *   col >= 1 AND col <= 300
 *
 * are estimated through the same range estimator and therefore produce the
 * same selectivity.
 *
 * The rewrite is only applied when it provably selects the same rows: the
 * column has to be a plain column reference, and the value list has to be a
 * contiguous ascending run of integer literals without duplicates.  A list
 * that skips a value, for example (0, 5, 10), selects fewer rows than the
 * range between its first and its last value, so it is left alone.
 */
class ObFilterCanonicalizer
{
public:
  ObFilterCanonicalizer() : expr_factory_(nullptr) {}
  explicit ObFilterCanonicalizer(ObRawExprFactory *expr_factory) : expr_factory_(expr_factory) {}
  virtual ~ObFilterCanonicalizer() {}

  /**
   * @brief Canonicalize the filter expressions of a statement.
   *
   * @param[in,out] conditions  the filter expressions, rewritten in place
   * @param[out]    trans_happened  set when an expression was rewritten
   */
  int canonicalize(common::ObIArray<ObRawExpr *> &conditions, bool &trans_happened);

private:
  /// Rewrite a single IN list into the equivalent range.
  int fold_in_list_to_range(ObRawExpr *&expr, bool &trans_happened);

  /// Build a two-argument operator expression.
  int create_op_expr(ObItemType op_type,
                     ObRawExpr *&op_expr,
                     ObRawExpr *left_expr,
                     ObRawExpr *right_expr);

  /// Check that the row expression holds a contiguous ascending run of integers.
  bool is_ascending_integer_list(const ObRawExpr *row_expr);

  /// Read the value of an integer constant expression.
  static bool get_integer_value(const ObRawExpr *expr, int64_t &value);

private:
  ObRawExprFactory *expr_factory_;
};

} // end namespace sql
} // end namespace oceanbase

#endif // OCEANBASE_SQL_REWRITE_OB_FILTER_CANONICALIZER_H
