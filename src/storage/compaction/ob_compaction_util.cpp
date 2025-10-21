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

#include "storage/compaction/ob_compaction_util.h"
#include "share/ob_define.h"
namespace oceanbase
{
namespace compaction
{

const static char * ObMergeTypeStr[] = {
    "INVALID_MERGE_TYPE",
    "MINOR_MERGE",
    "HISTORY_MINOR_MERGE",
    "META_MAJOR_MERGE",
    "MINI_MERGE",
    "MAJOR_MERGE",
    "MEDIUM_MERGE",
    "DDL_KV_MERGE",
    "BACKFILL_TX_MERGE",
    "MDS_MINI_MERGE",
    "MDS_MINOR_MERGE",
    "BATCH_EXEC",
    "CONVERT_CO_MAJOR_MERGE",
    "INC_MAJOR_MERGE",
    "EMPTY_MERGE_TYPE"
};

const char *merge_type_to_str(const ObMergeType &merge_type)
{
  STATIC_ASSERT(static_cast<int64_t>(MERGE_TYPE_MAX + 1) == ARRAYSIZEOF(ObMergeTypeStr), "merge type str len is mismatch");
  const char *str = "";
  if (is_valid_merge_type(merge_type)) {
    str = ObMergeTypeStr[merge_type];
  } else {
    str = "invalid_merge_type";
  }
  return str;
}

const static char * ObMergeLevelStr[] = {
    "MACRO_BLOCK_LEVEL",
    "MICRO_BLOCK_LEVEL"
};

const char *merge_level_to_str(const ObMergeLevel &merge_level)
{
  STATIC_ASSERT(static_cast<int64_t>(MERGE_LEVEL_MAX) == ARRAYSIZEOF(ObMergeLevelStr), "merge level str len is mismatch");
  const char *str = "";
  if (is_valid_merge_level(merge_level)) {
    str = ObMergeLevelStr[merge_level];
  } else {
    str = "invalid_merge_level";
  }
  return str;
}

const static char * ObExecModeStr[] = {
  "EXEC_MODE_LOCAL",
  "EXEC_MODE_CALC_CKM",
  "EXEC_MODE_OUTPUT",
  "EXEC_MODE_VALIDATE"
};

const char *exec_mode_to_str(const ObExecMode &exec_mode)
{
  STATIC_ASSERT(static_cast<int64_t>(EXEC_MODE_MAX) == ARRAYSIZEOF(ObExecModeStr), "exec mode str len is mismatch");
  const char *str = "";
  if (is_valid_exec_mode(exec_mode)) {
    str = ObExecModeStr[exec_mode];
  } else {
    str = "invalid_exec_mode";
  }
  return str;
}

bool is_valid_get_macro_seq_stage(const ObGetMacroSeqStage stage)
{
  return stage >= BUILD_INDEX_TREE && stage < MACRO_SEQ_TYPE_MAX;
}

} // namespace compaction
} // namespace oceanbase
