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

#include "storage/meta_store/ob_server_runtime_meta.h"
#include "lib/hash/ob_hashmap.h"

namespace oceanbase
{
namespace storage
{
class ObServerCheckpointSlogHandler;
class ObIServerRuntime;
class ObServerStorageMetaReplayer
{
public:
  ObServerStorageMetaReplayer()
    : is_inited_(false),
      ckpt_slog_handler_(nullptr),
      server_runtime_(nullptr) {}
  ObServerStorageMetaReplayer(const ObServerStorageMetaReplayer &) = delete;
  ObServerStorageMetaReplayer &operator=(const ObServerStorageMetaReplayer &) = delete;
      
  int init(ObServerCheckpointSlogHandler &ckpt_slog_handler,
           ObIServerRuntime &server_runtime);
  int start_replay();
  void destroy();
  
private:
  int apply_replay_result_(const omt::ObServerRuntimeMeta &runtime_meta, const bool is_valid);
  int finish_storage_meta_replay_();
  int online_ls_();



private:
  bool is_inited_;
  ObServerCheckpointSlogHandler *ckpt_slog_handler_;
  ObIServerRuntime *server_runtime_;
};

} // namespace storage
} // namespace oceanbase
#endif // OCEANBASE_STORAGE_BLOCKSSTALE_OB_STORAGE_META_REPLAYER_H_
