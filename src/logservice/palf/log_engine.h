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

#ifndef OCEANBASE_LOGSERVICE_LOG_ENGINE_
#define OCEANBASE_LOGSERVICE_LOG_ENGINE_

#include "lib/lock/ob_mutex.h"
#include "lib/lock/ob_spin_lock.h"
#include "lib/utility/ob_print_utils.h"                // TO_STRING_KV
#include "log_storage.h"                               // LogStorage
#include "log_meta.h"                                  // LogMeta
#include "share/log/palf/log_define.h"
#include "log_shared_queue_thread.h"

namespace oceanbase
{
namespace common
{
class ObAddr;
class ObILogAllocator;
} // namespace common
namespace palf
{
class LogGroupEntry;
class LSN;
class LogIOWorker;
class LogSharedQueueTh;
class PalfHandleImpl;
class LogIOTask;
class LogHandleSubmitTask;
class LogIOFlushLogTask;
class LogIOTruncatePrefixBlocksTask;
class FlushLogCbCtx;
class TruncatePrefixBlocksCbCtx;
class LogWriteBuf;
class LogGroupEntryHeader;
class TruncatePrefixBlocksCbCtx;
class LogIOTruncatePrefixBlocksTask;

class LogEngine
{
  friend class PalfHandleImpl;
public:
  LogEngine();
  virtual ~LogEngine();

public:
  int init(share::LocalStore &store, const char *base_dir,
           const LogMeta &log_meta,
           common::ObILogAllocator *alloc_mgr,
           ILogBlockPool *log_block_pool,
           LogCache *log_cache,
           LogIOWorker *log_io_worker,
           LogSharedQueueTh *log_shared_queue_th,
           LogPlugins *plugins,
           const int64_t palf_epoch,
           const int64_t log_storage_block_size,
           LogIOAdapter *io_adapter);
  void destroy();

  int load(share::LocalStore &store, const char *base_dir,
           common::ObILogAllocator *alloc_mgr,
           ILogBlockPool *log_block_pool,
           LogCache *log_cache,
           LogIOWorker *log_io_worker,
           LogSharedQueueTh *log_shared_queue_th,
           LogPlugins *plugins,
           LSN &last_group_entry_header_lsn,
           LogGroupEntryHeader &entry_header,
           const int64_t palf_epoch,
           const int64_t log_storage_size,
           LogIOAdapter *io_adapter);

  // ==================== Submit async task start ================
  //
  int submit_flush_log_task(const FlushLogCbCtx &flush_log_cb_ctx,
                            const char *buf,
                            const int64_t buf_len);

  virtual int submit_flush_log_task(const FlushLogCbCtx &flush_log_cb_ctx, const LogWriteBuf &write_buf);
  virtual int submit_handle_submit_task();

  int submit_truncate_prefix_blocks_task(
      const TruncatePrefixBlocksCbCtx &truncate_prefix_blocks_ctx);

  // ==================== Submit aysnc task end ==================

  // ====================== LogStorage start =====================
  int append_log(const LSN &lsn, const LogWriteBuf &write_buf, const share::SCN &scn);
  int append_log(const LSNArray &lsn, const LogWriteBufArray &write_buf, const SCNArray &scn_array, int64_t &completed_count);
  int read_log(const LSN &lsn,
               const int64_t in_read_size,
               ReadBuf &read_buf,
               int64_t &out_read_size);
  int read_group_entry_header(const LSN &lsn, LogGroupEntryHeader &log_group_entry_header);
  int truncate(const LSN &lsn);
  int truncate_prefix_blocks(const LSN &lsn);
  int delete_block(const block_id_t &block_id);

  const LSN get_begin_lsn() const;
  int get_block_id_range(block_id_t &min_block_id, block_id_t &max_block_id) const;
  int get_block_min_scn(const block_id_t &block_id, share::SCN &scn) const;
  int raw_read(const LSN &lsn,
               const int64_t in_read_size,
               const bool need_read_block_header,
               ReadBuf &read_buf,
               int64_t &out_read_size,
               LogIOContext &io_ctx);
  //
  // ====================== LogStorage end =======================

