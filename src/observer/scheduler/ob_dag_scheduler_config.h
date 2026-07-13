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

#ifdef DAG_SCHEDULER_DAG_NET_TYPE_DEF
// DAG_SCHEDULER_DAG_NET_TYPE_DEF(DAG_NET_TYPE_ENUM, DAG_NET_TYPE_STR)
DAG_SCHEDULER_DAG_NET_TYPE_DEF(DAG_NET_TYPE_RESERVED_7, "DAG_NET_RESERVED_7")
DAG_SCHEDULER_DAG_NET_TYPE_DEF(DAG_NET_TYPE_MAX, "DAG_NET_TYPE_MAX")
#endif

#ifdef DAG_SCHEDULER_DAG_PRIO_DEF
// DAG_SCHEDULER_DAG_PRIO_DEF(DAG_PRIO_ENUM, DAG_PRIORITY_SCORE, DAG_PRIORITY_STR, DAG_FUNCTION_TYPE)
DAG_SCHEDULER_DAG_PRIO_DEF(DAG_PRIO_COMPACTION_HIGH,   6, "PRIO_COMPACTION_HIGH", PRIO_COMPACTION_HIGH)
DAG_SCHEDULER_DAG_PRIO_DEF(DAG_PRIO_HA_HIGH,        8, "PRIO_HA_HIGH", PRIO_HA_HIGH)
DAG_SCHEDULER_DAG_PRIO_DEF(DAG_PRIO_COMPACTION_MID, 6, "PRIO_COMPACTION_MID", PRIO_COMPACTION_MID)
DAG_SCHEDULER_DAG_PRIO_DEF(DAG_PRIO_COMPACTION_LOW, 6, "PRIO_COMPACTION_LOW", PRIO_COMPACTION_LOW)
DAG_SCHEDULER_DAG_PRIO_DEF(DAG_PRIO_DDL,            2, "PRIO_DDL", PRIO_DDL)
DAG_SCHEDULER_DAG_PRIO_DEF(DAG_PRIO_DDL_HIGH,       6, "PRIO_DDL_HIGH", PRIO_DDL_HIGH)
DAG_SCHEDULER_DAG_PRIO_DEF(DAG_PRIO_TTL,            2, "PRIO_TTL", DEFAULT_FUNCTION)
DAG_SCHEDULER_DAG_PRIO_DEF(DAG_PRIO_MAX,            0, "INVALID", MAX_FUNCTION_NUM)
/*
* Attention! need apply for new therad & new parameter to manage new DAG_PRIO_QUEUE
*/
#endif

#ifdef DAG_SCHEDULER_DAG_TYPE_DEF
// DAG_SCHEDULER_DAG_TYPE_DEF(DAG_TYPE_ENUM, DAG_DEFAULT_PRIO, SYS_TASK_TYPE, DAG_TYPE_STR, DAG_MODULE_STR, DIAGNOSE_WITH_COMMENT, DIAGNOSE_PRIORITY, DIAGNOSE_INT_INFO_CNT, DIAGNOSE_INT_FMT_STR)
DAG_SCHEDULER_DAG_TYPE_DEF(DAG_TYPE_MINI_MERGE, ObDagPrio::DAG_PRIO_COMPACTION_HIGH, ObSysTaskType::SSTABLE_MINI_MERGE_TASK, "MINI_MERGE", "COMPACTION",
    true, 2, {"tablet_id", "compaction_scn"})
DAG_SCHEDULER_DAG_TYPE_DEF(DAG_TYPE_MERGE_EXECUTE, ObDagPrio::DAG_PRIO_COMPACTION_MID, ObSysTaskType::SSTABLE_MINOR_MERGE_TASK, "MINOR_EXECUTE", "COMPACTION",
    true, 2, {"tablet_id", "compaction_scn"})
DAG_SCHEDULER_DAG_TYPE_DEF(DAG_TYPE_MAJOR_MERGE, ObDagPrio::DAG_PRIO_COMPACTION_LOW, ObSysTaskType::SSTABLE_MAJOR_MERGE_TASK, "MAJOR_MERGE/MEDIUM_MERGE", "COMPACTION",
    true, 2, {"tablet_id", "compaction_scn"})
