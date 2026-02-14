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
  const int64_t MAX_TENANT_CNT = 512;
  const char* MEM_LABEL = "SvrStoreMetaReplayer";
  TENANT_META_MAP tenant_meta_map;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else if (OB_FAIL(tenant_meta_map.create(MAX_TENANT_CNT, MEM_LABEL, MEM_LABEL))) {
    LOG_WARN("create tenant meta map fail", K(ret));
  } else if (OB_FAIL(ckpt_slog_handler_->start_replay(tenant_meta_map))) {
    LOG_WARN("fail to start replay", K(ret));
  } else if (OB_FAIL(apply_replay_result_(tenant_meta_map))) {
    LOG_WARN("fail to apply repaly result", K(ret));
  } else if (OB_FAIL(ckpt_slog_handler_->do_post_replay_work())) {
    LOG_WARN("fail to do post repaly work", K(ret));
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

int ObServerStorageMetaReplayer::apply_replay_result_(const TENANT_META_MAP &tenant_meta_map)
{
  int ret = OB_SUCCESS;
  int64_t tenant_count = tenant_meta_map.size();
  for (TENANT_META_MAP::const_iterator iter = tenant_meta_map.begin();
      OB_SUCC(ret) && iter !=  tenant_meta_map.end(); iter++) {
    const omt::ObTenantMeta &tenant_meta = iter->second;
    ObTenantCreateStatus create_status = tenant_meta.create_status_;
    const uint64_t tenant_id = iter->first;

    FLOG_INFO("replay tenant result", K(tenant_id), K(tenant_meta));

    switch (create_status) {
      case ObTenantCreateStatus::CREATING : {
        if (OB_FAIL(handle_tenant_creating_(tenant_id, tenant_meta))) {
          LOG_ERROR("fail to handle tenant creating", K(ret), K(tenant_meta));
        }
        break;
      }
      case ObTenantCreateStatus::CREATED : {
        if (OB_FAIL(handle_tenant_create_commit_(tenant_meta))) {
          LOG_ERROR("fail to handle tenant create commit", K(ret), K(tenant_meta));
        }
        break;
      }
      case ObTenantCreateStatus::DELETING : {
        if (OB_FAIL(handle_tenant_deleting_(tenant_id, tenant_meta))) {
          LOG_ERROR("fail to handle tenant deleting", K(ret), K(tenant_meta));
        }
        break;
      }
      case ObTenantCreateStatus::DELETED :
      case ObTenantCreateStatus::CREATE_ABORT :
        break;

      default:
        ret = OB_ERR_UNEXPECTED;
        LOG_ERROR("tenant create status error", K(ret), K(tenant_meta));
        break;
    }
  }

  if (OB_SUCC(ret) && 0 != tenant_count) {
    GCTX.omt_->set_synced();
  }

  LOG_INFO("finish replay create tenants", K(ret), K(tenant_count));

  return ret;
}

int ObServerStorageMetaReplayer::handle_tenant_creating_(
    const uint64_t tenant_id, const ObTenantMeta &tenant_meta)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(persister_->clear_tenant_log_dir(tenant_id))) {
    LOG_ERROR("fail to clear persistent data", K(ret), K(tenant_id));
  } else if (OB_FAIL(persister_->abort_create_tenant(tenant_id, tenant_meta.epoch_))) {
    LOG_ERROR("fail to ab", K(ret), K(tenant_id));
  }
  return ret;
}

int ObServerStorageMetaReplayer::handle_tenant_create_commit_(const ObTenantMeta &tenant_meta)
{
  int ret = OB_SUCCESS;
  const uint64_t tenant_id = tenant_meta.unit_.tenant_id_;

  if (OB_FAIL(ret)) {
  } else if (OB_FAIL(GCTX.omt_->create_tenant(tenant_meta, false/* write_slog */))) {
    LOG_ERROR("fail to replay create tenant", K(ret), K(tenant_meta));
  }


  return ret;
}

int ObServerStorageMetaReplayer::handle_tenant_deleting_(
    const uint64_t tenant_id, const ObTenantMeta &tenant_meta)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(persister_->clear_tenant_log_dir(tenant_id))) {
    LOG_ERROR("fail to clear tenant log dir", K(ret), K(tenant_id));
  } else if (OB_FAIL(persister_->commit_delete_tenant(tenant_id, tenant_meta.epoch_))) {
    LOG_ERROR("fail to commit delete tenant", K(ret), K(tenant_id));
  }
  return ret;
}

int ObServerStorageMetaReplayer::finish_storage_meta_replay_()
{
  int ret = OB_SUCCESS;
  common::ObArray<uint64_t> tenant_ids;
  omt::ObMultiTenant *omt = GCTX.omt_;
  if (OB_ISNULL(omt)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("unexpected error, omt is nullptr", K(ret));
  } else if (OB_FAIL(omt->get_mtl_tenant_ids(tenant_ids))) {
    LOG_WARN("fail to get_mtl_tenant_ids", K(ret));
  }

  for (int64_t i = 0; OB_SUCC(ret) && i < tenant_ids.size(); i++) {
    const uint64_t &tenant_id = tenant_ids.at(i);
    MTL_SWITCH(tenant_id) {
      common::ObSharedGuard<ObLSIterator> ls_iter;
      ObLS *ls = nullptr;
      ObLSTabletService *ls_tablet_svr = nullptr;
      if (OB_FAIL(MTL(ObLSService *)->get_ls_iter(ls_iter, ObLSGetMod::STORAGE_MOD))) {
        LOG_WARN("failed to get ls iter", K(ret));
      } else {
        while (OB_SUCC(ret)) {
          if (OB_FAIL(ls_iter->get_next(ls))) {
            if (OB_ITER_END != ret) {
              LOG_WARN("fail to get next ls", K(ret));
            }
          } else if (nullptr == ls) {
            ret = OB_ERR_UNEXPECTED;
            LOG_WARN("ls is null", K(ret));
          } else if (OB_FAIL(ls->finish_storage_meta_replay())) {
            LOG_WARN("finish replay failed", K(ret), KPC(ls));
          }
        }
        if (OB_ITER_END == ret) {
          if (OB_FAIL(MTL(ObLSService*)->gc_ls_after_replay_slog())) {
            LOG_WARN("fail to gc ls after replay slog", K(ret));
          }
        }
      }
    }
  }
  FLOG_INFO("finish slog replay", K(ret));
  return ret;
}

int ObServerStorageMetaReplayer::online_ls_()
{
  int ret = OB_SUCCESS;
  common::ObArray<uint64_t> tenant_ids;
  omt::ObMultiTenant *omt = GCTX.omt_;

  if (OB_ISNULL(omt)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("unexpected error, omt is nullptr", K(ret));
  } else if (OB_FAIL(omt->get_mtl_tenant_ids(tenant_ids))) {
    LOG_WARN("fail to get_mtl_tenant_ids", K(ret));
  }
  for (int64_t i = 0; OB_SUCC(ret) && i < tenant_ids.size(); i++) {
    const uint64_t &tenant_id = tenant_ids.at(i);
    MTL_SWITCH(tenant_id) {
      if (OB_FAIL(MTL(ObLSService*)->online_ls())) {
        LOG_WARN("fail enable replay clog", K(ret));
      }
    }
  }
  FLOG_INFO("enable replay clog", K(ret));
  return ret;
}


} // namespace storage
} // namespace oceanbase
