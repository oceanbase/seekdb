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

#include "ob_tablet_autoinc_seq_service.h"
#include "share/rc/ob_module_provider.h"
#include "storage/ls/ob_ls.h"
#include "storage/multi_data_source/mds_ctx.h"
#include "storage/tx/ob_trans_service.h"
#include "storage/tx_storage/ob_ls_service.h"

using namespace oceanbase::share;

namespace oceanbase
{
namespace storage
{

// ObSyncTabletSeqReplayExecutor
ObSyncTabletSeqReplayExecutor::ObSyncTabletSeqReplayExecutor()
  : ObTabletReplayExecutor(), seq_(0), is_tablet_creating_(false), scn_()
{
}

int ObSyncTabletSeqReplayExecutor::init(
    const uint64_t autoinc_seq,
    const bool is_tablet_creating,
    const SCN &replay_scn)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(is_inited_)) {
    ret = OB_INIT_TWICE;
    LOG_WARN("init twice", KR(ret), K_(is_inited));
  } else if (OB_UNLIKELY(autoinc_seq == 0)
          || OB_UNLIKELY(!replay_scn.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arguments", K(autoinc_seq), K(replay_scn), K(ret));
  } else {
    seq_ = autoinc_seq;
    is_tablet_creating_ = is_tablet_creating;
    scn_ = replay_scn;
    is_inited_ = true;
  }

  return ret;
}

int ObSyncTabletSeqReplayExecutor::do_replay_(ObTabletHandle &handle)
{
  int ret = OB_SUCCESS;
  ObTablet *tablet = handle.get_obj();
  if (OB_ISNULL(tablet)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("tablet is null", K(ret), K(handle));
  } else {
    // replay to mds table
    ObArenaAllocator allocator;
    ObTabletAutoincSeq curr_autoinc_seq;

    if (OB_SUCC(ret)) {
      if (OB_FAIL(curr_autoinc_seq.set_autoinc_seq_value(allocator, seq_))) {
        LOG_WARN("failed to set autoinc seq value", K(ret), K(seq_), K(curr_autoinc_seq));
      } else {
        mds::MdsWriter mds_writer(mds::WriterType::AUTO_INC_SEQ, static_cast<int64_t>(seq_));
        mds::MdsCtx mds_ctx(mds_writer);
        if (OB_FAIL(replay_to_mds_table_(handle, curr_autoinc_seq, mds_ctx, scn_))) {
          LOG_WARN("failed to save autoinc seq", K(ret), K(curr_autoinc_seq));
        } else {
          mds_ctx.single_log_commit(scn_, scn_);
        }
      }
    }
  }
  return ret;
}

int ObTabletAutoincSeqReplayExecutor::init(mds::BufferCtx &user_ctx, const share::SCN &scn, const ObTabletAutoincSeq &data)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(is_inited_)) {
    ret = OB_INIT_TWICE;
    TRANS_LOG(WARN, "tablet autoinc replay executor init twice", KR(ret), K_(is_inited));
  } else if (OB_UNLIKELY(!scn.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    TRANS_LOG(WARN, "get invalid argument", KR(ret), K(scn));
  } else {
    user_ctx_ = &user_ctx;
    scn_ = scn;
    data_ = &data;
    is_inited_ = true;
  }
  return ret;
}

int ObTabletAutoincSeqReplayExecutor::do_replay_(ObTabletHandle &tablet_handle)
{
  int ret = OB_SUCCESS;
  mds::MdsCtx &user_ctx = static_cast<mds::MdsCtx&>(*user_ctx_);
  if (OB_FAIL(replay_to_mds_table_(tablet_handle, *data_, user_ctx, scn_))) {
    TRANS_LOG(WARN, "failed to replay to tablet", K(ret));
  }
  return ret;
}

// ObTabletAutoincSeqService
ObTabletAutoincSeqService::ObTabletAutoincSeqService()
  : is_inited_(false), bucket_lock_()
{
}

ObTabletAutoincSeqService::~ObTabletAutoincSeqService()
{
}

