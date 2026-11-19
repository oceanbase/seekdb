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
#include "lib/lock/ob_tc_rwlock.h"
#include "log_io_task_cb_utils.h"
#include "log_meta_info.h"
#include "palf_callback_wrapper.h"
#include "palf_iterator.h"
#include "rust/palf.h"
#include "share/log/palf/palf_options.h"

namespace oceanbase
{
namespace common
{
class ObILogAllocator;
}
namespace share
{
class LocalStore;
}
namespace palf
{
using share::SCN;
class LogWriteBuf;
class LogIOWorker;
class LogSharedQueueTh;
class LogIOAdapter;
class ILogBlockPool;
class IPalfEnvImpl;

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

class IPalfHandleImpl
{
public:
  IPalfHandleImpl() = default;
  virtual ~IPalfHandleImpl() = default;
  virtual bool check_can_be_used() const = 0;
  virtual int submit_log(const PalfAppendOptions &opts, const char *buf,
      const int64_t buf_len, const share::SCN &ref_scn, LSN &lsn, share::SCN &scn) = 0;
  virtual int submit_imported_group(const LSN &source_lsn, const share::SCN &source_scn,
      const char *buf, const int64_t buf_len) = 0;
  virtual int set_base_lsn(const LSN &lsn) = 0;
  virtual void set_deleted() = 0;
  virtual int locate_by_scn_coarsely(const share::SCN &scn, LSN &result_lsn) = 0;
  virtual int locate_by_lsn_coarsely(const LSN &lsn, share::SCN &result_scn) = 0;
  virtual int get_begin_lsn(LSN &lsn) const = 0;
  virtual int get_begin_scn(share::SCN &scn) = 0;
  virtual int get_base_lsn(LSN &lsn) const = 0;
  virtual int get_base_info(const LSN &base_lsn, PalfBaseInfo &base_info) = 0;
  virtual int get_min_block_id_for_gc(block_id_t &min_block_id) = 0;
  virtual int get_min_block_info_for_gc(block_id_t &min_block_id, share::SCN &max_scn) = 0;
  virtual const LSN get_end_lsn() const = 0;
  virtual LSN get_max_lsn() const = 0;
  virtual const share::SCN get_max_scn() const = 0;
  virtual const share::SCN get_end_scn() const = 0;
  virtual const LSN get_readable_end_lsn() const = 0;
  virtual int get_total_used_disk_space(int64_t &total_used_disk_space,
      int64_t &unrecyclable_disk_space) const = 0;
  virtual LSN get_base_lsn_used_for_block_gc() const = 0;
  virtual int delete_block(const block_id_t &block_id) = 0;
  virtual int inner_after_flush_log(const FlushLogCbCtx &flush_log_cb_ctx) = 0;
  virtual int inner_after_truncate_prefix_blocks(
      const TruncatePrefixBlocksCbCtx &truncate_prefix_cb_ctx) = 0;
  virtual int advance_reuse_lsn(const LSN &flush_log_end_lsn) = 0;
  virtual int inner_append_log(const LSN &lsn, const LogWriteBuf &write_buf,
      const share::SCN &scn) = 0;
  virtual int inner_append_log(const LSNArray &lsn_array,
      const LogWriteBufArray &write_buf_array, const SCNArray &scn_array,
      int64_t &completed_count) = 0;
  virtual int inner_truncate_prefix_blocks(const LSN &lsn) = 0;
  virtual int check_and_switch_state() = 0;
  virtual int check_and_switch_freeze_mode() = 0;
  virtual bool is_in_period_freeze_mode() const = 0;
  virtual int period_freeze_last_log() = 0;
  virtual int set_scan_disk_log_finished() = 0;
  virtual int get_access_mode_ref_scn(AccessMode &access_mode, SCN &ref_scn) const = 0;
  virtual int alloc_palf_buffer_iterator(const LSN &offset, PalfBufferIterator &iterator) = 0;
  virtual int alloc_palf_buffer_iterator(const SCN &scn, PalfBufferIterator &iterator) = 0;
  virtual int alloc_palf_group_buffer_iterator(const LSN &offset,
      PalfGroupBufferIterator &iterator) = 0;
  virtual int alloc_palf_group_buffer_iterator(const share::SCN &scn,
      PalfGroupBufferIterator &iterator) = 0;
  virtual int register_file_size_cb(palf::PalfFSCbNode *fs_cb) = 0;
  virtual int unregister_file_size_cb(palf::PalfFSCbNode *fs_cb) = 0;
  virtual int stat(PalfStat &palf_stat) = 0;
  virtual int get_palf_epoch(int64_t &palf_epoch) const = 0;
  virtual int diagnose(PalfDiagnoseInfo &diagnose_info) const = 0;
  virtual int read_data_from_buffer(const LSN &read_begin_lsn, const int64_t in_read_size,
      char *buf, int64_t &out_read_size) const = 0;
  virtual int raw_read(const palf::LSN &lsn, char *read_buf, const int64_t nbytes,
      int64_t &read_size, LogIOContext &io_ctx) = 0;
  virtual int try_handle_next_submit_log() = 0;
  DECLARE_PURE_VIRTUAL_TO_STRING;
};

class PalfHandleImpl : public IPalfHandleImpl
{
public:
  PalfHandleImpl();
  ~PalfHandleImpl() override;
  int init(share::LocalStore &store, const AccessMode &access_mode,
      const PalfBaseInfo &palf_base_info, const char *log_dir,
      ObILogAllocator *alloc_mgr, ILogBlockPool *log_block_pool,
      LogIOWorker *log_io_worker, LogSharedQueueTh *log_shared_queue_th,
      IPalfEnvImpl *palf_env_impl, const common::ObAddr &self,
      const int64_t palf_epoch, LogIOAdapter *io_adapter);
  int load(share::LocalStore &store, const char *log_dir,
      ObILogAllocator *alloc_mgr, ILogBlockPool *log_block_pool,
      LogIOWorker *log_io_worker, LogSharedQueueTh *log_shared_queue_th,
      IPalfEnvImpl *palf_env_impl, const common::ObAddr &self,
      const int64_t palf_epoch, LogIOAdapter *io_adapter, bool &is_integrity);
  bool check_can_be_used() const override final;
  void destroy();
  int start();
  int set_disk_limit(const int64_t disk_limit);
  int submit_log(const PalfAppendOptions &opts, const char *buf, const int64_t buf_len,
      const share::SCN &ref_scn, LSN &lsn, share::SCN &scn) override final;
  int submit_imported_group(const LSN &source_lsn, const share::SCN &source_scn,
      const char *buf, const int64_t buf_len) override final;
  int set_base_lsn(const LSN &lsn) override final;
  void set_deleted() override final;
  int locate_by_scn_coarsely(const share::SCN &scn, LSN &result_lsn) override final;
  int locate_by_lsn_coarsely(const LSN &lsn, share::SCN &result_scn) override final;
  int read_data_from_buffer(const LSN &read_begin_lsn, const int64_t in_read_size,
      char *buf, int64_t &out_read_size) const override final;
  int raw_read(const palf::LSN &lsn, char *buffer, const int64_t nbytes,
      int64_t &read_size, LogIOContext &io_ctx) override final;
  int try_handle_next_submit_log() override final { return OB_SUCCESS; }
  int delete_block(const block_id_t &block_id) override final;
  int set_scan_disk_log_finished() override final { return OB_SUCCESS; }
  int get_access_mode_ref_scn(AccessMode &access_mode, SCN &ref_scn) const override final;
  int alloc_palf_buffer_iterator(const LSN &offset, PalfBufferIterator &iterator) override final;
  int alloc_palf_buffer_iterator(const SCN &scn, PalfBufferIterator &iterator) override final;
  int alloc_palf_group_buffer_iterator(const LSN &offset,
      PalfGroupBufferIterator &iterator) override final;
  int alloc_palf_group_buffer_iterator(const share::SCN &scn,
      PalfGroupBufferIterator &iterator) override final;
  int register_file_size_cb(palf::PalfFSCbNode *fs_cb) override final;
  int unregister_file_size_cb(palf::PalfFSCbNode *fs_cb) override final;
  int set_monitor_cb(PalfMonitorCb *monitor_cb);
  int reset_monitor_cb();
  int get_begin_lsn(LSN &lsn) const override final;
  int get_begin_scn(share::SCN &scn) override final;
  int get_base_lsn(LSN &lsn) const override final;
  int get_base_info(const LSN &base_lsn, PalfBaseInfo &base_info) override final;
  int get_min_block_id_for_gc(block_id_t &min_block_id) override final;
  int get_min_block_info_for_gc(block_id_t &min_block_id, share::SCN &max_scn) override final;
  const LSN get_end_lsn() const override final;
  LSN get_max_lsn() const override final { return get_end_lsn(); }
  const share::SCN get_max_scn() const override final { return get_end_scn(); }
  const share::SCN get_end_scn() const override final;
  const LSN get_readable_end_lsn() const override final { return get_end_lsn(); }
  int get_total_used_disk_space(int64_t &total_used_disk_space,
      int64_t &unrecyclable_disk_space) const override final;
  LSN get_base_lsn_used_for_block_gc() const override final;
  int inner_after_flush_log(const FlushLogCbCtx &flush_log_cb_ctx) override final;
  int inner_after_truncate_prefix_blocks(
      const TruncatePrefixBlocksCbCtx &truncate_prefix_cb_ctx) override final;
  int advance_reuse_lsn(const LSN &flush_log_end_lsn) override final;
  int inner_append_log(const LSN &lsn, const LogWriteBuf &write_buf,
      const share::SCN &scn) override final;
  int inner_append_log(const LSNArray &lsn_array, const LogWriteBufArray &write_buf_array,
      const SCNArray &scn_array, int64_t &completed_count) override final;
  int inner_truncate_prefix_blocks(const LSN &lsn) override final;
  int check_and_switch_state() override final { return OB_SUCCESS; }
  int check_and_switch_freeze_mode() override final { return OB_SUCCESS; }
  bool is_in_period_freeze_mode() const override final { return false; }
  int period_freeze_last_log() override final { return OB_SUCCESS; }
  int stat(PalfStat &palf_stat) override final;
  int get_palf_epoch(int64_t &palf_epoch) const override final;
  int diagnose(PalfDiagnoseInfo &diagnose_info) const override final;
  TO_STRING_KV(K_(self), K_(has_set_deleted));

private:
  typedef common::RWLock RWLock;
  typedef RWLock::RLockGuard RLockGuard;
  typedef RWLock::WLockGuard WLockGuard;
  template<typename IteratorType>
  int alloc_iterator_from_scn_(const SCN &scn, IteratorType &iterator);

private:
  mutable RWLock lock_;
  std::optional<::rust::Box<palf_rs::Stream>> stream_;
  char log_dir_[common::MAX_PATH_SIZE];
  common::ObAddr self_;
  AccessMode access_mode_;
  palf::PalfFSCbWrapper fs_cb_wrapper_;
  LogPlugins plugins_;
  bool has_set_deleted_;
  IPalfEnvImpl *palf_env_impl_;
  int64_t palf_epoch_;
  bool is_inited_;
};
} // namespace palf
} // namespace oceanbase
#endif
