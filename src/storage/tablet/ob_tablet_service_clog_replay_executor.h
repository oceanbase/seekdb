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

#ifndef OCEANBASE_STORAGE_OB_TABLET_SERVICE_CLOG_REPLAY_EXECUTOR
#define OCEANBASE_STORAGE_OB_TABLET_SERVICE_CLOG_REPLAY_EXECUTOR

#include <stdint.h>
#include "common/ob_tablet_id.h"
#include "logservice/palf/lsn.h"
#include "logservice/replayservice/ob_tablet_replay_executor.h"
#include "share/scn.h"

namespace oceanbase
{
namespace storage
{
class ObLS;

class ObTabletServiceClogReplayExecutor final : public logservice::ObTabletReplayExecutor
{
public:
  ObTabletServiceClogReplayExecutor();
  int init(
      const char *buf,
      const int64_t log_size,
      const int64_t pos,
      const share::SCN &scn);

  TO_STRING_KV(KP_(buf), 
               K_(buf_size),
               K_(pos), 
               K_(scn));

protected:
  bool is_replay_update_tablet_status_() const override
  {
    return false;
  }

  // replay to the tablet
  // @return OB_SUCCESS, replay successfully, data has written to tablet.
  // @return OB_EAGAIN, failed to replay, need retry.
  // @return OB_NO_NEED_UPDATE, this log needs to be ignored.
  // @return other error codes, failed to replay.
  int do_replay_(ObTabletHandle &handle) override;

  virtual bool is_replay_update_mds_table_() const override
  {
    return false;
  }

private:
  const char *buf_;
  int64_t buf_size_;
  int64_t pos_;
  share::SCN scn_;
};

}
}
#endif
