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

#include "storage/tablet/ob_tablet_binding_replay_executor.h"

#define USING_LOG_PREFIX STORAGE

namespace oceanbase
{
namespace storage
{

ObTabletBindingReplayExecutor::ObTabletBindingReplayExecutor()
  :logservice::ObTabletReplayExecutor(), user_ctx_(nullptr), user_data_(nullptr)
{}

int ObTabletBindingReplayExecutor::init(
    mds::BufferCtx &user_ctx,
    const ObTabletBindingMdsUserData &user_data,
    const share::SCN &scn,
    const bool for_old_mds)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(is_inited_)) {
    ret = OB_INIT_TWICE;
    LOG_WARN("tablet binding replay executor init twice", KR(ret), K_(is_inited));
  } else if (OB_UNLIKELY(!scn.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("get invalid argument", KR(ret), K(scn));
  } else {
    user_ctx_ = &user_ctx;
    user_data_ = &user_data;
    scn_ = scn;
    for_old_mds_ = for_old_mds;
    is_inited_ = true;
  }
  return ret;
}


int ObTabletBindingReplayExecutor::do_replay_(ObTabletHandle &tablet_handle)
{
  int ret = OB_SUCCESS;
  mds::MdsCtx &user_ctx = static_cast<mds::MdsCtx&>(*user_ctx_);

  if (OB_FAIL(replay_to_mds_table_(tablet_handle, *user_data_, user_ctx, scn_, for_old_mds_))) {
    LOG_WARN("failed to replay to tablet", K(ret));
  }

  return ret;
}

}
}
