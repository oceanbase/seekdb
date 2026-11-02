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

#ifndef OCEANBASE_STORAGE_OB_TABLET_SPLIT_REPLAY_EXECUTOR
#define OCEANBASE_STORAGE_OB_TABLET_SPLIT_REPLAY_EXECUTOR

#include "common/ob_tablet_id.h"
#include "logservice/replayservice/ob_tablet_replay_executor.h"
#include "storage/tablet/ob_tablet_split_mds_user_data.h"

namespace oceanbase
{
namespace storage
{

class ObTabletSplitReplayExecutor final : public logservice::ObTabletReplayExecutor
{
public:
  ObTabletSplitReplayExecutor()
    : logservice::ObTabletReplayExecutor(), user_ctx_(nullptr), scn_(), data_(nullptr) {}

  int init(mds::BufferCtx &user_ctx, const share::SCN &scn, const ObTabletSplitMdsUserData &data);

protected:
  bool is_replay_update_tablet_status_() const override
  {
    return true;
  }

  int do_replay_(ObTabletHandle &tablet_handle) override;

  virtual bool is_replay_update_mds_table_() const override
  {
    return true;
  }

private:
  mds::BufferCtx *user_ctx_;
  share::SCN scn_;
  const ObTabletSplitMdsUserData *data_;
};

}
}

#endif
