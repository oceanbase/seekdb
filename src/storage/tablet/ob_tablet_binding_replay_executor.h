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

#ifndef OCEANBASE_STORAGE_OB_TABLET_BINDING_REPLAY_EXECUTOR
#define OCEANBASE_STORAGE_OB_TABLET_BINDING_REPLAY_EXECUTOR

#include "common/ob_tablet_id.h"
#include "logservice/replayservice/ob_tablet_replay_executor.h"
#include "storage/tablet/ob_tablet_binding_mds_user_data.h"

namespace oceanbase
{

namespace storage
{

class ObTabletBindingReplayExecutor final : public logservice::ObTabletReplayExecutor
{
public:
  ObTabletBindingReplayExecutor();

  int init(
      mds::BufferCtx &user_ctx,
      const ObTabletBindingMdsUserData &user_data,
      const share::SCN &scn,
      const bool for_old_mds);

protected:
  bool is_replay_update_tablet_status_() const override
  {
     // TODO (jiahua.cjh): binding is pre barrier that doesn't
     // need call ObLS::replay_get_tablet. Consider refactor base execuator interface
    return true;
  }

  int do_replay_(ObTabletHandle &tablet_handle) override;

  virtual bool is_replay_update_mds_table_() const override
  {
    return true;
  }

private:
  mds::BufferCtx *user_ctx_;
  const ObTabletBindingMdsUserData *user_data_;
  share::SCN scn_;
  bool for_old_mds_;
};


}
}

#endif