DAG_SCHEDULER_DAG_TYPE_DEF(DAG_TYPE_TX_TABLE_MERGE, ObDagPrio::DAG_PRIO_COMPACTION_HIGH, ObSysTaskType::SPECIAL_TABLE_MERGE_TASK, "TX_TABLE_MERGE", "COMPACTION",
    false, 2, {"tablet_id", "compaction_scn"})
DAG_SCHEDULER_DAG_TYPE_DEF(DAG_TYPE_MDS_MINI_MERGE, ObDagPrio::DAG_PRIO_COMPACTION_HIGH, ObSysTaskType::MDS_MINI_MERGE_TASK, "MDS_MINI_MERGE", "COMPACTION",
    false, 2, {"tablet_id", "flush_scn"})
DAG_SCHEDULER_DAG_TYPE_DEF(DAG_TYPE_VERIFY_CKM, ObDagPrio::DAG_PRIO_COMPACTION_LOW, ObSysTaskType::SSTABLE_MAJOR_MERGE_TASK, "VERIFY_CKM", "COMPACTION",
    false, 1, {"tablet_count"})
DAG_SCHEDULER_DAG_TYPE_DEF(DAG_TYPE_BATCH_FREEZE_TABLETS, ObDagPrio::DAG_PRIO_COMPACTION_LOW, ObSysTaskType::BATCH_FREEZE_TABLET_TASK, "BATCH_FREEZE", "COMPACTION",
    false, 1, {"tablet_count"})
DAG_SCHEDULER_DAG_TYPE_DEF(DAG_TYPE_UPDATE_SKIP_MAJOR, ObDagPrio::DAG_PRIO_COMPACTION_LOW, ObSysTaskType::SSTABLE_MAJOR_MERGE_TASK, "UPDATE_SKIP_MAJOR", "COMPACTION",
    false, 1, {"tablet_count"})
DAG_SCHEDULER_DAG_TYPE_DEF(DAG_TYPE_REFRESH_SSTABLES, ObDagPrio::DAG_PRIO_COMPACTION_LOW, ObSysTaskType::SSTABLE_MAJOR_MERGE_TASK, "REFRESH_MAJOR", "COMPACTION",
    false, 2, {"tablet_id", "compaction_scn"})
/*
 * NOTICE: if you add/delete a compaction dag type here, remember to alter function is_compaction_dag and get_diagnose_tablet_type in ob_tenant_dag_scheduler.h
 * AND update check_ls_compaction_dag_exist_with_cancel
*/

DAG_SCHEDULER_DAG_TYPE_DEF(DAG_TYPE_DDL, ObDagPrio::DAG_PRIO_DDL, ObSysTaskType::DDL_TASK, "DDL_COMPLEMENT", "DDL",
    true, 6, {"source_tablet_id", "dest_tablet_id", "data_table_id", "target_table_id", "schema_version", "snapshot_version"})
DAG_SCHEDULER_DAG_TYPE_DEF(DAG_TYPE_UNIQUE_CHECKING, ObDagPrio::DAG_PRIO_DDL, ObSysTaskType::DDL_TASK, "UNIQUE_CHECK", "DDL",
    true, 2, {"tablet_id", "index_id"})
DAG_SCHEDULER_DAG_TYPE_DEF(DAG_TYPE_SQL_BUILD_INDEX, ObDagPrio::DAG_PRIO_DDL, ObSysTaskType::DDL_TASK, "SQL_BUILD_INDEX", "DDL",
    true, 0, {})
DAG_SCHEDULER_DAG_TYPE_DEF(DAG_TYPE_DDL_KV_MERGE, ObDagPrio::DAG_PRIO_DDL_HIGH, ObSysTaskType::DDL_KV_MERGE_TASK, "DDL_KV_MERGE", "DDL",
    true, 2, {"tablet_id", "rec_scn"})
