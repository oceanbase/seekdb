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

#ifndef OB_RAW_EXPR_SETS_H
#define OB_RAW_EXPR_SETS_H

#include "share/ob_define.h"
#include "lib/container/ob_fixed_array.h"
#include "lib/container/ob_se_array.h"

namespace oceanbase
{
namespace sql
{
class ObRawExpr;
typedef common::ObFixedArray<ObRawExpr*, common::ObIAllocator> ObRawExprSet;
// NOTE: do not reuse on ObRawExprSets
// reuse will not de-construct existed ObFixedArray
// A ObFixedArray can not be reused because its size can not be adjusted.
typedef common::ObSEArray<ObRawExprSet*, 8, common::ModulePageAllocator, true> ObRawExprSets;
typedef ObRawExprSet EqualSet;
typedef ObRawExprSets EqualSets;

class ObRawExprSetUtils
{
public:

  static int to_expr_set(common::ObIAllocator *allocator,
                         const common::ObIArray<ObRawExpr *> &exprs,
                         ObRawExprSet &expr_set);

  static int add_expr_set(common::ObIAllocator *allocator,
                          const common::ObIArray<ObRawExpr *> &exprs,
                          ObRawExprSets &expr_sets);
};



}
}


#endif // OB_RAW_EXPR_SETS_H
