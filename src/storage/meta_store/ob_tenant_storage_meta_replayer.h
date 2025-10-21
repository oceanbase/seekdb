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
#ifndef OCEANBASE_STORAGE_META_STORE_OB_TENANT_STORAGE_META_REPLAYER_H_
#define OCEANBASE_STORAGE_META_STORE_OB_TENANT_STORAGE_META_REPLAYER_H_

#include "lib/allocator/page_arena.h"
#include "storage/blocksstable/ob_macro_block_id.h"

namespace oceanbase
{
namespace blocksstable
{
class ObStorageObjectOpt;
}
namespace storage
{
class ObTenantSuperBlock;
class ObLSItem;
class ObTenantStorageMetaPersister;
class ObTenantCheckpointSlogHandler;
class ObTenantStorageMetaReplayer
{
public:
  ObTenantStorageMetaReplayer()
    : is_inited_(false),
      is_shared_storage_(false) {}
  ObTenantStorageMetaReplayer(const ObTenantStorageMetaReplayer &) = delete;
  ObTenantStorageMetaReplayer &operator=(const ObTenantStorageMetaReplayer &) = delete;
      
  int init(const bool is_share_storage,
           ObTenantStorageMetaPersister &persister,
           ObTenantCheckpointSlogHandler &ckpt_slog_handler);
  void destroy();
  int start_replay(const ObTenantSuperBlock &super_block);
  
private:
#ifdef OB_BUILD_SHARED_STORAGE
  int ss_start_replay_(const ObTenantSuperBlock &super_block);
  int ss_replay_create_ls_(ObArenaAllocator &allocator, const ObLSItem &item);
  int ss_replay_ls_tablets_for_trans_info_tmp_(ObArenaAllocator &allocator, const ObLSItem &item);
  int ss_recover_ls_pending_free_list_(ObArenaAllocator &allocator, const ObLSItem &item);
#endif 

private:
  bool is_inited_;
  bool is_shared_storage_;
  ObTenantStorageMetaPersister *persister_;
  ObTenantCheckpointSlogHandler *ckpt_slog_handler_;
};

} // namespace storage
} // namespace oceanbase
#endif // OCEANBASE_STORAGE_BLOCKSSTALE_OB_TENANT_META_REPLAYER_H_
