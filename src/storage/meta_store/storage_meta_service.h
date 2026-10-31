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
#pragma once

#include "share/storage/local_store.h"
#include "share/ob_i_local_device_space_provider.h"
#include "share/resource/ob_server_runtime_config.h"
#include "storage/meta_mem/ob_meta_obj_struct.h"

namespace oceanbase { namespace storage {
class ObIServerRuntime;
class ObLSMeta;
class ObTablet;

// Process-owned recovery state; the LocalStore is supplied by Observer.
class StorageMetaService : public share::ObILocalDeviceSpaceProvider
{
public:
  static StorageMetaService &get_instance();
  int init(ObIServerRuntime &runtime, share::LocalStore &store);
  int start(bool bootstrap);
  void stop() { ATOMIC_STORE(&is_started_, false); }
  void destroy();
  bool is_started() const { return ATOMIC_LOAD(&is_started_); }
  int get_reserved_size(int64_t &size) const override;
  int commit_bootstrap();
  int write_resources(const share::ObServerRuntimeConfig &config);
  int update_ls_meta(const ObLSMeta &meta);
  int update_tablet(const common::ObTabletID &id, const ObMetaDiskAddr &addr);
  int write_empty_shell_tablet(ObTablet *tablet, ObMetaDiskAddr &addr);
  int remove_tablet(const common::ObTabletID &id);
  int read_empty_shell(const ObMetaDiskAddr &addr, common::ObArenaAllocator &allocator,
                       char *&buf, int64_t &len);
private:
  StorageMetaService() = default;
  int recover(bool bootstrap);
  int write_tablet(const common::ObTabletID &id, int64_t kind, const share::LocalValue &payload);
  static int check_commit(int ret);
  share::LocalStore *store_ = nullptr;
  ObIServerRuntime *runtime_ = nullptr;
  bool is_started_ = false;
  bool need_reserved_ = false;
};

#define STORAGE_META_SERVICE (oceanbase::storage::StorageMetaService::get_instance())
} } // namespace oceanbase::storage
