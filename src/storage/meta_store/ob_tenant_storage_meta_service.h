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
#ifndef OCEANBASE_STORAGE_META_STORE_TENANT_STORAGE_META_SERVICE_
#define OCEANBASE_STORAGE_META_STORE_TENANT_STORAGE_META_SERVICE_

#include <stdint.h>
#include "share/rc/ob_module_provider.h"
#include "storage/meta_store/ob_tenant_storage_meta_persister.h"
#include "storage/meta_store/ob_tenant_storage_meta_replayer.h"
#include "storage/blockstore/ob_shared_object_reader_writer.h"
#include "storage/meta_store/ob_tenant_seq_generator.h"
#include "storage/slog_ckpt/ob_tenant_checkpoint_slog_handler.h"
#include "storage/slog/ob_storage_logger.h"

namespace oceanbase
{
namespace storage
{
struct ObGCTabletMetaInfoList;
class ObTenantStorageMetaService
{
public:
  ObTenantStorageMetaService();
  ~ObTenantStorageMetaService() = default;
  ObTenantStorageMetaService(const ObTenantStorageMetaService &) = delete;
  ObTenantStorageMetaService &operator=(const ObTenantStorageMetaService &) = delete;

  static int mtl_init(ObTenantStorageMetaService *&meta_service);
  int init();
  int start();
  void stop();
  void wait();
  void destroy();
  bool is_started() { return is_started_; }
  ObTenantStorageMetaPersister &get_persister() { return persister_; }
  ObTenantStorageMetaReplayer &get_replayer() { return replayer_; }
  ObTenantSeqGenerator &get_seq_generator() { return seq_generator_; }
  int get_active_cursor(common::ObLogCursor &log_cursor);
  int get_meta_block_list(ObIArray<blocksstable::MacroBlockId> &meta_block_list);
  int write_checkpoint(bool is_force);
  int add_snapshot(const ObTenantSnapshotMeta &tenant_snapshot);
  int delete_snapshot(const share::ObTenantSnapshotID &snapshot_id);
  int swap_snapshot(const ObTenantSnapshotMeta &tenant_snapshot);
  int clone_ls(
      observer::ObStartupAccelTaskHandler* startup_accel_handler,
      const blocksstable::MacroBlockId &tablet_meta_entry);
  int read_from_disk(
      const ObMetaDiskAddr &addr,
      const int64_t ls_epoch,
      common::ObArenaAllocator &allocator,
      char *&buf,
      int64_t &buf_len);
  int read_from_share_blk(
      const ObMetaDiskAddr &addr,
      const int64_t ls_epoch,
      common::ObArenaAllocator &allocator,
      char *&buf,
      int64_t &buf_len);
  const ObTenantCheckpointSlogHandler& get_ckpt_slog_hdl() const { return ckpt_slog_handler_; };

  ObSharedObjectReaderWriter &get_shared_object_reader_writer() { return shared_object_rwriter_; }
  ObSharedObjectReaderWriter &get_shared_object_raw_reader_writer() { return shared_object_raw_rwriter_; }
  storage::ObStorageLogger &get_slogger() { return slogger_; }

private:
private:
  bool is_inited_;
  bool is_started_;
  bool is_shared_storage_;
  ObTenantCheckpointSlogHandler ckpt_slog_handler_;
  storage::ObStorageLogger slogger_;
  ObTenantSeqGenerator seq_generator_;
  ObTenantStorageMetaPersister persister_;
  ObTenantStorageMetaReplayer replayer_;
  ObSharedObjectReaderWriter shared_object_rwriter_;
  ObSharedObjectReaderWriter shared_object_raw_rwriter_;
  
};

#define TENANT_STORAGE_META_PERSISTER (share::g_mp->tenant_storage_meta_service()->get_persister())
#define TENANT_SEQ_GENERATOR (share::g_mp->tenant_storage_meta_service()->get_seq_generator())

} // namespace storage
} // namespace oceanbase

#endif // OCEANBASE_STORAGE_META_STORE_TENANT_STORAGE_META_SERVICE_
