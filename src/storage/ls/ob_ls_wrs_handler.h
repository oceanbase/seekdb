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

#ifndef OCEANBASE_STORAGE_OB_LS_LOOP_WORKER
#define OCEANBASE_STORAGE_OB_LS_LOOP_WORKER
#include <stdint.h>

#include <sys/types.h>
#include "lib/lock/ob_spin_lock.h"
#include "lib/utility/ob_macro_utils.h"
#include "share/scn.h"
#include "share/ob_ls_id.h"
#include "storage/tx/ob_trans_define.h"

namespace oceanbase
{
namespace clog
{
class ObIPartitionLogService;
class ObISubmitLogCb;
}

namespace storage
{
class ObLS;


class ObLSWRSHandler
{
public:
  ObLSWRSHandler() { reset(); }
  ~ObLSWRSHandler() { reset(); }
  int init(const share::ObLSID &ls_id);
  void reset();
  int offline();
  int online();
  int generate_ls_weak_read_snapshot_version(oceanbase::storage::ObLS &ls,
                                              bool &need_skip,
                                              bool &is_user_ls,
                                              share::SCN &wrs_version,
                                              const int64_t max_stale_time);
  share::SCN get_ls_weak_read_ts() const { return ls_weak_read_ts_; }
  bool can_skip_ls() const { return !is_enabled_; }

  TO_STRING_KV(K_(is_inited), K_(is_enabled), K_(ls_id), K_(ls_weak_read_ts));

private:
  int generate_weak_read_timestamp_(oceanbase::storage::ObLS &ls, const int64_t max_stale_time, share::SCN &timestamp);

private:
  DISALLOW_COPY_AND_ASSIGN(ObLSWRSHandler);

protected:
  // check enable and protect ls_weak_read_ts_ modify
  common::ObSpinLock lock_;
  bool is_inited_;
  bool is_enabled_;
  share::ObLSID ls_id_;
  share::SCN ls_weak_read_ts_;
};

}
}

#endif // OCEANBASE_TRANSACTION_OB_LS_LOOP_WORKER
