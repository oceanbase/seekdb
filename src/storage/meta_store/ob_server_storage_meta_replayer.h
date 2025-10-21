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
#ifndef OCEANBASE_STORAGE_META_STORE_OB_SERVER_STORAGE_META_REPLAYER_H_
#define OCEANBASE_STORAGE_META_STORE_OB_SERVER_STORAGE_META_REPLAYER_H_

#include "observer/omt/ob_tenant_meta.h"
#include "lib/hash/ob_hashmap.h"


namespace oceanbase
{
namespace storage
{
class ObServerStorageMetaPersister;
class ObServerCheckpointSlogHandler;
class ObServerStorageMetaReplayer
{
public:
  ObServerStorageMetaReplayer()
    : is_inited_(false),
      is_shared_storage_(false),
      persister_(nullptr),
      ckpt_slog_handler_(nullptr) {}
  ObServerStorageMetaReplayer(const ObServerStorageMetaReplayer &) = delete;
  ObServerStorageMetaReplayer &operator=(const ObServerStorageMetaReplayer &) = delete;
      
  int init(const bool is_share_storage,
           ObServerStorageMetaPersister &persister,
           ObServerCheckpointSlogHandler &ckpt_slog_handler);
  int start_replay();
  void destroy();
  
private:
  typedef common::hash::ObHashMap<uint64_t, omt::ObTenantMeta> TENANT_META_MAP;
  int apply_replay_result_(const TENANT_META_MAP &tenant_meta_map);
  int handle_tenant_creating_(const uint64_t tenant_id, const omt::ObTenantMeta &tenant_meta);
  int handle_tenant_create_commit_(const omt::ObTenantMeta &tenant_meta);
  int handle_tenant_deleting_(const uint64_t tenant_id, const omt::ObTenantMeta &tenant_meta);
  static int finish_storage_meta_replay_();
  static int online_ls_();

#ifdef OB_BUILD_SHARED_STORAGE
  int ss_start_replay_(TENANT_META_MAP &tenant_meta_map) const;
  int ss_read_tenant_super_block_(ObArenaAllocator &allocator, const ObTenantItem &item, ObTenantSuperBlock &super_block) const;
  int ss_read_tenant_unit_(ObArenaAllocator &allocator, const ObTenantItem &item, share::ObUnitInfoGetter::ObTenantConfig &unit) const;
#endif 

private:
  bool is_inited_;
  bool is_shared_storage_;
  ObServerStorageMetaPersister *persister_;
  ObServerCheckpointSlogHandler *ckpt_slog_handler_;
};

} // namespace storage
} // namespace oceanbase
#endif // OCEANBASE_STORAGE_BLOCKSSTALE_OB_STORAGE_META_REPLAYER_H_
