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

#ifndef OB_STORAGE_CKPT_TENANT_STORAGE_CHECKPOINT_READER_H_
#define OB_STORAGE_CKPT_TENANT_STORAGE_CHECKPOINT_READER_H_

#include "storage/slog_ckpt/ob_linked_macro_block_reader.h"
#include "storage/blocksstable/ob_macro_block_id.h"
#include "lib/function/ob_function.h"

namespace oceanbase
{
namespace storage
{

struct ObMetaDiskAddr;

class ObTenantStorageCheckpointReader final
{
public:
  using ObStorageMetaOp = common::ObFunction<int(const ObMetaDiskAddr &addr, const char *buf, const int64_t buf_len)>;
  ObTenantStorageCheckpointReader();
  ~ObTenantStorageCheckpointReader() = default;
  ObTenantStorageCheckpointReader(const ObTenantStorageCheckpointReader &) = delete;
  ObTenantStorageCheckpointReader &operator=(const ObTenantStorageCheckpointReader &) = delete;
  int iter_read_meta_item(const blocksstable::MacroBlockId &entry_block,
                                const ObStorageMetaOp &op,
                                ObIArray<blocksstable::MacroBlockId> &block_list);

  static int read_tablet_checkpoint_by_addr(const ObIArray<blocksstable::MacroBlockId> &block_list,
    const ObMetaDiskAddr &addr, char *item_buf, int64_t &item_buf_len);

};

}  // end namespace storage
}  // end namespace oceanbase

#endif  // OB_STORAGE_CKPT_TENANT_STORAGE_META_READER_H_