DAG_SCHEDULER_DAG_TYPE_DEF(DAG_TYPE_DDL_DEL_LOB_META, ObDagPrio::DAG_PRIO_DDL, ObSysTaskType::DDL_TASK, "DDL_DEL_LOB_META", "DROP_VEC_INDEX",
    true, 5, {"table_id", "tablet_id", "dest_tablet_id", "schema_version", "snapshot_version"})
DAG_SCHEDULER_DAG_TYPE_DEF(DAG_TYPE_TABLET_SPLIT, ObDagPrio::DAG_PRIO_DDL, ObSysTaskType::DDL_TABLET_SPLIT, "DDL_TABLET_SPLIT", "DDL",
    true, 1, {"source_tablet_id"})
DAG_SCHEDULER_DAG_TYPE_DEF(DAG_TYPE_LOB_SPLIT, ObDagPrio::DAG_PRIO_DDL, ObSysTaskType::DDL_TABLET_SPLIT, "DDL_TABLET_SPLIT", "DDL",
    true, 1, {"source_tablet_id"})
DAG_SCHEDULER_DAG_TYPE_DEF(DAG_TYPE_FORK_TABLE, ObDagPrio::DAG_PRIO_DDL, ObSysTaskType::DDL_TASK, "DDL_FORK_TABLE", "DDL",
  true, 1, {"source_tablet_id"})


DAG_SCHEDULER_DAG_TYPE_DEF(DAG_TYPE_TABLET_BACKFILL_TX, ObDagPrio::DAG_PRIO_HA_HIGH, ObSysTaskType::BACKFILL_TX_TASK, "TABLET_BACKFILL_TX", "BACKFILL_TX",
    true, 1, {"tablet_id"})
DAG_SCHEDULER_DAG_TYPE_DEF(DAG_TYPE_VECTOR_INDEX, ObDagPrio::DAG_PRIO_TTL, ObSysTaskType::VECTOR_INDEX_TASK, "VECTOR_INDEX_DAG", "VECTOR_INDEX",
    false, 2, {"table_id", "tablet_id"})

DAG_SCHEDULER_DAG_TYPE_DEF(DAG_TYPE_MAX, ObDagPrio::DAG_PRIO_MAX, ObSysTaskType::MAX_SYS_TASK_TYPE, "DAG_TYPE_MAX", "INVALID", false, 0, {})
#endif

#ifdef DIAGNOSE_INFO_PRIORITY_DEF
DIAGNOSE_INFO_PRIORITY_DEF(DIAGNOSE_PRIORITY_LOW)
DIAGNOSE_INFO_PRIORITY_DEF(DIAGNOSE_PRIORITY_MID)
DIAGNOSE_INFO_PRIORITY_DEF(DIAGNOSE_PRIORITY_HIGH)
#endif

#ifndef SRC_SHARE_SCHEDULER_OB_DAG_SCHEDULER_CONFIG_H_
#define SRC_SHARE_SCHEDULER_OB_DAG_SCHEDULER_CONFIG_H_

#include "lib/ob_define.h"
#include "lib/utility/ob_print_utils.h"
#include "ob_sys_task_stat.h"

