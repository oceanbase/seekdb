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
#ifndef OCEANBASE_STORAGE_META_STORE_OB_SERVER_STORAGE_META_PERSISTER_H_
#define OCEANBASE_STORAGE_META_STORE_OB_SERVER_STORAGE_META_PERSISTER_H_

#include "share/ob_unit_getter.h"

namespace oceanbase
{
namespace omt
{
class ObTenantMeta;
}

namespace storage
{
class ObStorageLogger;
class ObTenantSuperBlock;
class ObServerStorageMetaPersister
{
public:
  ObServerStorageMetaPersister()
    : is_inited_(false), is_shared_storage_(false),
      server_slogger_(nullptr) {}
  ObServerStorageMetaPersister(const ObServerStorageMetaPersister &) = delete;
  ObServerStorageMetaPersister &operator=(const ObServerStorageMetaPersister &) = delete;
      
  int init(const bool is_share_storage, ObStorageLogger *server_slogger);
  int start();
  void stop();
  void wait();
  void destroy();
  int prepare_create_tenant(const omt::ObTenantMeta &meta, int64_t &epoch);
  int commit_create_tenant(const uint64_t tenant_id, const int64_t epoch);
  int abort_create_tenant(const uint64_t tenant_id, const int64_t epoch);
  int commit_delete_tenant(const uint64_t tenant_id, const int64_t epoch);
  int update_tenant_super_block(const int64_t tenant_epoch, const ObTenantSuperBlock &super_block);
  int update_tenant_unit(const int64_t epoch, const share::ObUnitInfoGetter::ObTenantConfig &unit);
  int clear_tenant_log_dir(const uint64_t tenant_id);
  
  
private:
  int write_prepare_create_tenant_slog_(const omt::ObTenantMeta &meta);
  int write_abort_create_tenant_slog_(uint64_t tenant_id);
  int write_commit_create_tenant_slog_(uint64_t tenant_id);
  int write_prepare_delete_tenant_slog_(uint64_t tenant_id);
  int write_commit_delete_tenant_slog_(uint64_t tenant_id);
  int write_update_tenant_super_block_slog_(const ObTenantSuperBlock &super_block);
  int write_update_tenant_unit_slog_(const share::ObUnitInfoGetter::ObTenantConfig &unit);

#ifdef OB_BUILD_SHARED_STORAGE
  int ss_prepare_create_tenant_(const omt::ObTenantMeta &meta, int64_t &epoch);
  int ss_commit_create_tenant_(const uint64_t tenant_id, const int64_t epoch);
  int ss_abort_create_tenant_(const uint64_t tenant_id, const int64_t epoch);
  int ss_prepare_delete_tenant_(const uint64_t tenant_id, const int64_t epoch);
  int ss_commit_delete_tenant_(const uint64_t tenant_id, const int64_t epoch);

  int ss_write_tenant_super_block_(const int64_t tenant_epoch, const ObTenantSuperBlock &tenant_super_block);
  int ss_write_unit_config_(
      const int64_t tenant_epoch,
      const share::ObUnitInfoGetter::ObTenantConfig &unit_config);

#endif 

private:
  bool is_inited_;
  bool is_shared_storage_;
  storage::ObStorageLogger *server_slogger_;
  common::ObConcurrentFIFOAllocator allocator_;
  
};

} // namespace storage
} // namespace oceanbase
#endif // OCEANBASE_STORAGE_BLOCKSSTALE_OB_STORAGE_META_PERSISTER_H_
