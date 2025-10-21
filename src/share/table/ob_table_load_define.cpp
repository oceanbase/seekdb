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

#define USING_LOG_PREFIX CLIENT

#include "ob_table_load_define.h"

namespace oceanbase
{
namespace table
{

OB_SERIALIZE_MEMBER(ObTableLoadConfig,
                    parallel_,
                    batch_size_,
                    max_error_row_count_,
                    dup_action_,
                    is_need_sort_,
                    is_task_need_sort_);

OB_SERIALIZE_MEMBER(ObTableLoadSegmentID,
                    id_);

OB_SERIALIZE_MEMBER(ObTableLoadTransId,
                    segment_id_,
                    trans_gid_);

OB_SERIALIZE_MEMBER(ObTableLoadPartitionId,
                    partition_id_,
                    tablet_id_);

OB_SERIALIZE_MEMBER(ObTableLoadLSIdAndPartitionId,
                    ls_id_,
                    part_tablet_id_);

OB_SERIALIZE_MEMBER(ObTableLoadResultInfo,
                    rows_affected_,
                    records_,
                    deleted_,
                    skipped_,
                    warnings_);

OB_SERIALIZE_MEMBER_SIMPLE(ObTableLoadSequenceNo, sequence_no_);

}  // namespace table
}  // namespace oceanbase
