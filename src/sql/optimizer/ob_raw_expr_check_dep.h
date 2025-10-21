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

#ifndef _OB_RAW_EXPR_CHECK_DEP_H
#define _OB_RAW_EXPR_CHECK_DEP_H 1
#include "lib/container/ob_bit_set.h"
#include "lib/container/ob_iarray.h"

namespace oceanbase {
namespace sql {
  class ObRawExpr;
  class ObRawExprCheckDep
  {
  public:
  ObRawExprCheckDep(common::ObIArray<ObRawExpr *> &dep_exprs,
                    common::ObBitSet<common::OB_MAX_BITSET_SIZE> &deps,
                    bool is_access)
      : dep_exprs_(dep_exprs),
        dep_indices_(&deps),
        is_access_(is_access) { }
    virtual ~ObRawExprCheckDep() {}

    /**
     *  The starting point
     */
    int check(const ObRawExpr &expr);

    int check(const ObIArray<ObRawExpr *> &exprs);

    const common::ObBitSet<common::OB_MAX_BITSET_SIZE> *get_dep_indices() const { return dep_indices_; }
  private:
    int check_expr(const ObRawExpr &expr, bool &found);
  private:
    common::ObIArray<ObRawExpr *> &dep_exprs_;
    common::ObBitSet<common::OB_MAX_BITSET_SIZE> *dep_indices_;
    bool is_access_; // mark whether we are do project pruning for access exprs
    DISALLOW_COPY_AND_ASSIGN(ObRawExprCheckDep);
  };
}
}

#endif // _OB_RAW_EXPR_CHECK_DEPENDENCY_H

