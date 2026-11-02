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

#ifndef OB_STORAGE_COMPACTION_UTIL_H_
#define OB_STORAGE_COMPACTION_UTIL_H_
#include "stdint.h"
namespace oceanbase
{
namespace compaction
{
enum ObMergeType : uint8_t
{
  INVALID_MERGE_TYPE = 0,
  MINOR_MERGE,  // minor merge, compaction several mini sstable into one larger mini sstable
  HISTORY_MINOR_MERGE,
  META_MAJOR_MERGE,
  MINI_MERGE,  // mini merge, only flush memtable
  MAJOR_MERGE,
  MEDIUM_MERGE,
  DDL_KV_MERGE, // only use for ddl dag
  BACKFILL_TX_MERGE,
  MDS_MINI_MERGE,
  MDS_MINOR_MERGE,
  BATCH_EXEC, // for ObBatchExecDag
  CONVERT_CO_MAJOR_MERGE, // convert row store major into columnar store cg sstables
  INC_MAJOR_MERGE,
  // add new merge type here
  // fix merge_type_to_str & ObPartitionMergePolicy::get_merge_tables
  MERGE_TYPE_MAX
};

const char *merge_type_to_str(const ObMergeType &merge_type);
inline bool is_valid_merge_type(const ObMergeType &merge_type)
{
  return merge_type > INVALID_MERGE_TYPE && merge_type < MERGE_TYPE_MAX;
}
inline bool is_major_merge(const ObMergeType &merge_type)
{
  return MAJOR_MERGE == merge_type;
}
inline bool is_medium_merge(const ObMergeType &merge_type)
{
  return MEDIUM_MERGE == merge_type;
}
inline bool is_convert_co_major_merge(const ObMergeType &merge_type)
{
  return CONVERT_CO_MAJOR_MERGE == merge_type;
}
inline bool is_major_merge_type(const ObMergeType &merge_type)
{
  return is_convert_co_major_merge(merge_type) || is_medium_merge(merge_type) || is_major_merge(merge_type);
}
inline bool is_mini_merge(const ObMergeType &merge_type)
{
  return MINI_MERGE == merge_type;
}
inline bool is_minor_merge(const ObMergeType &merge_type)
{
  return MINOR_MERGE == merge_type;
}
inline bool is_multi_version_merge(const ObMergeType &merge_type)
{
  return MINOR_MERGE == merge_type
      || MINI_MERGE == merge_type
      || HISTORY_MINOR_MERGE == merge_type
      || BACKFILL_TX_MERGE == merge_type
      || MDS_MINOR_MERGE == merge_type;
}
inline bool is_history_minor_merge(const ObMergeType &merge_type)
{
  return HISTORY_MINOR_MERGE == merge_type;
}
inline bool is_minor_merge_type(const ObMergeType &merge_type)
{
  return is_minor_merge(merge_type) || is_history_minor_merge(merge_type);
}
inline bool is_meta_major_merge(const ObMergeType &merge_type)
{
  return META_MAJOR_MERGE == merge_type;
}
inline bool is_major_or_meta_merge_type(const ObMergeType &merge_type)
{
  return is_major_merge_type(merge_type) || is_meta_major_merge(merge_type);
}
inline bool is_backfill_tx_merge(const ObMergeType &merge_type)
{
  return BACKFILL_TX_MERGE == merge_type;
}
inline bool is_mds_mini_merge(const ObMergeType &merge_type)
{
  return MDS_MINI_MERGE == merge_type;
}
inline bool is_mds_minor_merge(const ObMergeType &merge_type)
{
  return MDS_MINOR_MERGE == merge_type;
}
inline bool is_mds_merge(const ObMergeType &merge_type)
{
  return is_mds_mini_merge(merge_type) || is_mds_minor_merge(merge_type);
}

enum ObMergeLevel : uint8_t
{
  MACRO_BLOCK_MERGE_LEVEL = 0,
  MICRO_BLOCK_MERGE_LEVEL = 1,
  MERGE_LEVEL_MAX
};

inline bool is_valid_merge_level(const ObMergeLevel &merge_level)
{
  return merge_level >= MACRO_BLOCK_MERGE_LEVEL && merge_level < MERGE_LEVEL_MAX;
}
const char *merge_level_to_str(const ObMergeLevel &merge_level);

enum ObExecMode : uint8_t {
  EXEC_MODE_LOCAL = 0,
  EXEC_MODE_CALC_CKM, // calc checksum, not output macro
  EXEC_MODE_OUTPUT,   // normal compaction, output macro to share_storage
  EXEC_MODE_VALIDATE,   // verify checksum and dump macro block
  EXEC_MODE_MAX
};

inline bool is_valid_exec_mode(const ObExecMode &exec_mode)
{
  return exec_mode >= EXEC_MODE_LOCAL && exec_mode < EXEC_MODE_MAX;
}
inline bool is_local_exec_mode(const ObExecMode &exec_mode)
{
  return EXEC_MODE_LOCAL == exec_mode;
}
inline bool is_output_exec_mode(const ObExecMode &exec_mode)
{
  return EXEC_MODE_OUTPUT == exec_mode;
}
inline bool is_calc_ckm_exec_mode(const ObExecMode &exec_mode)
{
  return EXEC_MODE_CALC_CKM == exec_mode;
}
inline bool is_validate_exec_mode(const ObExecMode &exec_mode)
{
  return EXEC_MODE_VALIDATE == exec_mode;
}
inline bool is_flush_macro_exec_mode(const ObExecMode &exec_mode)
{
  return is_local_exec_mode(exec_mode) || is_output_exec_mode(exec_mode);
}

const char *exec_mode_to_str(const ObExecMode &exec_mode);

enum ObGetMacroSeqStage : uint8_t
{
  BUILD_INDEX_TREE = 0,
  BUILD_TABLET_META = 1,
  GET_NEW_ROOT_MACRO_SEQ = 2, // for next major
  MACRO_SEQ_TYPE_MAX
};
bool is_valid_get_macro_seq_stage(const ObGetMacroSeqStage stage);

const int64_t MAX_MERGE_THREAD = 64;
const int64_t DEFAULT_CG_MERGE_BATCH_SIZE = 10;
const int64_t ALL_CG_IN_ONE_BATCH_CNT = DEFAULT_CG_MERGE_BATCH_SIZE * 2;

} // namespace compaction
} // namespace oceanbase

#endif // OB_STORAGE_COMPACTION_UTIL_H_
