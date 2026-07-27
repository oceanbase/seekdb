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

#include "lib/allocator/ob_concurrent_fifo_allocator.h"
#include "share/resource/ob_server_runtime_config.h"

namespace oceanbase
{
namespace omt
{
class ObServerRuntimeMeta;
}

namespace storage
{
class ObStorageLogger;
class ObServerRuntimeSuperBlock;
class ObServerStorageMetaPersister
{
public:
  ObServerStorageMetaPersister()
    : is_inited_(false), server_slogger_(nullptr) {}
  ObServerStorageMetaPersister(const ObServerStorageMetaPersister &) = delete;
  ObServerStorageMetaPersister &operator=(const ObServerStorageMetaPersister &) = delete;
      
  int init(ObStorageLogger *server_slogger);
  int start();
  void stop();
  void wait();
  void destroy();
  int prepare_create_runtime(const omt::ObServerRuntimeMeta &meta);
  int commit_create_runtime();
  int abort_create_runtime();
  int update_runtime_super_block(const ObServerRuntimeSuperBlock &super_block);
  int update_server_resources(const share::ObServerRuntimeConfig &runtime_config);
  int clear_runtime_log_dirs();
  
private:
  int write_prepare_create_runtime_slog_(const omt::ObServerRuntimeMeta &meta);
  int write_abort_create_runtime_slog_();
  int write_commit_create_runtime_slog_();
  int write_update_runtime_super_block_slog_(const ObServerRuntimeSuperBlock &super_block);
  int write_update_server_resources_slog_(const share::ObServerRuntimeConfig &runtime_config);


private:
  bool is_inited_;
  storage::ObStorageLogger *server_slogger_;
  common::ObConcurrentFIFOAllocator allocator_;
  
};

} // namespace storage
} // namespace oceanbase
#endif // OCEANBASE_STORAGE_BLOCKSSTALE_OB_STORAGE_META_PERSISTER_H_
