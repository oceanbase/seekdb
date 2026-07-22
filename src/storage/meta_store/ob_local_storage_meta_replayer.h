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
#ifndef OCEANBASE_STORAGE_META_STORE_OB_LOCAL_STORAGE_META_REPLAYER_H_
#define OCEANBASE_STORAGE_META_STORE_OB_LOCAL_STORAGE_META_REPLAYER_H_

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
class ObServerRuntimeSuperBlock;
class ObLocalStorageMetaPersister;
class ObLocalStorageCheckpointSlogHandler;
class ObLocalStorageMetaReplayer
{
public:
  ObLocalStorageMetaReplayer()
    : is_inited_(false) {}
  ObLocalStorageMetaReplayer(const ObLocalStorageMetaReplayer &) = delete;
  ObLocalStorageMetaReplayer &operator=(const ObLocalStorageMetaReplayer &) = delete;

  int init(ObLocalStorageMetaPersister &persister,
           ObLocalStorageCheckpointSlogHandler &ckpt_slog_handler);
  void destroy();
  int start_replay(const ObServerRuntimeSuperBlock &super_block);

private:

private:
  bool is_inited_;
  ObLocalStorageMetaPersister *persister_;
  ObLocalStorageCheckpointSlogHandler *ckpt_slog_handler_;
};

} // namespace storage
} // namespace oceanbase
#endif // OCEANBASE_STORAGE_META_STORE_OB_LOCAL_STORAGE_META_REPLAYER_H_
