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

#ifndef OCEANBASE_STORAGE_TABLELOCK_OB_LOCK_UTILS_H
#define OCEANBASE_STORAGE_TABLELOCK_OB_LOCK_UTILS_H

#include "share/ob_ls_id.h" // ObLSID
#include "share/inner_table/ob_inner_table_schema.h"
#include "storage/tablelock/ob_table_lock_common.h" // ObTableLockMode

namespace oceanbase
{
namespace common
{
class ObMySQLTransaction;
}

namespace transaction
{
namespace tablelock
{
class ObInnerTableLockUtil
{
public:
  // only inner tables in the white list can be locked
  // please think carefully about circular dependencies before adding inner table into the white list
  static bool in_inner_table_lock_white_list(const uint64_t inner_table_id)
  {
    bool b_ret = share::OB_ALL_BALANCE_JOB_TID == inner_table_id
              || share::OB_ALL_RECOVER_TABLE_JOB_TID == inner_table_id
              || share::OB_ALL_LS_REPLICA_TASK_TID == inner_table_id
              || share::OB_ALL_TRANSFER_PARTITION_TASK_TID == inner_table_id
              || share::OB_FT_DICT_IK_UTF8_TID == inner_table_id
              || share::OB_FT_STOPWORD_IK_UTF8_TID == inner_table_id
              || share::OB_FT_QUANTIFIER_IK_UTF8_TID == inner_table_id
              || share::OB_ALL_AI_MODEL_ENDPOINT_TID == inner_table_id;
    return b_ret;
  }
  /*
   * lock inner table in trans with internal_sql_execute_timeout
   *
   * @param[in] trans:           ObMySQLTransaction
   * @param[in] tenant_id:       tenant_id of the inner table
   * @param[in] inner_table_id:  inner table id which you want to lock
   * @param[in] lock_mode:       table lock mode
   * @param[in] is_from_sql:     is from sql table_lock can retry
   * @return
   * - OB_SUCCESS:               lock inner table successfully
   * - OB_TRY_LOCK_ROW_CONFLICT: lock conflict
   * - other:                    lock failed
   */
  static int lock_inner_table_in_trans(
      common::ObMySQLTransaction &trans,
      const uint64_t tenant_id,
      const uint64_t inner_table_id,
      const ObTableLockMode &lock_mode,
      const bool is_from_sql);
};

class ObLSObjLockUtil
{
public:
  /*
   * lock ls in trans with internal_sql_execute_timeout
   *
   * @param[in] trans:           ObMySQLTransaction
   * @param[in] tenant_id:       tenant_id of the ls
   * @param[in] ls_id:           target log stream(ls) id
   * @param[in] lock_mode:       obj lock mode
   * @return
   * - OB_SUCCESS:               lock ls successfully
   * - OB_TRY_LOCK_ROW_CONFLICT: lock conflict
   * - other:                    lock failed
   */
  static int lock_ls_in_trans(
      common::ObMySQLTransaction &trans,
      const uint64_t tenant_id,
      const share::ObLSID &ls_id,
      const ObTableLockMode &lock_mode);
};

} // end namespace tablelock
} // end namespace transaction
} // end namespace oceanbase
#endif
