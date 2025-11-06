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
#include "ob_raw_expr_sets.h"
#include "src/sql/resolver/expr/ob_raw_expr.h"

using namespace oceanbase;
using namespace oceanbase::sql;

int ObRawExprSetUtils::to_expr_set(common::ObIAllocator *allocator,
                                   const common::ObIArray<ObRawExpr *> &exprs,
                                   ObRawExprSet &expr_set)
{
  int ret = OB_SUCCESS;
  if (exprs.count() <= 0 || OB_ISNULL(allocator)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("params are invalid", K(ret), K(allocator), K(exprs.count()));
  } else {
    expr_set.set_allocator(allocator);
    if (OB_FAIL(expr_set.init(exprs.count()))) {
      LOG_WARN("failed to init expr set", K(ret));
    } else if (OB_FAIL(common::append(expr_set, exprs))) {
      LOG_WARN("faield to append expr", K(ret));
    }
  }
  return ret;
}

int ObRawExprSetUtils::add_expr_set(common::ObIAllocator *allocator,
                                    const common::ObIArray<ObRawExpr *> &exprs,
                                    ObRawExprSets &expr_sets)
{
  int ret = OB_SUCCESS;
  ObRawExprSet *expr_set = NULL;
  void *ptr = NULL;
  if (OB_ISNULL(allocator)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("params are invalid", K(ret), K(allocator));
  } else if (exprs.count() <= 0) {
    /*do nothing*/
  } else if (OB_ISNULL(ptr = allocator->alloc(sizeof(ObRawExprSet)))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    LOG_WARN("no memory to create ObRawExprSet", K(ret));
  } else {
    expr_set = new(ptr) ObRawExprSet();
    if (OB_FAIL(to_expr_set(allocator, exprs, *expr_set))) {
      LOG_WARN("failed to convert array to expr set", K(ret));
    } else if (OB_FAIL(expr_sets.push_back(expr_set))) {
      LOG_WARN("failed to push back expr set", K(ret));
    }
  }
  if (OB_FAIL(ret) && expr_set != NULL) {
    expr_set->~ObRawExprSet();
  }
  return ret;
}
