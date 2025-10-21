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

#ifndef OCEANBASE_STORAGE_OB_LS_GET_MOD
#define OCEANBASE_STORAGE_OB_LS_GET_MOD

namespace oceanbase
{
namespace storage
{
enum class ObLSGetMod : int
{
  INVALID_MOD = -1,
  TRANS_MOD = 0,
  STORAGE_MOD = 1,
  RS_MOD = 2,
  LOG_MOD = 3,
  OBSERVER_MOD = 4,
  ARCHIVE_MOD = 5,
  DAS_MOD = 6,
  SHARE_MOD = 7,
  APPLY_MOD = 8,
  ADAPTER_MOD = 9,
  DEADLOCK_MOD = 10,
  TABLELOCK_MOD = 11,
  HA_MOD = 12,
  TABLET_MOD = 13,
  DDL_MOD = 14,
  TXSTORAGE_MOD = 15,
  LEADER_COORDINATOR_MOD = 16,
  DATA_DICT_MOD = 17,
  DATA_MEMTABLE_MOD = 18,
  MULTI_VERSION_GARBAGE_COLLOECTOR_MOD = 19,
  MDS_TABLE_MOD = 20,
  COMPACT_MODE = 21,
  SS_PREWARM_MOD = 22,
  SHARED_META_SERVICE = 23,
  STORAGE_CACHE_POLICY_MOD = 24,
  TOTAL_MAX_MOD = 25,
};

}
}

#endif // OCEANBASE_STORAGE_OB_LS_GET_MOD
