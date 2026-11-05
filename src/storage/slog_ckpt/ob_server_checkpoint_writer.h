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

#ifndef OB_STORAGE_CKPT_SERVER_CHECKPOINT_WRITER_H
#define OB_STORAGE_CKPT_SERVER_CHECKPOINT_WRITER_H

#include "storage/slog_ckpt/ob_linked_macro_block_writer.h"

namespace oceanbase
{

namespace common
{
class ObLogCursor;
}

namespace storage
{

class ObStorageLogger;

class ObServerCheckpointWriter final
{
public:
  ObServerCheckpointWriter() : is_inited_(false), server_slogger_(nullptr) {}
  ~ObServerCheckpointWriter() = default;
  ObServerCheckpointWriter(const ObServerCheckpointWriter &) = delete;
  ObServerCheckpointWriter &operator=(const ObServerCheckpointWriter &) = delete;

  int init(ObStorageLogger *server_slogger);
  int write_checkpoint(const common::ObLogCursor &log_cursor);
  common::ObIArray<blocksstable::MacroBlockId> &get_meta_block_list();

private:
  int write_tenant_meta_checkpoint(blocksstable::MacroBlockId &block_entry);

private:
  bool is_inited_;
  ObStorageLogger *server_slogger_;
  common::ObConcurrentFIFOAllocator allocator_;
  ObLinkedMacroBlockItemWriter tenant_meta_item_writer_;
};

}  // end namespace storage
}  // end namespace oceanbase

#endif  // OB_STORAGE_CKPT_SERVER_CHECKPOINT_WRITER_H