ObTabletAutoincSeqService &ObTabletAutoincSeqService::get_instance()
{
  static ObTabletAutoincSeqService service;
  return service;
}

int ObTabletAutoincSeqService::init()
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(is_inited_)) {
    ret = OB_INIT_TWICE;
    LOG_WARN("tablet autoinc sequence service init twice", K(ret));
  } else if (OB_FAIL(bucket_lock_.init(BUCKET_LOCK_BUCKET_CNT))) {
    LOG_WARN("fail to init bucket lock", K(ret));
  } else {
    is_inited_ = true;
  }
  return ret;
}

void ObTabletAutoincSeqService::destroy()
{
  bucket_lock_.destroy();
  is_inited_ = false;
}

static int get_local_ls(ObLS *&ls)
{
  int ret = OB_SUCCESS;
  ObLSService *ls_service = share::g_mp->ls_service();
  ls = nullptr;
  if (OB_ISNULL(ls_service)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("ls service is null", K(ret));
  } else if (OB_FAIL(ls_service->get_ls(ls))) {
    LOG_WARN("failed to get local log stream", K(ret));
  } else if (OB_ISNULL(ls)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("local log stream is null", K(ret));
  }
  return ret;
}

int ObTabletAutoincSeqService::fetch_tablet_autoinc_seq_cache(
    const ObTabletID &tablet_id,
    const uint64_t cache_size,
    ObTabletAutoincInterval &interval)
{
  int ret = OB_SUCCESS;
  ObLS *ls = nullptr;
  ObTabletHandle tablet_handle;
  ObTabletCreateDeleteMdsUserData user_data;
  mds::MdsWriter writer;
  mds::TwoPhaseCommitState trans_stat;
  SCN trans_version;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("not inited", K(ret), K_(is_inited));
  } else if (OB_UNLIKELY(!tablet_id.is_valid() || 0 == cache_size)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), K(tablet_id), K(cache_size));
  } else if (OB_FAIL(get_local_ls(ls))) {
    LOG_WARN("failed to get local log stream", K(ret), K(tablet_id));
  } else {
    ObBucketHashWLockGuard lock_guard(bucket_lock_, tablet_id.hash());
    if (OB_FAIL(ls->get_tablet(
        tablet_id,
        tablet_handle,
        THIS_WORKER.is_timeout_ts_valid()
            ? THIS_WORKER.get_timeout_remain()
            : ObTabletCommon::DEFAULT_GET_TABLET_DURATION_US))) {
      LOG_WARN("failed to get tablet", K(ret), K(tablet_id));
    } else if (OB_FAIL(tablet_handle.get_obj()->ObITabletMdsInterface::get_latest_tablet_status(
        user_data, writer, trans_stat, trans_version))) {
      LOG_WARN("failed to get latest tablet status", K(ret), K(tablet_id));
    } else if (OB_UNLIKELY(mds::TwoPhaseCommitState::ON_COMMIT != trans_stat)) {
      ret = OB_EAGAIN;
      LOG_WARN("tablet status is not committed",
          K(ret), K(user_data), K(trans_stat), K(writer), K(tablet_id));
    } else if (OB_FAIL(tablet_handle.get_obj()->fetch_tablet_autoinc_seq_cache(
        cache_size, interval))) {
      LOG_WARN("failed to fetch tablet autoinc sequence", K(ret), K(tablet_id));
    }
  }
  return ret;
}

