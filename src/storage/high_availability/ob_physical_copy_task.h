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

#ifndef OCEABASE_STORAGE_PHYSICAL_COPY_TASK_
#define OCEABASE_STORAGE_PHYSICAL_COPY_TASK_

#include "lib/thread/ob_work_queue.h"
#include "lib/thread/ob_dynamic_thread_pool.h"
#include "lib/atomic/ob_atomic.h"
#include "share/ob_common_rpc_proxy.h" // ObCommonRpcProxy
#include "share/ob_srv_rpc_proxy.h" // ObPartitionServiceRpcProxy
#include "share/scheduler/ob_tenant_dag_scheduler.h"
#include "storage/ob_storage_rpc.h"
#include "storage/blocksstable/ob_block_sstable_struct.h"
#include "storage/blocksstable/ob_macro_block_meta_mgr.h"
#include "ob_storage_ha_struct.h"
#include "ob_storage_ha_macro_block_writer.h"
#include "ob_storage_ha_reader.h"
#include "storage/blocksstable/ob_sstable.h"
#include "ob_storage_restore_struct.h"
#include "ob_storage_ha_dag.h"
#include "ob_physical_copy_ctx.h"
#include "ob_sstable_copy_finish_task.h"
#include "ob_tablet_copy_finish_task.h"

namespace oceanbase
{
namespace storage
{

class ObSSTableCopyFinishTask;
class ObStorageHAMacroBlockWriter;
class ObPhysicalCopyTask : public share::ObITask
{
public:
  ObPhysicalCopyTask();
  virtual ~ObPhysicalCopyTask();
  int init(
      ObPhysicalCopyCtx *copy_ctx,
      ObSSTableCopyFinishTask *finish_task);
  virtual int process() override;
  virtual int generate_next_task(ObITask *&next_task) override;
  VIRTUAL_TO_STRING_KV(K("ObPhysicalCopyFinishTask"), KP(this), KPC(copy_ctx_));
private:
  int fetch_macro_block_with_retry_(
      ObMacroBlocksWriteCtx &copied_ctx);
  int fetch_macro_block_(
      const int64_t retry_times,
      ObMacroBlocksWriteCtx &copied_ctx);
  int build_macro_block_copy_info_(ObSSTableCopyFinishTask *finish_task);
  int get_macro_block_reader_(
      ObICopyMacroBlockReader *&reader);
  int get_restore_reader_(
      const ObCopyMacroBlockReaderInitParam &init_param,
      ObICopyMacroBlockReader *&reader);
  int get_macro_block_restore_reader_(
      const ObCopyMacroBlockReaderInitParam &init_param,
      ObICopyMacroBlockReader *&reader);
  int get_ddl_macro_block_restore_reader_(
      const ObCopyMacroBlockReaderInitParam &init_param,
      ObICopyMacroBlockReader *&reader);
  int get_remote_macro_block_restore_reader_(
      const ObCopyMacroBlockReaderInitParam &init_param,
      ObICopyMacroBlockReader *&reader);
  int get_macro_block_writer_(
      ObICopyMacroBlockReader *reader,
      ObIndexBlockRebuilder *index_block_rebuilder,
      ObStorageHAMacroBlockWriter *&writer);
  void free_macro_block_reader_(ObICopyMacroBlockReader *&reader);
  void free_macro_block_writer_(ObStorageHAMacroBlockWriter *&writer);
  int build_copy_macro_block_reader_init_param_(
      ObCopyMacroBlockReaderInitParam &init_param);
  int build_data_version_for_macro_block_reuse_(ObCopyMacroBlockReaderInitParam &init_param);
  int record_server_event_();
private:
  // For rebuilder can not retry, define MAX_RETRY_TIMES as 1.
  static const int64_t MAX_RETRY_TIMES = 1;
  static const int64_t OB_FETCH_MAJOR_BLOCK_RETRY_INTERVAL = 1 * 1000 * 1000L;// 1s
  bool is_inited_;
  ObPhysicalCopyCtx *copy_ctx_;
  ObSSTableCopyFinishTask *finish_task_;
  ObITable::TableKey copy_table_key_;
  const ObCopyMacroRangeInfo *copy_macro_range_info_;
  int64_t task_idx_;
  DISALLOW_COPY_AND_ASSIGN(ObPhysicalCopyTask);
};

}
}
#endif
