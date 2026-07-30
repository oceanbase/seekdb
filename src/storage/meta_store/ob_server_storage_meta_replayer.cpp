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
#include "observer/omt/ob_server_runtime_controller.h"
#include "share/rc/ob_module_provider.h"
#include "storage/meta_store/ob_storage_meta_io_util.h"
#include "storage/meta_store/ob_server_storage_meta_persister.h"
#include "storage/slog_ckpt/ob_server_checkpoint_slog_handler.h"
#include "storage/tx_storage/ob_ls_service.h"
#include "storage/ls/ob_ls.h"


namespace oceanbase
{
using namespace omt;
namespace storage
{
int ObServerStorageMetaReplayer::init(
    ObServerStorageMetaPersister &persister,
    ObServerCheckpointSlogHandler &ckpt_slog_handler)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(is_inited_)) {
    ret = OB_INIT_TWICE;
    LOG_WARN("ObServerStorageMetaReplayer has inited", K(ret));
  } else {
    persister_ = &persister;
    ckpt_slog_handler_ = &ckpt_slog_handler;
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
    LOG_WARN("fail to start replay", K(ret));
  } else if (FALSE_IT(ckpt_slog_handler_->get_replay_result(runtime_meta, runtime_meta_valid))) {
  } else if (OB_FAIL(apply_replay_result_(runtime_meta, runtime_meta_valid))) {
    LOG_WARN("fail to apply repaly result", K(ret));
  } else if (OB_FAIL(ckpt_slog_handler_->do_post_replay_work())) {
    LOG_WARN("fail to do post replay work", K(ret));
  }

  if (OB_FAIL(ret)) {
  } else if (OB_FAIL(finish_storage_meta_replay_())) {
    LOG_ERROR("fail to finish storage meta replay", KR(ret));
  } else if(OB_FAIL(online_ls_())) {
    LOG_WARN("fail to online_ls", K(ret));
  }
  return ret;
}

void ObServerStorageMetaReplayer::destroy()
{
  persister_ = nullptr;
  ckpt_slog_handler_ = nullptr;
  is_inited_ = false;
}

int ObServerStorageMetaReplayer::apply_replay_result_(
    const omt::ObServerRuntimeMeta &runtime_meta, const bool is_valid)
{
  int ret = OB_SUCCESS;
  const int64_t runtime_count = is_valid ? 1 : 0;
  if (is_valid) {
    const ObServerRuntimeCreateStatus create_status = runtime_meta.create_status_;

    FLOG_INFO("replay runtime result", K(runtime_meta));

    switch (create_status) {
      case ObServerRuntimeCreateStatus::CREATING : {
        if (OB_FAIL(handle_runtime_creating_())) {
          LOG_ERROR("fail to handle runtime creating", K(ret), K(runtime_meta));
        }
        break;
      }
      case ObServerRuntimeCreateStatus::CREATED : {
        if (OB_FAIL(handle_runtime_create_commit_(runtime_meta))) {
          LOG_ERROR("fail to handle runtime create commit", K(ret), K(runtime_meta));
        }
        break;
      }
      case ObServerRuntimeCreateStatus::CREATE_ABORT :
        break;

      default:
        ret = OB_ERR_UNEXPECTED;
        LOG_ERROR("runtime create status error", K(ret), K(runtime_meta));
        break;
    }
  }

  if (OB_SUCC(ret) && 0 != runtime_count) {
    GCTX.server_runtime_controller_->set_synced();
  }

  LOG_INFO("finish replay create runtime", K(ret), K(runtime_count));

  return ret;
}

int ObServerStorageMetaReplayer::handle_runtime_creating_()
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(persister_->clear_runtime_log_dirs())) {
    LOG_ERROR("fail to clear persistent data", K(ret));
  } else if (OB_FAIL(persister_->abort_create_runtime())) {
    LOG_ERROR("fail to ab", K(ret));
  }
  return ret;
}

int ObServerStorageMetaReplayer::handle_runtime_create_commit_(const ObServerRuntimeMeta &runtime_meta)
{
  int ret = OB_SUCCESS;
  

  if (OB_FAIL(ret)) {
  } else if (OB_FAIL(GCTX.server_runtime_controller_->create_runtime(runtime_meta, false/* write_slog */))) {
    LOG_ERROR("fail to replay create runtime", K(ret), K(runtime_meta));
  }


  return ret;
}

int ObServerStorageMetaReplayer::finish_storage_meta_replay_()
{
  int ret = OB_SUCCESS;
  omt::ObServerRuntimeController *omt = GCTX.server_runtime_controller_;
  if (OB_ISNULL(omt)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("unexpected error, server_runtime is nullptr", K(ret));
  }

  if (OB_SUCC(ret)) {
    SERVER_MODULE_SCOPE {
      ObLS *ls = nullptr;
      if (OB_FAIL(share::g_mp->ls_service()->get_ls(ls))) {
        LOG_WARN("failed to get log stream", K(ret));
      } else if (OB_FAIL(ls->finish_storage_meta_replay())) {
        LOG_WARN("finish replay failed", K(ret), KPC(ls));
      }
      if (OB_SUCC(ret) && OB_FAIL(share::g_mp->ls_service()->gc_ls_after_replay_slog())) {
        LOG_WARN("fail to gc ls after replay slog", K(ret));
      }
    }
  }
  FLOG_INFO("finish slog replay", K(ret));
  return ret;
}

int ObServerStorageMetaReplayer::online_ls_()
{
  int ret = OB_SUCCESS;
  omt::ObServerRuntimeController *omt = GCTX.server_runtime_controller_;

  if (OB_ISNULL(omt)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("unexpected error, omt is nullptr", K(ret));
  }
  if (OB_SUCC(ret)) {
    SERVER_MODULE_SCOPE {
      if (OB_FAIL(share::g_mp->ls_service()->online_ls())) {
        LOG_WARN("fail enable replay clog", K(ret));
      }
    }
  }
  FLOG_INFO("enable replay clog", K(ret));
  return ret;
}


} // namespace storage
} // namespace oceanbase
