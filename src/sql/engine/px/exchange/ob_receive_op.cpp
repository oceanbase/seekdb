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

#define USING_LOG_PREFIX SQL_ENG

#include "ob_receive_op.h"
#include "sql/engine/ob_exec_context.h"

namespace oceanbase
{
using namespace common;
namespace sql
{

ObReceiveSpec::ObReceiveSpec(ObIAllocator &alloc, const ObPhyOperatorType type)
    : ObOpSpec(alloc, type),
    partition_order_specified_(false),
    need_set_affected_row_(false),
    is_merge_sort_(false)
{
}

OB_SERIALIZE_MEMBER((ObReceiveSpec, ObOpSpec),
                    partition_order_specified_,
                    need_set_affected_row_,
                    is_merge_sort_);

ObReceiveOp::ObReceiveOp(ObExecContext &exec_ctx, const ObOpSpec &spec, ObOpInput *input)
    : ObOperator(exec_ctx, spec, input)
{
}

} // end namespace sql
} // end namespace oceanbase
