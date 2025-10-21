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
#ifndef OCEANBASE_STORAGE_META_STORE_SERVER_STORAGE_META_SERVICE_
#define OCEANBASE_STORAGE_META_STORE_SERVER_STORAGE_META_SERVICE_

#include <stdint.h>
#include "storage/meta_store/ob_server_storage_meta_persister.h"
#include "storage/meta_store/ob_server_storage_meta_replayer.h"
#include "storage/slog_ckpt/ob_server_checkpoint_slog_handler.h"
#include "storage/slog/ob_storage_logger_manager.h"

namespace oceanbase
{
namespace storage
{
class ObServerStorageMetaService
{
public:
  static ObServerStorageMetaService &get_instance();
  int init(const bool is_share_storage);
  int start();
  void stop();
  void wait();
  void destroy();
  ObServerStorageMetaPersister &get_persister() { return persister_; }
  bool is_started() const { return ATOMIC_LOAD(&is_started_); }
  
  int get_meta_block_list(ObIArray<blocksstable::MacroBlockId> &meta_block_list);
  ObStorageLoggerManager &get_slogger_manager() { return slogger_mgr_; }
  int get_reserved_size(int64_t &reserved_size) const;
  int get_server_slogger(ObStorageLogger *&slogger) const;
  int write_checkpoint(bool is_force);

  class ObTenantItemIterator final
  {
  public:
    explicit ObTenantItemIterator():
      idx_(0),
      is_inited_(false),
      server_super_block_()
      {}
    ~ObTenantItemIterator() = default;
    int init();
    int get_next_tenant_item(storage::ObTenantItem &item);
    TO_STRING_KV(K_(idx), K_(is_inited), K_(server_super_block));
  private:
    int64_t idx_;
    bool is_inited_;
    storage::ObServerSuperBlock server_super_block_;
    DISALLOW_COPY_AND_ASSIGN(ObTenantItemIterator);
  };

  int get_tenant_items_by_status(
      const storage::ObTenantCreateStatus status,
      ObIArray<storage::ObTenantItem> &tenant_items);

private:
  ObServerStorageMetaService();
  ~ObServerStorageMetaService() = default;
  ObServerStorageMetaService(const ObServerStorageMetaService &) = delete;
  ObServerStorageMetaService &operator=(const ObServerStorageMetaService &) = delete;

private:
  bool is_inited_;
  bool is_started_;
  bool is_shared_storage_;
  ObServerStorageMetaPersister persister_;
  ObServerStorageMetaReplayer replayer_;
  ObStorageLoggerManager slogger_mgr_;
  ObStorageLogger *server_slogger_;
  ObServerCheckpointSlogHandler ckpt_slog_handler_;
};

#define SERVER_STORAGE_META_SERVICE (oceanbase::storage::ObServerStorageMetaService::get_instance())
#define SERVER_STORAGE_META_PERSISTER (oceanbase::storage::ObServerStorageMetaService::get_instance().get_persister())



} // namespace storage
} // namespace oceanbase

#endif // OCEANBASE_STORAGE_META_STORE_SERVER_STORAGE_META_SERVICE_