namespace oceanbase
{
namespace share
{

enum class ObDiagnoseInfoPrio : uint32_t
{
#define DIAGNOSE_INFO_PRIORITY_DEF(diagnose_info_prio) diagnose_info_prio,
#include "ob_dag_scheduler_config.h"
#undef DIAGNOSE_INFO_PRIORITY_DEF
};

struct ObDagPrioStruct
{
  int64_t score_;
  const char *dag_prio_str_;
  TO_STRING_KV(K_(score), K_(dag_prio_str));
};

struct ObDagPrio
{
  enum ObDagPrioEnum
  {
#define DAG_SCHEDULER_DAG_PRIO_DEF(dag_prio, score, dag_prio_str, dag_function_type) dag_prio,
#include "ob_dag_scheduler_config.h"
#undef DAG_SCHEDULER_DAG_PRIO_DEF
  };
};

static constexpr ObDagPrioStruct OB_DAG_PRIOS[] = {
#define DAG_SCHEDULER_DAG_PRIO_DEF(dag_prio, score, dag_prio_str, dag_function_type) \
    {score, dag_prio_str},
#include "ob_dag_scheduler_config.h"
#undef DAG_SCHEDULER_DAG_PRIO_DEF
};

struct ObDagNetTypeStruct
{
  const char *dag_net_type_str_;
  TO_STRING_KV(K_(dag_net_type_str));
};

struct ObDagNetType
{
  enum ObDagNetTypeEnum
  {
#define DAG_SCHEDULER_DAG_NET_TYPE_DEF(dag_net_type, dag_net_type_str) dag_net_type,
#include "ob_dag_scheduler_config.h"
#undef DAG_SCHEDULER_DAG_NET_TYPE_DEF
  };
};

static constexpr ObDagNetTypeStruct OB_DAG_NET_TYPES[] = {
#define DAG_SCHEDULER_DAG_NET_TYPE_DEF(dag_net_type, dag_net_type_str) \
    {dag_net_type_str},
#include "ob_dag_scheduler_config.h"
#undef DAG_SCHEDULER_DAG_NET_TYPE_DEF
};

struct ObDagTypeStruct
{
  ObDagPrio::ObDagPrioEnum init_dag_prio_;
  ObSysTaskType sys_task_type_;
  const char *dag_type_str_;
  const char *dag_module_str_;
  TO_STRING_KV(K_(init_dag_prio), K_(sys_task_type), K_(dag_type_str), K_(dag_module_str));
};

struct ObDagType
{
  enum ObDagTypeEnum : uint8_t
  {
#define DAG_SCHEDULER_DAG_TYPE_DEF(dag_type, init_dag_prio, sys_task_type, dag_type_str, dag_module_str, diagnose_with_comment, diagnose_priority, diagnose_int_info_cnt, ...) dag_type,
#include "ob_dag_scheduler_config.h"
#undef DAG_SCHEDULER_DAG_TYPE_DEF
  };
};

static constexpr ObDagTypeStruct OB_DAG_TYPES[] = {
#define DAG_SCHEDULER_DAG_TYPE_DEF(dag_type, init_dag_prio, sys_task_type, dag_type_str, dag_module_str, diagnose_with_comment, diagnose_priority, diagnose_int_info_cnt, ...) \
    {init_dag_prio, sys_task_type, dag_type_str, dag_module_str},
#include "ob_dag_scheduler_config.h"
#undef DAG_SCHEDULER_DAG_TYPE_DEF
};

inline bool is_compaction_dag(ObDagType::ObDagTypeEnum dag_type)
{
  return ObDagType::DAG_TYPE_MAJOR_MERGE == dag_type ||
         ObDagType::DAG_TYPE_MINI_MERGE == dag_type ||
         ObDagType::DAG_TYPE_MERGE_EXECUTE == dag_type ||
         ObDagType::DAG_TYPE_TX_TABLE_MERGE == dag_type ||
         ObDagType::DAG_TYPE_MDS_MINI_MERGE == dag_type;
}

inline bool is_batch_exec_dag(ObDagType::ObDagTypeEnum dag_type)
{
  return ObDagType::DAG_TYPE_VERIFY_CKM == dag_type
      || ObDagType::DAG_TYPE_BATCH_FREEZE_TABLETS == dag_type
      || ObDagType::DAG_TYPE_UPDATE_SKIP_MAJOR == dag_type;
}

inline bool is_diagnose_dag(ObDagType::ObDagTypeEnum dag_type)
{
  return is_compaction_dag(dag_type)
      || is_batch_exec_dag(dag_type);
}

inline bool is_valid_dag_priority(ObDagPrio::ObDagPrioEnum priority)
{
  return priority < ObDagPrio::DAG_PRIO_MAX && 
         priority >= ObDagPrio::DAG_PRIO_COMPACTION_HIGH;
}

} // namespace share
} // namespace oceanbase
#endif
