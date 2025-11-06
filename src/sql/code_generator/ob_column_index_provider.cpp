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

#include "sql/code_generator/ob_column_index_provider.h"
#include "sql/resolver/expr/ob_raw_expr.h"

namespace oceanbase
{
using namespace common;
namespace sql
{
int RowDesc::init()
{
  static const int64_t BUCKET_SIZE = 256;
  return expr_idx_map_.create(BUCKET_SIZE, ObModIds::OB_SQL_CG, ObModIds::OB_SQL_CG);
}

void RowDesc::reset()
{
  expr_idx_map_.clear();
  exprs_.reset();
}

int RowDesc::assign(const RowDesc &other)
{
  reset();
  return append(other);
}

int RowDesc::append(const RowDesc &other)
{
  int ret = OB_SUCCESS;
  int64_t N = other.get_column_num();
  ObRawExpr *raw_expr = NULL;
  for (int64_t i = 0; OB_SUCC(ret) && i < N; ++i) {
    if (OB_FAIL(other.get_column(i, raw_expr))) {
      SQL_CG_LOG(WARN, "failed to get column", K(ret), K(i), K(other.get_column_num()));
    } else if (OB_ISNULL(raw_expr)) {
      ret = OB_ERR_UNEXPECTED;
      SQL_CG_LOG(WARN, "invalid argument", K(ret));
    } else if (OB_FAIL(add_column(raw_expr))) {
      SQL_CG_LOG(WARN, "failed to add column", K(ret), K(*raw_expr));
    }
  } // end for
  return ret;
}

int RowDesc::add_column(ObRawExpr *raw_expr)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(raw_expr)) {
    ret = OB_INVALID_ARGUMENT;
    SQL_CG_LOG(WARN, "invalid argument", K(ret));
  } else if (OB_FAIL(exprs_.push_back(raw_expr))) {
    SQL_CG_LOG(WARN, "failed to add raw_expr", K(ret), K(*raw_expr));
  } else {
    int64_t idx = OB_INVALID_INDEX;
    ret = expr_idx_map_.get_refactored(reinterpret_cast<int64_t>(raw_expr), idx);
    if (OB_HASH_EXIST == ret) {
      ret = OB_SUCCESS;
    } else if (OB_HASH_NOT_EXIST == ret) {
      if (OB_FAIL(expr_idx_map_.set_refactored(reinterpret_cast<int64_t>(raw_expr),
                                                                 exprs_.count() - 1))) {
        SQL_CG_LOG(WARN, "failed to set", K(ret), K(*raw_expr));
      } else {
        ret = OB_SUCCESS;
      }
    } else if (OB_FAIL(ret)) {
      SQL_CG_LOG(WARN, "failed to get hashmap", K(ret));
    }
  }
  if (OB_SUCC(ret) && !raw_expr->has_flag(IS_COLUMNLIZED)
      && OB_FAIL(raw_expr->add_flag(IS_COLUMNLIZED))) {
    SQL_CG_LOG(WARN, "failed to add flag", K(ret));
  }
  return ret;
}



int64_t RowDesc::get_column_num() const
{
  return exprs_.count();
}

ObRawExpr* RowDesc::get_column(int64_t idx) const
{
  ObRawExpr *ret = NULL;
  if (0 <= idx && idx < exprs_.count()) {
    ret = reinterpret_cast<ObRawExpr*>(exprs_.at(idx));
  }
  return ret;
}

int RowDesc::get_column(int64_t idx, ObRawExpr *&raw_expr) const
{
  int ret = OB_SUCCESS;
  if (0 <= idx && idx < exprs_.count()) {
    raw_expr = reinterpret_cast<ObRawExpr*>(exprs_.at(idx));
  } else {
    ret = OB_ENTRY_NOT_EXIST;
  }
  return ret;
}

int RowDesc::get_idx(const ObRawExpr *raw_expr, int64_t &idx) const
{
  idx = OB_INVALID_INDEX;
  int ret = OB_SUCCESS;
  if (OB_FAIL(expr_idx_map_.get_refactored(reinterpret_cast<int64_t>(raw_expr), idx))) {
    if (OB_HASH_NOT_EXIST == ret) {
      ret = OB_ENTRY_NOT_EXIST;
    }
  }
  return ret;
}


}//end of namespace sql
}//end of namespace oceanbase
