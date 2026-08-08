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

#ifndef SRC_DATA_PLANE_API_SCHEDULER_OB_SYS_TASK_TYPE_H_
#define SRC_DATA_PLANE_API_SCHEDULER_OB_SYS_TASK_TYPE_H_

namespace oceanbase
{
namespace share
{

// Stable scheduler vocabulary shared by data-plane implementations and their
// callers. Runtime task tracking remains private to the scheduler owner.
enum ObSysTaskType
{
  DDL_TASK = 0,
  SSTABLE_MINI_MERGE_TASK,
  SPECIAL_TABLE_MERGE_TASK,
  SSTABLE_MINOR_MERGE_TASK,
  SSTABLE_MAJOR_MERGE_TASK,
  WRITE_CKPT_TASK,
  DDL_KV_MERGE_TASK,
  COMPLEMENT_DATA_TASK,
  BACKFILL_TX_TASK,
  MDS_MINI_MERGE_TASK,
  BATCH_FREEZE_TABLET_TASK,
  VECTOR_INDEX_TASK,
  SSTABLE_MICRO_MINI_MERGE_TASK,
  VECTOR_INDEX_ASYNC_TASK,
  MAX_SYS_TASK_TYPE
};

} // namespace share
} // namespace oceanbase

#endif // SRC_DATA_PLANE_API_SCHEDULER_OB_SYS_TASK_TYPE_H_
