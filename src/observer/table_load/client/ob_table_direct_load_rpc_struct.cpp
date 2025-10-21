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

 #define USING_LOG_PREFIX SERVER

 #include "ob_table_direct_load_rpc_struct.h"

 namespace oceanbase
{
namespace observer
{

// begin
OB_SERIALIZE_MEMBER_SIMPLE(ObTableDirectLoadBeginArg,
                           table_name_,
                           parallel_,
                           max_error_row_count_,
                           dup_action_,
                           timeout_,
                           heartbeat_timeout_,
                           force_create_,
                           is_async_,
                           load_method_,
                           column_names_,
                           part_names_);

OB_SERIALIZE_MEMBER_SIMPLE(ObTableDirectLoadBeginRes,
                           table_id_,
                           task_id_,
                           column_names_,
                           status_,
                           error_code_);

// commit
OB_SERIALIZE_MEMBER_SIMPLE(ObTableDirectLoadCommitArg,
                           table_id_,
                           task_id_);

// abort
OB_SERIALIZE_MEMBER_SIMPLE(ObTableDirectLoadAbortArg,
                           table_id_,
                           task_id_);

// get_status
OB_SERIALIZE_MEMBER_SIMPLE(ObTableDirectLoadGetStatusArg,
                           table_id_,
                           task_id_);

OB_SERIALIZE_MEMBER_SIMPLE(ObTableDirectLoadGetStatusRes,
                           status_,
                           error_code_);

// insert
OB_SERIALIZE_MEMBER_SIMPLE(ObTableDirectLoadInsertArg,
                           table_id_,
                           task_id_,
                           payload_);

// heart_beat
OB_SERIALIZE_MEMBER_SIMPLE(ObTableDirectLoadHeartBeatArg,
                           table_id_,
                           task_id_);

OB_SERIALIZE_MEMBER_SIMPLE(ObTableDirectLoadHeartBeatRes,
                           status_,
                           error_code_);

} // namespace observer
} // namespace oceanbase