  // ===================== MetaStorage start =====================
  //
  int advance_base_lsn(const LSN &lsn);
  int update_manifest(const block_id_t block_id);
  //
  // ===================== MetaStorage end =======================

  LogMeta get_log_meta() const;
  LSN get_base_lsn_used_for_block_gc() const;
  int get_min_block_info_for_gc(block_id_t &block_id, share::SCN &max_scn);
  int get_min_block_info(block_id_t &block_id, share::SCN &min_scn);
  LogStorage *get_log_storage() { return &log_storage_; }
  int get_total_used_disk_space(int64_t &total_used_size_byte,
                                int64_t &unrecyclable_disk_space) const;
  virtual int64_t get_palf_epoch() const { return palf_epoch_; }
  TO_STRING_KV(K_(is_inited), K_(min_block_max_scn), K_(min_block_id), K_(min_block_min_scn),
      K_(log_meta), K_(log_storage), K_(palf_epoch), KP(this));
private:
  // =========== Async callback task generate and destroy ==============
  int generate_flush_log_task_(const FlushLogCbCtx &flush_log_cb_ctx,
                               const LogWriteBuf &write_buf,
                               LogIOFlushLogTask *&flush_log_task);
  int generate_handle_submit_task_(LogHandleSubmitTask *&handle_submit_task);
  int generate_truncate_prefix_blocks_task_(
      const TruncatePrefixBlocksCbCtx &truncate_prefix_blocks_ctx,
      LogIOTruncatePrefixBlocksTask *&truncate_prefix_blocks_task);

  int try_clear_up_holes_and_check_storage_integrity_(
      const LSN &last_entry_begin_lsn,
      const block_id_t &expected_next_block_id,
      LogGroupEntryHeader &last_group_entry_header);
  bool check_last_block_whether_is_integrity_(const block_id_t expected_next_block_id,
                                              const block_id_t max_block_id,
                                              const LSN &log_storage_tail);

  void set_min_block_info_(const int64_t min_block_info_cache_version,
                           const block_id_t min_block_id,
                           const share::SCN &min_block_min_scn);

  void reset_min_block_info_();

  void set_min_block_info_for_gc_(const int64_t min_block_info_cache_version,
                                  const block_id_t min_block_id,
                                  const share::SCN &min_block_max_scn);

private:
  DISALLOW_COPY_AND_ASSIGN(LogEngine);

private:
  // ======================== begin used for GC ===========================
  mutable ObSpinLock min_block_info_lock_;
  // update it only in:
  // 1) get min block info for gc.
  // 2) delete min block.
  share::SCN min_block_max_scn_;
  // update it only in:
  // 1) get min block info for gc.
  // 2) get min block info
  // 3) delete min block.
  mutable block_id_t min_block_id_;
  // update it only in:
  // 1) get min block info for gc.
  // 2) get min block info.
  // 3) delete min block.
  share::SCN min_block_min_scn_;
  // update it only in reset_min_block_info_
  int64_t min_block_info_cache_version_;
  // ======================== end used for GC ===========================

  // Serialize writes without blocking readers on local storage IO.
  lib::ObMutex meta_write_lock_;
  mutable ObSpinLock log_meta_lock_;
  LogMeta log_meta_;
  share::LocalStore *store_;
  LogStorage log_storage_;
  common::ObILogAllocator *alloc_mgr_;
  LogIOWorker *log_io_worker_;
  LogSharedQueueTh *log_shared_queue_th_;
  LogPlugins *plugins_;
  // palf_epoch_ is used for identifying an uniq palf instance.
  int64_t palf_epoch_;
  bool is_inited_;
};
} // end namespace palf
} // end namespace oceanbase

#endif
