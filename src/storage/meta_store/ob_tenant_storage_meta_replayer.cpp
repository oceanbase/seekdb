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

#include "ob_tenant_storage_meta_replayer.h"
#include "share/rc/ob_module_provider.h"
#include "storage/meta_store/ob_storage_meta_io_util.h"
#include "storage/tx_storage/ob_ls_service.h"
#include "storage/slog_ckpt/ob_tenant_checkpoint_slog_handler.h"

namespace oceanbase
{
using namespace omt;
namespace storage
{
int ObTenantStorageMetaReplayer::init(
    ObTenantStorageMetaPersister &persister,
    ObTenantCheckpointSlogHandler &ckpt_slog_handler)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(is_inited_)) {
    ret = OB_INIT_TWICE;
    LOG_WARN("has inited", K(ret));
  } else {
    
    persister_ = &persister;
    ckpt_slog_handler_ = &ckpt_slog_handler;
    is_inited_ = true;
  }
  return ret;
}

int ObTenantStorageMetaReplayer::start_replay(const ObTenantSuperBlock &super_block)
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else if (OB_UNLIKELY(!super_block.is_valid())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("tenant super block invalid", K(ret), K(super_block));
  } else {
    if (OB_FAIL(ckpt_slog_handler_->start_replay(super_block))) {
    }
  }
  return ret;
}

void ObTenantStorageMetaReplayer::destroy()
{
  
  persister_ = nullptr;
  ckpt_slog_handler_ = nullptr;
  is_inited_ = false;
}


} // namespace storage
} // namespace oceanbase
