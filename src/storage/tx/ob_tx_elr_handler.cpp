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

#include "ob_tx_elr_handler.h"
#include "ob_trans_part_ctx.h"
#include "ob_trans_service.h"

namespace oceanbase
{
namespace transaction
{

void ObTxELRHandler::reset()
{
  elr_prepared_state_ = TxELRState::ELR_INIT;
  mt_ctx_ = NULL;
}

int ObTxELRHandler::check_and_early_lock_release(bool has_row_updated, ObPartTransCtx *ctx)
{
  int ret = OB_SUCCESS;

  if (!ctx->is_can_elr()) {
    // do nothing
  } else {
    ctx->trans_service_->get_tx_version_mgr().update_max_commit_ts(ctx->ctx_tx_data_.get_commit_version(), true);
    if (has_row_updated) {
      if (OB_FAIL(ctx->acquire_ctx_ref())) {
        TRANS_LOG(WARN, "get trans ctx error", K(ret), K(*this));
      } else {
        set_elr_prepared();
      }
    } else {
      // no need to release lock after submit log
      mt_ctx_ = NULL;
    }
  }
  return ret;
}

} //transaction
} //oceanbase
