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

#include "storage/meta_store/ob_server_storage_meta_replayer.h"
#include "share/rc/ob_server_runtime.h"
#include "storage/api/storage/runtime/ob_i_server_runtime.h"
#include "storage/meta_store/ob_storage_meta_io_util.h"
#include "storage/slog_ckpt/ob_server_checkpoint_slog_handler.h"
#include "storage/tx_storage/ob_ls_service.h"
#include "storage/ls/ob_ls.h"


namespace oceanbase
{
using namespace omt;
namespace storage
{
int ObServerStorageMetaReplayer::init(
    ObServerCheckpointSlogHandler &ckpt_slog_handler,
    ObIServerRuntime &server_runtime)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(is_inited_)) {
    ret = OB_INIT_TWICE;
    LOG_WARN("ObServerStorageMetaReplayer has inited", K(ret));
  } else {
    ckpt_slog_handler_ = &ckpt_slog_handler;
    server_runtime_ = &server_runtime;
    is_inited_ = true;
  }
  return ret;
}

int ObServerStorageMetaReplayer::start_replay()
{
  int ret = OB_SUCCESS;
  omt::ObServerRuntimeMeta runtime_meta;
  bool runtime_meta_valid = false;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else if (OB_FAIL(ckpt_slog_handler_->start_replay())) {
  } else if (FALSE_IT(ckpt_slog_handler_->get_replay_result(runtime_meta, runtime_meta_valid))) {
  } else if (OB_FAIL(apply_replay_result_(runtime_meta, runtime_meta_valid))) {
  } else if (OB_FAIL(ckpt_slog_handler_->do_post_replay_work())) {
  }

  if (OB_FAIL(ret)) {
  } else if (OB_FAIL(finish_storage_meta_replay_())) {
  } else if(OB_FAIL(online_ls_())) {
  }
  return ret;
}

void ObServerStorageMetaReplayer::destroy()
{
  ckpt_slog_handler_ = nullptr;
  server_runtime_ = nullptr;
  is_inited_ = false;
}

int ObServerStorageMetaReplayer::apply_replay_result_(
    const omt::ObServerRuntimeMeta &runtime_meta, const bool is_valid)
{
  int ret = OB_SUCCESS;
  const int64_t runtime_count = is_valid ? 1 : 0;
  if (is_valid) {
    FLOG_INFO("replay runtime result", K(runtime_meta));
    if (OB_FAIL(server_runtime_->create_runtime(runtime_meta))) {
    }
  }

  if (OB_SUCC(ret) && 0 != runtime_count) {
    server_runtime_->set_synced();
  }

  LOG_INFO("finish replay runtime", K(ret), K(runtime_count));
  return ret;
}

int ObServerStorageMetaReplayer::finish_storage_meta_replay_()
{
  int ret = OB_SUCCESS;
  SERVER_MODULE_SCOPE {
    ObLS *ls = nullptr;
    if (OB_FAIL(::oceanbase::share::server_service<::oceanbase::storage::ObLSService>()->get_ls(ls))) {
    } else if (OB_FAIL(ls->finish_storage_meta_replay())) {
    }
    if (OB_SUCC(ret) && OB_FAIL(::oceanbase::share::server_service<::oceanbase::storage::ObLSService>()->gc_ls_after_replay_slog())) {
      LOG_WARN("fail to gc ls after replay slog", K(ret));
    }
  }
  FLOG_INFO("finish slog replay", K(ret));
  return ret;
}

int ObServerStorageMetaReplayer::online_ls_()
{
  int ret = OB_SUCCESS;
  SERVER_MODULE_SCOPE {
    if (OB_FAIL(::oceanbase::share::server_service<::oceanbase::storage::ObLSService>()->online_ls())) {
    }
  }
  FLOG_INFO("enable replay clog", K(ret));
  return ret;
}


} // namespace storage
} // namespace oceanbase
