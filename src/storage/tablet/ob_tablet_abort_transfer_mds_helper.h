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

#ifndef OCEANBASE_STORAGE_OB_TABLET_ABORT_TRANSFER_MDS_HELPER
#define OCEANBASE_STORAGE_OB_TABLET_ABORT_TRANSFER_MDS_HELPER

#include <stdint.h>
#include "lib/container/ob_iarray.h"
#include "lib/utility/ob_macro_utils.h"
#include "common/ob_tablet_id.h"

namespace oceanbase
{
namespace share
{
class SCN;
class ObLSID;
struct ObTransferTabletInfo;
}
namespace storage
{

namespace mds
{
struct BufferCtx;
}

class ObLS;
class ObTablet;
class ObTabletHandle;
class ObTabletCreateDeleteMdsUserData;
class ObTXTransferInAbortedInfo;

class ObTabletAbortTransferHelper
{
public:
  static int on_register(
      const char* buf,
      const int64_t len,
      mds::BufferCtx &ctx);
  static int on_replay(
      const char* buf,
      const int64_t len,
      const share::SCN &scn,
      mds::BufferCtx &ctx);
  static bool check_can_do_tx_end(
       const bool is_willing_to_commit,
       const bool for_replay,
       const share::SCN &log_scn,
       const char *buf,
       const int64_t buf_len,
       mds::BufferCtx &ctx,
       const char *&can_not_do_reason);
private:
  static int on_register_success_(
      const ObTXTransferInAbortedInfo &transfer_in_aborted_info,
      mds::BufferCtx &ctx);
  static int on_replay_success_(
      const share::SCN &scn,
      const ObTXTransferInAbortedInfo &transfer_in_aborted_info,
      mds::BufferCtx &ctx);
  static int check_transfer_in_tablet_aborted_(
      const share::SCN &scn,
      const bool for_replay,
      const ObTXTransferInAbortedInfo &transfer_in_aborted_info,
      ObLS *ls);
  static int do_tx_end_before_commit_(
      const ObTXTransferInAbortedInfo &transfer_in_aborted_info,
      const share::SCN &abort_redo_scn,
      const char *&can_not_do_reason);
  static int check_can_skip_replay_(
      const share::SCN &scn,
      const ObTXTransferInAbortedInfo &transfer_in_aborted_info,
      bool &skip_replay);

private:
  DISALLOW_COPY_AND_ASSIGN(ObTabletAbortTransferHelper);
};

} // namespace storage
} // namespace oceanbase

#endif // OCEANBASE_STORAGE_OB_TABLET_ABORT_TRANSFER_MDS_HELPER
