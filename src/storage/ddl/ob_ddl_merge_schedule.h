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

#ifndef OCEANBASE_STORAGE_DDL_MERGE_SCHEDULE_
#define OCEANBASE_STORAGE_DDL_MERGE_SCHEDULE_

#include "storage/tablet/ob_tablet.h"
#include "storage/ddl/ob_ddl_merge_task.h"

#include "share/scn.h"
#include "storage/meta_mem/ob_tablet_handle.h"
#include "share/scheduler/ob_tenant_dag_scheduler.h"
#include "storage/blocksstable/index_block/ob_index_block_builder.h"
#include "storage/blocksstable/ob_macro_block_struct.h"
#include "storage/ddl/ob_ddl_struct.h"
#include "storage/ddl/ob_tablet_ddl_kv.h"
#include "storage/blocksstable/ob_macro_block_struct.h"
#include "storage/ddl/ob_tablet_ddl_kv_mgr.h"
#include "storage/ddl/ob_direct_load_struct.h"

namespace oceanbase
{
namespace storage
{
class ObLSHandle;
class ObTabletHandle;
class ObDDLMergeScheduler
{
public:
  static int schedule_ddl_merge(ObLSHandle &ls_handle, ObTabletHandle &tablet_handl);
  #ifdef OB_BUILD_SHARED_STORAGE
  static int schedule_ddl_minor_merge_on_demand(const bool need_freeze,
                                                const share::ObLSID &ls_id,
                                                ObDDLKvMgrHandle &ddl_kv_mgr_handle);
  static int finish_log_freeze_ddl_kv(const ObLSID &ls_id, ObTabletHandle &tablet_handle);
  #endif
  static int schedule_tablet_ddl_major_merge(ObLSHandle &ls_handle, ObTabletHandle &tablet_handle);
private:
  /* check need merge */
  static int check_tablet_need_merge(ObTablet &tablet, ObDDLKvMgrHandle &ddl_kv_mgr_handle, bool &need_schedule_merge, ObDDLKVType &ddl_kv_type);
  static int check_need_merge_for_nidem_sn(ObTablet &tablet, ObArray<ObDDLKVHandle> &ddl_kvs, bool &need_schedule_merge, ObDDLKVType &ddl_kv_type);
  static int check_need_merge_for_idem_sn(ObTablet &tablet, ObArray<ObDDLKVHandle> &ddl_kvs, bool &need_schedule_merge, ObDDLKVType &ddl_kv_type);

#ifdef OB_BUILD_SHARED_STORAGE
  static int check_need_merge_for_ss(ObTablet &tablet, ObArray<ObDDLKVHandle> &ddl_kvs, bool &need_schedule_merge, ObDDLKVType &ddl_kv_type);

  static int schedule_task_if_split_src(ObTabletHandle &tablet_handle);
#endif
private:
  static const int64_t PRINT_LOG_INTERVAL = 2 * 60 * 1000 * 1000L; // 2m
};

} // namespace storage
} // namespace oceanbase

#endif
