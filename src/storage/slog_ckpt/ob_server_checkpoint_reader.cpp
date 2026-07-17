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

#define USING_LOG_PREFIX STORAGE

#include "storage/slog_ckpt/ob_server_checkpoint_reader.h"

namespace oceanbase
{
namespace storage
{

using namespace oceanbase::common;
using namespace oceanbase::blocksstable;

int ObServerCheckpointReader::read_checkpoint(const ObServerSuperBlock &super_block)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!super_block.is_valid())) {
    ret = OB_ERR_SYS;
    LOG_WARN("super block is invalid", K(ret), K(super_block));
  } else if (OB_FAIL(read_tenant_meta_checkpoint(super_block.body_.tenant_meta_entry_))) {
    LOG_WARN("fail to read tenant meta checkpoint", K(ret), K(super_block));
  }
  return ret;
}


int ObServerCheckpointReader::read_tenant_meta_checkpoint(const MacroBlockId &entry_block)
{
  int ret = OB_SUCCESS;
  ObMemAttr mem_attr(ObModIds::OB_CHECKPOINT);
  if (OB_UNLIKELY(!entry_block.is_valid())) {
    LOG_INFO("has no tenant config checkpoint");
  } else if (OB_FAIL(tenant_meta_item_reader_.init(entry_block, mem_attr))) {
    LOG_WARN("fail to init tenant config item reader", K(ret));
  } else {
    char *item_buf = nullptr;
    int64_t item_buf_len = 0;
    ObMetaDiskAddr addr;
    int ret = OB_SUCCESS;
    while (OB_SUCC(ret)) {
      if (OB_FAIL(tenant_meta_item_reader_.get_next_item(item_buf, item_buf_len, addr))) {
        if (OB_ITER_END != ret) {
          LOG_WARN("fail to get next tenant meta item", K(ret));
        } else {
          ret = OB_SUCCESS;
          break;
        }
      } else if (OB_FAIL(deserialize_tenant_meta(item_buf, item_buf_len))) {
        LOG_WARN("failed to replay_tenant_meta_checkpoint", K(ret));
      }
    }
  }
  return ret;
}

int ObServerCheckpointReader::deserialize_tenant_meta(const char *buf, const int64_t buf_len)
{
  int ret = OB_SUCCESS;

  omt::ObTenantMeta tenant_meta;
  int64_t pos = 0;
  if (OB_ISNULL(buf)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret));
  } else if (OB_FAIL(tenant_meta.deserialize(buf, buf_len, pos))) {
    LOG_WARN("fail to deserialize", K(ret));
  } else {
    // Keep cover semantics (last item wins)
    tenant_meta_ = tenant_meta;
    tenant_meta_valid_ = true;
  }

  return ret;
}

ObIArray<MacroBlockId> &ObServerCheckpointReader::get_meta_block_list()
{
  return tenant_meta_item_reader_.get_meta_block_list();
}

int ObServerCheckpointReader::get_tenant_meta(omt::ObTenantMeta &tenant_meta, bool &is_valid)
{
  int ret = OB_SUCCESS;
  // Checkpoint carries at most the single sys tenant meta;
  // take the last one as the cover semantics of the previous map set_refactored(1UL,..,1)
  is_valid = tenant_meta_valid_;
  if (is_valid) {
    tenant_meta = tenant_meta_;
  }
  return ret;
}

}  // end namespace storage
}  // end namespace oceanbase
