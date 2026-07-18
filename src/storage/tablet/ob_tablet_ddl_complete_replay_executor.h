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

#ifndef OCEANBASE_STORAGE_OB_TABLET_DDL_COMPLETE_REPLAY_EXECUTOR
#define OCEANBASE_STORAGE_OB_TABLET_DDL_COMPLETE_REPLAY_EXECUTOR

#include "logservice/replayservice/ob_tablet_replay_executor.h"
#include "share/scn.h"

namespace oceanbase
{
namespace storage
{
namespace mds
{
class BufferCtx;
}

class ObTabletDDLCompleteReplayExecutor final : public logservice::ObTabletReplayExecutor
{
public:
  ObTabletDDLCompleteReplayExecutor();

  int init(
      mds::BufferCtx &user_ctx,
      const share::SCN &scn,
      const bool for_old_mds,
      const ObTabletDDLCompleteMdsUserData &user_data);
  static int freeze_ddl_kv(ObTablet &tablet, const ObTabletDDLCompleteMdsUserData &user_data);
  static int update_tablet_table_store(ObTablet &tablet, const ObTabletDDLCompleteMdsUserData &user_data);
  static int schedule_merge(ObTablet &table, const ObTabletDDLCompleteMdsUserData &user_data);

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
  bool for_old_mds_;
  const ObTabletDDLCompleteMdsUserData *user_data_;
};
} // namespace storage
} // namespace oceanbase

#endif // OCEANBASE_STORAGE_OB_TABLET_REPLAY_EXECUTOR
