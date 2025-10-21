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

#ifndef OB_STORAGE_CKPT_SERVER_CHECKPOINT_LOG_READER_H
#define OB_STORAGE_CKPT_SERVER_CHECKPOINT_LOG_READER_H

#include "common/log/ob_log_cursor.h"
#include "storage/slog_ckpt/ob_linked_macro_block_reader.h"
#include "observer/omt/ob_tenant_meta.h"
namespace oceanbase
{
namespace storage
{
class ObServerCheckpointReader final
{
public:
  ObServerCheckpointReader() = default;
  ~ObServerCheckpointReader() = default;
  ObServerCheckpointReader(const ObServerCheckpointReader &) = delete;
  ObServerCheckpointReader &operator=(const ObServerCheckpointReader &) = delete;

  int read_checkpoint(const ObServerSuperBlock &super_block);
  common::ObIArray<blocksstable::MacroBlockId> &get_meta_block_list();
  int get_tenant_metas(common::hash::ObHashMap<uint64_t, omt::ObTenantMeta> &tenant_meta_map);
  const common::ObArray<omt::ObTenantMeta> &get_tenant_meta_list() const { return tenant_meta_list_; }

private:
  int read_tenant_meta_checkpoint(const blocksstable::MacroBlockId &entry_block);
  int deserialize_tenant_meta(const char *buf, const int64_t buf_len);

private:
  ObLinkedMacroBlockItemReader tenant_meta_item_reader_;
  common::ObArray<omt::ObTenantMeta> tenant_meta_list_;

};

}  // end namespace storage
}  // end namespace oceanbase

#endif  // OB_STORAGE_CKPT_SERVER_CHECKPOINT_LOG_READER_H
