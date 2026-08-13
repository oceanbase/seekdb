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
#include "storage/slog/ob_storage_logger.h"
#include "share/ob_i_local_device_space_provider.h"

namespace oceanbase
{
namespace storage
{
class ObIServerRuntime;
class ObServerStorageMetaService : public share::ObILocalDeviceSpaceProvider
{
public:
  static ObServerStorageMetaService &get_instance();
  int init(ObIServerRuntime &server_runtime);
  int start();
  void stop();
  void wait();
  void destroy();
  ObServerStorageMetaPersister &get_persister() { return persister_; }
  bool is_started() const { return ATOMIC_LOAD(&is_started_); }
  
  int get_meta_block_list(ObIArray<blocksstable::MacroBlockId> &meta_block_list);
  int get_reserved_size(int64_t &reserved_size) const override;
  int get_server_slogger(ObStorageLogger *&slogger) const;
  int write_checkpoint(bool is_force);
  int set_need_reserved_for_test(const bool need_reserved);

  static constexpr int64_t SLOG_RESERVED_DISK_SIZE = 128 * 1024 * 1024L; // 128M

private:
  ObServerStorageMetaService();
  ~ObServerStorageMetaService() = default;
  ObServerStorageMetaService(const ObServerStorageMetaService &) = delete;
  ObServerStorageMetaService &operator=(const ObServerStorageMetaService &) = delete;

  int get_using_disk_space(int64_t &using_space) const;
  int check_log_disk(const char *data_dir, const char *log_dir);

private:
  bool is_inited_;
  bool is_started_;
  ObServerStorageMetaPersister persister_;
  ObServerStorageMetaReplayer replayer_;
  ObStorageLogger server_slogger_;
  ObServerCheckpointSlogHandler ckpt_slog_handler_;
  bool need_reserved_;
};

#define SERVER_STORAGE_META_SERVICE (oceanbase::storage::ObServerStorageMetaService::get_instance())
#define SERVER_STORAGE_META_PERSISTER (oceanbase::storage::ObServerStorageMetaService::get_instance().get_persister())



} // namespace storage
} // namespace oceanbase

#endif // OCEANBASE_STORAGE_META_STORE_SERVER_STORAGE_META_SERVICE_
