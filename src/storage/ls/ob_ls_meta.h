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

#ifndef OCEABASE_STORAGE_OB_LS_META_
#define OCEABASE_STORAGE_OB_LS_META_

#include "lib/utility/utility.h"           // ObTimeGuard
#include "lib/lock/ob_spin_lock.h"
#include "share/log/palf/lsn.h"
#include "lib/ob_define.h"
#include "lib/function/ob_function.h"
#include "storage/ls/ob_ls_state.h"
#include "share/ls/ob_ls_restore_status.h"
#include "share/ls/ob_restore_status.h"
#include "storage/tx/ob_id_service.h"
#include "storage/ls/ob_ls_saved_info.h"
#include "share/scn.h"

namespace oceanbase
{
namespace storage
{
class ObLSMeta
{

  OB_UNIS_VERSION_V(3);
public:
  ObLSMeta();
  ObLSMeta(const ObLSMeta &ls_meta);
  ~ObLSMeta() {}
  void reset();
  bool is_valid() const;
  int set_start_work_state();
  int set_start_restore_state();
  int set_remove_state();
  const ObLSPersistentState &get_persistent_state() const;
  ObLSMeta &operator=(const ObLSMeta &other);
  share::SCN get_clog_checkpoint_scn() const;
  palf::LSN get_clog_base_lsn() const;
  int set_clog_checkpoint(const int64_t ls_epoch,
                          const palf::LSN &clog_checkpoint_lsn,
                          const share::SCN &clog_checkpoint_scn,
                          const bool write_slog);
  int set_restore_status(const int64_t ls_epoch, const ObRestoreStatus &restore_status);
  int get_restore_status(ObRestoreStatus &restore_status) const;
  int update_ls_replayable_point(const int64_t ls_epoch, const share::SCN &replayable_point);
  int get_ls_replayable_point(share::SCN &replayable_point);

  share::SCN get_tablet_change_checkpoint_scn() const;
  int set_tablet_change_checkpoint_scn(const int64_t ls_epoch, const share::SCN &tablet_change_checkpoint_scn);
  int update_id_meta(const int64_t ls_epoch,
                     const int64_t service_type,
                     const int64_t limited_id,
                     const share::SCN &latest_scn,
                     const bool write_slog);
  int get_all_id_meta(transaction::ObAllIDMeta &all_id_meta) const;
  int get_saved_info(ObLSSavedInfo &saved_info);
  int update_for_physical_restore(const int64_t ls_epoch, const ObLSMeta &source_meta);
  int build_saved_info(const int64_t ls_epoch);
  int clear_saved_info(const int64_t ls_epoch);
  int check_ls_need_online(bool &need_online) const;
  int init(
      const ObRestoreStatus &restore_status,
      const share::SCN &create_scn,
      const palf::LSN &clog_base_lsn);

  // IF I have locked with W:
  //    lock with R/W will be succeed do nothing.
  // ELSE:
  //    lock with R/W
  class ObReentrantWLockGuard
  {
  public:
    ObReentrantWLockGuard(common::ObLatch &lock,
                          const bool try_lock = false,
                          const int64_t warn_threshold = 100 * 1000 /* 100 ms */);
    ~ObReentrantWLockGuard();
    inline int get_ret() const { return ret_; }
    void click(const char *mod = NULL) { time_guard_.click(mod); }
    bool locked() const { return common::OB_SUCCESS == ret_; }
  private:
    bool first_locked_;
    ObTimeGuard time_guard_;
    common::ObLatch &lock_;
    int ret_;
  };
  class ObReentrantRLockGuard
  {
  public:
    ObReentrantRLockGuard(common::ObLatch &lock,
                          const bool try_lock = false,
                          const int64_t warn_threshold = 100 * 1000 /* 100 ms */);
    ~ObReentrantRLockGuard();
    inline int get_ret() const { return ret_; }
    void click(const char *mod = NULL) { time_guard_.click(mod); }
    bool locked() const { return common::OB_SUCCESS == ret_; }
  private:
    bool first_locked_;
    ObTimeGuard time_guard_;
    common::ObLatch &lock_;
    int ret_;
  };
  TO_STRING_KV(K_(ls_persistent_state),
               K_(clog_checkpoint_scn), K_(clog_base_lsn),
               K_(restore_status), K_(replayable_point), K_(tablet_change_checkpoint_scn),
               K_(all_id_meta));
private:
  int check_can_update_();
public:
  mutable common::ObLatch rw_lock_;     // only for atomic read/write in memory.
  mutable common::ObLatch update_lock_; // only one process can update ls meta. both for write slog and memory
  
private:
  ObLSPersistentState ls_persistent_state_;
  typedef common::ObFunction<int(const int64_t, const ObLSMeta &)> WriteSlog;
  // for test
  static WriteSlog write_slog_;

  // clog_checkpoint_scn_, meaning:
  // 1. dump points of all modules have exceeded clog_checkpoint_scn_
  // 2. all clog entries which log_scn are smaller than clog_checkpoint_scn_ can be recycled
  share::SCN clog_checkpoint_scn_;
  // clog_base_lsn_, meaning:
  // 1. all clog entries which lsn are smaller than clog_base_lsn_ have been recycled
  // 2. log_scn of log entry that clog_base_lsn_ points to is smaller than/equal to clog_checkpoint_scn_
  // 3. clog starts to replay log entries from clog_base_lsn_ on crash recovery
  palf::LSN clog_base_lsn_;
  ObRestoreStatus restore_status_;
  share::SCN replayable_point_;
  //TODO(yaoying.yyy):modify this
  share::SCN tablet_change_checkpoint_scn_;
  transaction::ObAllIDMeta all_id_meta_;
  ObLSSavedInfo saved_info_;
};

}  // namespace storage
}  // namespace oceanbase
#endif
