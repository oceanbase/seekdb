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

#ifndef OCEANBASE_PALF_LOG_SERVICE_
#define OCEANBASE_PALF_LOG_SERVICE_

#include <optional>
#include "common/ob_role.h"
#include "lib/lock/ob_spin_lock.h"
#include "lib/lock/ob_tc_rwlock.h"
#include "log_meta_info.h"
#include "palf_callback_wrapper.h"
#include "palf_iterator.h"
#include "rust/palf.h"
#include "share/log/palf/palf_options.h"

namespace oceanbase
{
namespace palf
{
using share::SCN;

struct PalfStat
{
  OB_UNIS_VERSION(1);
public:
  PalfStat();
  ~PalfStat() { reset(); }
  bool is_valid() const;
  void reset();
  common::ObAddr self_;
  AccessMode access_mode_;
  LSN begin_lsn_;
  share::SCN begin_scn_;
  LSN base_lsn_;
  LSN end_lsn_;
  share::SCN end_scn_;
  LSN max_lsn_;
  share::SCN max_scn_;
  TO_STRING_KV(K_(self), K_(access_mode), K_(begin_lsn), K_(begin_scn),
      K_(base_lsn), K_(end_lsn), K_(end_scn), K_(max_lsn), K_(max_scn));
};

struct PalfDiagnoseInfo
{
  PalfDiagnoseInfo() { reset(); }
  ~PalfDiagnoseInfo() { reset(); }
  LogState log_state_;
  void reset() { log_state_ = LogState::INVALID_STATE; }
  TO_STRING_KV(K(log_state_));
};

class PalfHandleImpl
{
public:
  PalfHandleImpl();
  ~PalfHandleImpl();
  int init(const AccessMode &access_mode, const PalfBaseInfo &palf_base_info,
      const char *log_dir, const PalfDiskOptions &disk_options, const common::ObAddr &self);
  int load(const char *log_dir, const PalfDiskOptions &disk_options, const common::ObAddr &self,
      bool &is_integrity);
  bool check_can_be_used() const;
  void destroy();
  int set_disk_options(const PalfDiskOptions &disk_options);
  int submit_log(const PalfAppendOptions &opts, const char *buf, const int64_t buf_len,
      const share::SCN &ref_scn, LSN &lsn, share::SCN &scn);
  int submit_imported_group(const LSN &source_lsn, const share::SCN &source_scn,
      const char *buf, const int64_t buf_len);
  int set_base_lsn(const LSN &lsn);
  int locate_by_scn_coarsely(const share::SCN &scn, LSN &result_lsn);
  int locate_by_lsn_coarsely(const LSN &lsn, share::SCN &result_scn);
  int get_access_mode_ref_scn(AccessMode &access_mode, SCN &ref_scn) const;
  int alloc_palf_buffer_iterator(const LSN &offset, PalfBufferIterator &iterator);
  int alloc_palf_buffer_iterator(const SCN &scn, PalfBufferIterator &iterator);
  int alloc_palf_group_buffer_iterator(const LSN &offset,
      PalfGroupBufferIterator &iterator);
  int alloc_palf_group_buffer_iterator(const share::SCN &scn,
      PalfGroupBufferIterator &iterator);
  int register_file_size_cb(palf::PalfFSCbNode *fs_cb);
  int unregister_file_size_cb(palf::PalfFSCbNode *fs_cb);
  int set_monitor_cb(PalfMonitorCb *monitor_cb);
  int reset_monitor_cb();
  int get_begin_lsn(LSN &lsn) const;
  int get_base_lsn(LSN &lsn) const;
  int get_base_info(const LSN &base_lsn, PalfBaseInfo &base_info);
  const LSN get_end_lsn() const;
  LSN get_max_lsn() const { return get_end_lsn(); }
  const share::SCN get_max_scn() const { return get_end_scn(); }
  const share::SCN get_end_scn() const;
  int get_total_used_disk_space(int64_t &total_used_disk_space,
      int64_t &unrecyclable_disk_space) const;
  int stat(PalfStat &palf_stat);
  int diagnose(PalfDiagnoseInfo &diagnose_info) const;
  int get_io_start_time(int64_t &io_start_time) const;
  TO_STRING_KV(K_(self));

private:
  typedef common::RWLock RWLock;
  typedef RWLock::RLockGuard RLockGuard;
  typedef RWLock::WLockGuard WLockGuard;
  class IoScope
  {
  public:
    explicit IoScope(PalfHandleImpl &handle) : handle_(handle) { handle_.begin_io_(); }
    ~IoScope() { handle_.end_io_(); }
  private:
    PalfHandleImpl &handle_;
  };
  template<typename IteratorType>
  int alloc_iterator_from_scn_(const SCN &scn, IteratorType &iterator);
  void begin_io_();
  void end_io_();

private:
  mutable RWLock lock_;
  std::optional<::rust::Box<palf_rs::Stream>> stream_;
  common::ObAddr self_;
  AccessMode access_mode_;
  palf::PalfFSCbWrapper fs_cb_wrapper_;
  LogPlugins plugins_;
  mutable common::ObSpinLock io_lock_;
  int64_t active_io_count_;
  int64_t io_start_time_;
  bool is_inited_;
};
} // namespace palf
} // namespace oceanbase
#endif
