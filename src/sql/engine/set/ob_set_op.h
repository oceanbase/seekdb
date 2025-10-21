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

#ifndef OCEANBASE_BASIC_OB_SET_OB_SET_OP_H_
#define OCEANBASE_BASIC_OB_SET_OB_SET_OP_H_

#include "sql/engine/ob_operator.h"
#include "sql/engine/sort/ob_sort_basic_info.h"

namespace oceanbase
{
namespace sql
{

class ObSetSpec : public ObOpSpec
{
OB_UNIS_VERSION_V(1);
public:
  ObSetSpec(common::ObIAllocator &alloc, const ObPhyOperatorType type);

  INHERIT_TO_STRING_KV("op_spec", ObOpSpec, K_(is_distinct), K_(sort_collations));
  bool is_distinct_;
  ExprFixedArray set_exprs_;
  ObSortCollations sort_collations_;
  ObSortFuncs sort_cmp_funs_;
};

/**
 * MergeSet and HashSet have nothing in common, so an ObSetOp is not created here
 **/
// class ObSetOp : public ObOperator
// {
// public:
//   ObSetOp(ObExecContext &exec_ctx, const ObOpSpec &spec, ObOpInput *input)
//     : ObOperator(exec_ctx, spec, input)
//   {}
//   virtual ~ObSetOp() = 0;
// };

} // end namespace sql
} // end namespace oceanbase

#endif // OCEANBASE_BASIC_OB_SET_OB_SET_OP_H_