int ObTabletAutoincSeqService::batch_get_tablet_autoinc_seq(
    ObIArray<ObTabletAutoincSeqCopyParam> &params)
{
  int ret = OB_SUCCESS;
  ObLS *ls = nullptr;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("not inited", K(ret), K_(is_inited));
  } else if (OB_UNLIKELY(params.empty())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("empty tablet autoinc request", K(ret));
  } else if (OB_FAIL(get_local_ls(ls))) {
    LOG_WARN("failed to get local log stream", K(ret));
  } else {
    for (int64_t i = 0; i < params.count(); ++i) {
      int tmp_ret = OB_SUCCESS;
      ObTabletAutoincSeqCopyParam &param = params.at(i);
      const ObTabletID &src_tablet_id = param.src_tablet_id_;
      ObTabletHandle tablet_handle;
      ObArenaAllocator allocator("BatchGetSeq");
      ObBucketHashRLockGuard lock_guard(bucket_lock_, src_tablet_id.hash());
      if (OB_UNLIKELY(!src_tablet_id.is_valid() || !param.dest_tablet_id_.is_valid())) {
        tmp_ret = OB_INVALID_ARGUMENT;
        LOG_WARN("invalid tablet autoinc parameter", K(tmp_ret), K(param));
      } else if (OB_TMP_FAIL(ls->get_tablet(src_tablet_id, tablet_handle))) {
        LOG_WARN("failed to get tablet", K(tmp_ret), K(src_tablet_id));
      } else {
        ObTabletAutoincSeq autoinc_seq;
        if (OB_TMP_FAIL(tablet_handle.get_obj()->get_autoinc_seq(autoinc_seq, allocator))) {
          LOG_WARN("failed to get latest autoinc sequence", K(tmp_ret), K(src_tablet_id));
        } else if (OB_TMP_FAIL(autoinc_seq.get_autoinc_seq_value(param.autoinc_seq_))) {
          LOG_WARN("failed to get autoinc sequence value", K(tmp_ret), K(src_tablet_id));
        }
      }
      param.ret_code_ = tmp_ret;
    }
  }
  return ret;
}

int ObTabletAutoincSeqService::batch_set_tablet_autoinc_seq(
    ObIArray<ObTabletAutoincSeqCopyParam> &params,
    const bool is_tablet_creating)
{
  int ret = OB_SUCCESS;
  ObLS *ls = nullptr;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("not inited", K(ret), K_(is_inited));
  } else if (OB_UNLIKELY(params.empty())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("empty tablet autoinc request", K(ret));
  } else if (OB_FAIL(get_local_ls(ls))) {
    LOG_WARN("failed to get local log stream", K(ret));
  } else {
    for (int64_t i = 0; i < params.count(); ++i) {
      int tmp_ret = OB_SUCCESS;
      ObTabletAutoincSeqCopyParam &param = params.at(i);
      ObTabletHandle tablet_handle;
      ObBucketHashWLockGuard lock_guard(bucket_lock_, param.dest_tablet_id_.hash());
      if (OB_UNLIKELY(!param.dest_tablet_id_.is_valid())) {
        tmp_ret = OB_INVALID_ARGUMENT;
        LOG_WARN("invalid tablet autoinc parameter", K(tmp_ret), K(param));
      } else if (OB_TMP_FAIL(ls->get_tablet(
          param.dest_tablet_id_,
          tablet_handle,
          ObTabletCommon::DEFAULT_GET_TABLET_DURATION_US,
          ObMDSGetTabletMode::READ_WITHOUT_CHECK))) {
        LOG_WARN("failed to get tablet", K(tmp_ret), K(param));
      } else if (OB_TMP_FAIL(tablet_handle.get_obj()->update_tablet_autoinc_seq(
          param.autoinc_seq_, is_tablet_creating))) {
        LOG_WARN("failed to update tablet autoinc sequence", K(tmp_ret), K(param));
      }
      param.ret_code_ = tmp_ret;
    }
  }
  return ret;
}

