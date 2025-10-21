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

#ifndef OCEANBASE_STORAGE_OB_TABLET_DELETE_MDS_HELPER
#define OCEANBASE_STORAGE_OB_TABLET_DELETE_MDS_HELPER

#include <stdint.h>
#include "storage/tx/ob_trans_define.h"

namespace oceanbase
{
namespace share
{
class SCN;
}

namespace obrpc
{
struct ObBatchRemoveTabletArg;
}

namespace storage
{
namespace mds
{
struct BufferCtx;
}

class ObTabletHandle;
class ObLSTabletService;

class ObTabletDeleteMdsHelper
{
public:
  static int on_register(
      const char* buf,
      const int64_t len,
      mds::BufferCtx &ctx);
  static int register_process(
      obrpc::ObBatchRemoveTabletArg &arg,
      mds::BufferCtx &ctx);
  static int on_commit_for_old_mds(
      const char* buf,
      const int64_t len,
      const transaction::ObMulSourceDataNotifyArg &notify_arg);
  static int on_replay(
      const char* buf,
      const int64_t len,
      const share::SCN &scn,
      mds::BufferCtx &ctx);
  static int replay_process(
      obrpc::ObBatchRemoveTabletArg &arg,
      const share::SCN &scn,
      mds::BufferCtx &ctx);
private:
  static int delete_tablets(
      const obrpc::ObBatchRemoveTabletArg &arg,
      mds::BufferCtx &ctx);
  static int replay_delete_tablets(
      const obrpc::ObBatchRemoveTabletArg &arg,
      const share::SCN &scn,
      mds::BufferCtx &ctx);
  static int set_tablet_deleted_status(
    ObLSTabletService *ls_tablet_service,
    ObTabletHandle &tablet_handle,
    mds::BufferCtx &ctx);
};
} // namespace storage
} // namespace oceanbase

#endif // OCEANBASE_STORAGE_OB_TABLET_DELETE_MDS_HELPER
