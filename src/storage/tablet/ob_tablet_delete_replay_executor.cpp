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

#include "storage/tablet/ob_tablet_delete_replay_executor.h"

#define USING_LOG_PREFIX MDS

namespace oceanbase
{
namespace storage
{

OB_SERIALIZE_MEMBER(ObRemoveTabletArg, ls_id_, tablet_id_);


// ObTabletDeleteReplayExecutor
ObTabletDeleteReplayExecutor::ObTabletDeleteReplayExecutor()
  :logservice::ObTabletReplayExecutor(), ctx_(nullptr)
{}

int ObTabletDeleteReplayExecutor::init(
    mds::BufferCtx &ctx,
    const share::SCN &scn,
    const bool for_old_mds)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(is_inited_)) {
    ret = OB_INIT_TWICE;
    LOG_WARN("tablet delete replay executor init twice", KR(ret), K_(is_inited));
  } else if (OB_UNLIKELY(!scn.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("get invalid argument", KR(ret), K(scn));
  } else {
    ctx_ = &ctx;
    scn_ = scn;
    for_old_mds_ = for_old_mds;
    is_inited_ = true;
  }
  return ret;
}

int ObTabletDeleteReplayExecutor::do_replay_(ObTabletHandle &tablet_handle)
{
  MDS_TG(10_ms);
  int ret = OB_SUCCESS;
  ObTablet *tablet = tablet_handle.get_obj();
  mds::MdsCtx &user_ctx = static_cast<mds::MdsCtx&>(*ctx_);
  ObTabletCreateDeleteMdsUserData data;
  const int64_t timeout = THIS_WORKER.get_timeout_remain();

  if (OB_ISNULL(tablet)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("tablet is null", K(ret), K(tablet_handle));
  } else if (CLICK_FAIL(tablet->get_latest_committed(data))) {
    LOG_WARN("failed to get tablet status", K(ret), K(timeout));
  } else {
    data.tablet_status_ = ObTabletStatus::DELETED;
    data.data_type_ = ObTabletMdsUserDataType::REMOVE_TABLET;
    if (CLICK_FAIL(replay_to_mds_table_(tablet_handle, data, user_ctx, scn_, for_old_mds_))) {
      LOG_WARN("failed to replay to tablet", K(ret));
    }
  }

  return ret;
}

}
}