int ObTabletAutoincSeqService::replay_update_tablet_autoinc_seq(
    const ObLS *ls,
    const ObTabletID &tablet_id,
    const uint64_t autoinc_seq,
    const bool is_tablet_creating,
    const share::SCN &replay_scn)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(nullptr == ls
      || !tablet_id.is_valid()
      || 0 == autoinc_seq
      || !replay_scn.is_valid_and_not_min())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), K(tablet_id), K(autoinc_seq), K(replay_scn));
  } else {
    ObBucketHashWLockGuard guard(bucket_lock_, tablet_id.hash());
    ObSyncTabletSeqReplayExecutor replay_executor;
    if (OB_FAIL(replay_executor.init(autoinc_seq, is_tablet_creating, replay_scn))) {
      LOG_WARN("failed to init tablet autoinc replay executor",
          K(ret), K(autoinc_seq), K(is_tablet_creating), K(replay_scn));
    } else if (OB_FAIL(replay_executor.execute(replay_scn, tablet_id))) {
      if (OB_TABLET_NOT_EXIST == ret || OB_NO_NEED_UPDATE == ret) {
        LOG_INFO("skip tablet autoinc replay", K(ret), K(tablet_id), K(replay_scn));
        ret = OB_SUCCESS;
      } else if (OB_EAGAIN != ret) {
        LOG_ERROR("failed to replay tablet autoinc sequence",
            K(ret), K(tablet_id), K(replay_scn));
        ret = OB_EAGAIN;
      }
    }
  }
  return ret;
}

int ObTabletAutoincSeqService::batch_set_tablet_autoinc_seq_in_trans(
    ObLS &ls,
    const ObIArray<ObTabletAutoincSeqCopyParam> &params,
    const share::SCN &replay_scn,
    mds::BufferCtx &ctx)
{
  int ret = OB_SUCCESS;
  ObArenaAllocator allocator(common::ObMemAttr("SetAutoSeq"));
  if (OB_UNLIKELY(params.empty())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("empty tablet autoinc request", K(ret));
  }
  for (int64_t i = 0; OB_SUCC(ret) && i < params.count(); ++i) {
    allocator.reuse();
    const ObTabletID &tablet_id = params.at(i).dest_tablet_id_;
    const uint64_t autoinc_seq = params.at(i).autoinc_seq_;
    ObTabletAutoincSeq data;
    ObBucketHashWLockGuard lock_guard(bucket_lock_, tablet_id.hash());
    if (OB_FAIL(data.set_autoinc_seq_value(allocator, autoinc_seq))) {
      LOG_WARN("failed to set autoinc sequence value", K(ret), K(tablet_id), K(autoinc_seq));
    } else if (OB_FAIL(set_tablet_autoinc_seq_in_trans(
        ls, tablet_id, data, replay_scn, ctx))) {
      LOG_WARN("failed to set tablet autoinc MDS data", K(ret), K(tablet_id));
    }
  }
  return ret;
}

int ObTabletAutoincSeqService::set_tablet_autoinc_seq_in_trans(
    ObLS &ls,
    const ObTabletID &tablet_id,
    const ObTabletAutoincSeq &data,
    const share::SCN &replay_scn,
    mds::BufferCtx &ctx)
{
  MDS_TG(100_ms);
  UNUSED(ls);
  int ret = OB_SUCCESS;
  if (!replay_scn.is_valid()) {
    const ObTabletMapKey key(tablet_id);
    ObTabletHandle tablet_handle;
    ObTablet *tablet = nullptr;
    mds::MdsCtx &user_ctx = static_cast<mds::MdsCtx &>(ctx);
    if (CLICK_FAIL(ObTabletCreateDeleteHelper::get_tablet(key, tablet_handle))) {
      LOG_WARN("failed to get tablet", K(ret), K(tablet_id));
    } else if (OB_FALSE_IT(tablet = tablet_handle.get_obj())) {
    } else if (CLICK_FAIL(tablet->ObITabletMdsInterface::set(
        data, user_ctx, 0 /* lock_timeout_us */))) {
      LOG_WARN("failed to set tablet autoinc MDS data", K(ret), K(tablet_id));
    }
  } else {
    ObTabletAutoincSeqReplayExecutor replay_executor;
    if (CLICK_FAIL(replay_executor.init(ctx, replay_scn, data))) {
      LOG_ERROR("failed to init tablet autoinc replay executor", K(ret));
    } else if (CLICK_FAIL(replay_executor.execute(replay_scn, tablet_id))) {
      if (OB_EAGAIN != ret) {
        LOG_ERROR("failed to replay tablet autoinc MDS data", K(ret), K(tablet_id));
      }
    }
  }
  return ret;
}

} // namespace storage
} // namespace oceanbase
