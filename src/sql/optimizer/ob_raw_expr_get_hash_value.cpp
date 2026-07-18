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

#define USING_LOG_PREFIX SQL_OPT
#include "ob_raw_expr_get_hash_value.h"

/// interface of ObRawExprVisitor
using namespace oceanbase::sql;
using namespace oceanbase::common;


int ObRawExprGetHashValue::visit(ObConstRawExpr &expr)
{
  seed_ = expr.hash(seed_);
  return OB_SUCCESS;
}

int ObRawExprGetHashValue::visit(ObVarRawExpr &expr)
{
  seed_ = expr.hash(seed_);
  return OB_SUCCESS;
}

int ObRawExprGetHashValue::visit(ObOpPseudoColumnRawExpr &expr)
{
  seed_ = expr.hash(seed_);
  return OB_SUCCESS;
}

int ObRawExprGetHashValue::visit(ObQueryRefRawExpr &expr)
{
  seed_ = expr.hash(seed_);
  return OB_SUCCESS;
}

int ObRawExprGetHashValue::visit(ObPlQueryRefRawExpr &expr)
{
  seed_ = expr.hash(seed_);
  return OB_SUCCESS;
}

int ObRawExprGetHashValue::visit(ObColumnRefRawExpr &expr)
{
  seed_ = expr.hash(seed_);
  return OB_SUCCESS;
}
int ObRawExprGetHashValue::visit(ObOpRawExpr &expr)
{
  seed_ = expr.hash(seed_);
  return OB_SUCCESS;
}
int ObRawExprGetHashValue::visit(ObCaseOpRawExpr &expr)
{
  seed_ = expr.hash(seed_);
  return OB_SUCCESS;
}
int ObRawExprGetHashValue::visit(ObAggFunRawExpr &expr)
{
  seed_ = expr.hash(seed_);
  return OB_SUCCESS;
}
int ObRawExprGetHashValue::visit(ObSysFunRawExpr &expr)
{
  seed_ = expr.hash(seed_);
  return OB_SUCCESS;
}

int ObRawExprGetHashValue::visit(ObSetOpRawExpr &expr)
{
  seed_ = expr.hash(seed_);
  return OB_SUCCESS;
}

int ObRawExprGetHashValue::visit(ObMatchFunRawExpr &expr)
{
  seed_ = expr.hash(seed_);
  return OB_SUCCESS;
}
