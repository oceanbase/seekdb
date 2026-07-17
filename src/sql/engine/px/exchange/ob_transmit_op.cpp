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

#include "ob_transmit_op.h"

namespace oceanbase
{
using namespace common;
namespace sql
{

OB_SERIALIZE_MEMBER(ObTransmitOpInput);

ObTransmitSpec::ObTransmitSpec(ObIAllocator &alloc, const ObPhyOperatorType type)
: ObOpSpec(alloc, type),
    split_task_count_(0),
    parallel_server_count_(0),
    server_parallel_thread_count_(0),
    px_dop_(0),
    px_single_(false),
    dfo_id_(common::OB_INVALID_ID),
    px_id_(common::OB_INVALID_ID),
    repartition_ref_table_id_(OB_INVALID_ID),
    repartition_type_(OB_REPARTITION_NO_REPARTITION),
    dist_method_(ObPQDistributeMethod::LOCAL),
    unmatch_row_dist_method_(ObPQDistributeMethod::LOCAL),
    null_row_dist_method_(ObNullDistributeMethod::NONE),
    slave_mapping_type_(SlaveMappingType::SM_NONE),
    has_lgi_(false),
    is_rollup_hybrid_(false),
    is_wf_hybrid_(false)
{
}

OB_SERIALIZE_MEMBER((ObTransmitSpec, ObOpSpec),
                    split_task_count_,
                    parallel_server_count_,
                    server_parallel_thread_count_,
                    px_dop_,
                    px_single_,
                    dfo_id_,
                    px_id_,
                    repartition_ref_table_id_,
                    dist_method_,
                    repartition_type_,
                    unmatch_row_dist_method_,
                    slave_mapping_type_,
                    has_lgi_,
                    is_rollup_hybrid_,
                    null_row_dist_method_,
                    is_wf_hybrid_);

ObTransmitOp::ObTransmitOp(ObExecContext &exec_ctx, const ObOpSpec &spec, ObOpInput *input)
: ObOperator(exec_ctx, spec, input)
{
}

} // end namespace sql
} // end namespace oceanbase
