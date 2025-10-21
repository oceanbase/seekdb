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

#ifndef OCEANBASE_SRC_SQL_ENGINE_AGGREGATE_OB_DISTINCT_OP_H_
#define OCEANBASE_SRC_SQL_ENGINE_AGGREGATE_OB_DISTINCT_OP_H_

#include "sql/engine/ob_operator.h"
#include "share/datum/ob_datum_funcs.h"

namespace oceanbase
{
namespace sql
{

class ObDistinctSpec : public ObOpSpec
{
  OB_UNIS_VERSION_V(1);
public:
  ObDistinctSpec(common::ObIAllocator &alloc, const ObPhyOperatorType type);

  INHERIT_TO_STRING_KV("op_spec", ObOpSpec,
                       K_(distinct_exprs), K_(is_block_mode), K_(cmp_funcs));

  // data members
  common::ObFixedArray<ObExpr*, common::ObIAllocator> distinct_exprs_;
  common::ObCmpFuncs cmp_funcs_;
  bool is_block_mode_;
  bool by_pass_enabled_;
};

/**
 * Distinct has nothing common for Hash and Merge, so a DistinctOp base class is not implemented for now
 **/

} // end namespace sql
} // end namespace oceanbase

#endif /* OCEANBASE_SRC_SQL_ENGINE_AGGREGATE_OB_DISTINCT_OP_H_ */
