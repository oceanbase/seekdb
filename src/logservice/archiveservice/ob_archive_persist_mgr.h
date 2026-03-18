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

#ifndef OCEANBASE_ARCHIVE_OB_ARCHIVE_PERSIST_INFO_MGR_H_
#define OCEANBASE_ARCHIVE_OB_ARCHIVE_PERSIST_INFO_MGR_H_

#include "lib/utility/ob_print_utils.h"     // print
#include "lib/hash/ob_link_hashmap.h"           // ObLinkHashMap
#include "lib/lock/ob_spin_rwlock.h"           // SpinRWLock
#include "share/ob_ls_id.h"   // ObLSID
#include "share/backup/ob_archive_struct.h"    // ObArchiveRoundState ObLSArchivePersistInfo
#include "share/backup/ob_archive_persist_helper.h"
#include "ob_archive_define.h"                      // ArchiveKey

namespace oceanbase
{
namespace common
{
class ObMySQLProxy;
class ObSqlString;
}

namespace share
{
class ObTenantArchiveRoundAttr;
class ObLSID;
class ObLSArchivePersistInfo;
class SCN;
}

namespace storage
{
class ObLSService;
}

namespace palf
{
struct LSN;
}

namespace archive
{
class ObLSArchiveTask;
class ObArchiveRoundMgr;
class ObArchiveLSMgr;
using oceanbase::share::ObLSID;
using oceanbase::palf::LSN;
using oceanbase::storage::ObLSService;
using oceanbase::share::ObArchiveRoundState;
using oceanbase::share::ObTenantArchiveRoundAttr;
using oceanbase::share::ObLSArchivePersistInfo;
using oceanbase::share::ObArchivePersistHelper;

typedef common::LinkHashValue<ObLSID> ArchiveProValue;
struct ObArchivePersistValue : public ArchiveProValue
{
  typedef common::SpinRWLock RWLock;
  typedef common::SpinRLockGuard  RLockGuard;
  typedef common::SpinWLockGuard  WLockGuard;
  ObArchivePersistValue() : info_(), last_update_ts_(OB_INVALID_TIMESTAMP), speed_(0) {}

  void get(bool &is_madatory, int64_t &speed, ObLSArchivePersistInfo &info);
  int set(const bool is_madatory, const ObLSArchivePersistInfo &info);

  ObLSArchivePersistInfo info_;
  int64_t last_update_ts_;
  int64_t speed_;              // Bytes/s
  bool is_madatory_;
  RWLock rwlock_;
};

typedef common::ObLinkHashMap<ObLSID, ObArchivePersistValue> LSArchiveProMap;
class ObArchivePersistMgr
{
public:
  ObArchivePersistMgr();
  ~ObArchivePersistMgr();
  int init(const uint64_t tenant_id,
      common::ObMySQLProxy *proxy,
      ObLSService *ls_svr,
      ObArchiveLSMgr *ls_mgr,
      ObArchiveRoundMgr *round_mgr);

  void destroy();

public:
  // Get log stream archive persistence information for inner
  int get_archive_persist_info(const ObLSID &id,
      const ArchiveKey &key,
      ObLSArchivePersistInfo &info);
  // Real-time query whether the tenant is in archive mode
  int check_tenant_in_archive(bool &in_archive);
  // Check cache archive progress information
  int check_and_get_piece_persist_continuous(const ObLSID &id,
      ObLSArchivePersistInfo &info);
  // Get tenant archive configuration information for inner
  int load_archive_round_attr(ObTenantArchiveRoundAttr &attr);
  // Get log stream archive progress for global
  int get_ls_archive_progress(const ObLSID &id, LSN &lsn, share::SCN &scn, bool &force, bool &ignore);

  int get_ls_archive_speed(const ObLSID &id, int64_t &speed, bool &force, bool &ignore);
  // Persistence and loading of local archive progress cache interface
  void persist_and_load();
  // Get the timestamp of log stream creation
  int get_ls_create_scn(const share::ObLSID &id, share::SCN &scn);

private:
  // 1. Persist the current server's archive log stream archiving progress
  //
  // base: current round needs to persist state, conditions that do not require include
  //       1. observer round is different from rs
  //       2. rs current round status is STOP
  //
  // what: Which log streams need to persist archive progress?
  //       Need to persist the archiving progress for the log streams currently being archived by this server
  //
  // how:  The archiving progress originates from the archiving module ls_mgr_. If this log stream exists, it will be persisted; otherwise, it will be skipped.
  //
  // when: compare the progress with the local cache archive, if the local progress is the same as the value of ls_mgr_, it means there is no change in progress, and persistence is not needed
  //
  // other: STOPPING/STOP state, ls_mgr_ log stream is released by the log stream, need to set the archive progress status to STOP
  int persist_archive_progress_();
  // 2. Refresh the archive progress of all log streams on this server
  //
  // what: Which log streams need to load archive progress to local cache? All log streams that the local server has
  //
  // when: when archiving is enabled or archiving is STOP but the local cache's archiving progress is still DOING
  int load_archive_progress_(const ArchiveKey &key);
  // 3. Clean up the log stream archive progress records that no longer exist on this server
  int clear_stale_ls_();

  bool state_in_archive_(const share::ObArchiveRoundState &state) const;
  int load_ls_archive_progress_(const ObLSID &id,
      const ArchiveKey &key,
      ObLSArchivePersistInfo &info,
      bool &record_exist);

  int load_dest_mode_(bool &is_madatory);
  int update_local_archive_progress_(const ObLSID &id, const bool is_madatory, const ObLSArchivePersistInfo &info);

  int check_round_state_if_do_persist_(const ObTenantArchiveRoundAttr &attr,
      ArchiveKey &key,
      ObArchiveRoundState &state,
      bool &need_do);

  bool check_persist_authority_(const ObLSID &id) const;

  int build_ls_archive_info_(const ObLSID &id,
      const ArchiveKey &key,
      ObLSArchivePersistInfo &info,
      bool &exist);

  int check_ls_archive_progress_advance_(const ObLSID &id,
      const ObLSArchivePersistInfo &info,
      bool &advanced);

  int do_persist_(const ObLSID &id, const bool exist, ObLSArchivePersistInfo &info);
  int do_wipe_suspend_status_(const ObLSID &id);

  bool need_persist_(const ObArchiveRoundState &state) const;
  bool need_stop_status_(const ObArchiveRoundState &state) const;
  bool need_suspend_status_(const ObArchiveRoundState &state) const;
  bool need_wipe_suspend_status_(const ObArchiveRoundState &state) const;

private:
  static const int64_t PRINT_INTERVAL = 1 * 1000 * 1000L;

private:
  class DeleteStaleLSFunctor;
private:
  typedef common::SpinRWLock RWLock;
  typedef common::SpinRLockGuard  RLockGuard;
  typedef common::SpinWLockGuard  WLockGuard;
private:
  bool inited_;
  uint64_t tenant_id_;
  ArchiveKey tenant_key_;
  int64_t dest_no_;
  ObArchiveRoundState state_;
  mutable RWLock state_rwlock_;
  common::ObMySQLProxy *proxy_;
  ObLSService *ls_svr_;
  ObArchiveLSMgr *ls_mgr_;
  ObArchiveRoundMgr *round_mgr_;
  int64_t last_update_ts_;
  ObArchivePersistHelper table_operator_;
  LSArchiveProMap map_;
private:
  DISALLOW_COPY_AND_ASSIGN(ObArchivePersistMgr);
};

} // namespace archive
} // namespace oceanbase

#endif /* OCEANBASE_ARCHIVE_OB_ARCHIVE_PERSIST_INFO_MGR_H_ */
